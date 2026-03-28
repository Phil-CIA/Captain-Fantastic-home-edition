# SPI Handshake — Two-Wire RTS/CTS Flow Control

## Overview

A two-wire handshake protocol is added to the IDC-10 ribbon cable between
the **host ESP32-C6** (SPI master) and the **display-board ESP32-C6** (SPI
slave) to prevent the master from clocking data before the slave is ready.
Without this, the master can assert CS and clock SCLK while the slave's DMA
buffer is still being filled, leading to garbled transactions.

## Why CS Alone Is Not Enough

CS (Chip Select) marks which device on the bus to talk to and defines the
frame boundary.  It does **not** tell the master whether the slave's software
or DMA has finished preparing its transmit buffer.  On an ESP32 SPI slave the
application must explicitly queue a buffer before the next transaction; if the
master starts clocking before that is done the slave returns stale bytes.

The READY line solves this:

> **Master only asserts CS when READY is HIGH.**

## Signal Definitions

| Line    | GPIO | IDC pin | Direction          | Active level | Meaning when active                       |
|---------|------|---------|--------------------|--------------|-------------------------------------------|
| REQUEST | 0    | 5       | HOST → DISPLAY     | HIGH         | Host wants to start a transaction         |
| READY   | 1    | 9       | DISPLAY → HOST     | HIGH         | Display slave buffer loaded, ready to Tx  |

Both lines are 3.3 V CMOS logic driven directly between ESP32-C6 GPIOs.
No pull-up or pull-down resistors are required; both pins are driven at all
times by their respective owners.

### GPIO0 / IDC pin 5 — REQUEST (host → display)

- **Host** configures as `OUTPUT`, drives `HIGH` when it intends to begin a
  transaction, `LOW` when idle.
- **Display** configures as `INPUT`, samples the pin to decide whether to arm
  its slave buffer.
- Optional: the display can use a GPIO interrupt on the rising edge of REQUEST
  to trigger buffer preparation without polling.

### GPIO1 / IDC pin 9 — READY (display → host)

- **Display** configures as `OUTPUT`, drives `HIGH` when the slave buffer is
  fully loaded and the peripheral is armed, `LOW` when busy.
- **Host** configures as `INPUT`, polls or waits (with timeout) for the pin
  to go `HIGH` before asserting CS.

## Transaction Timing

```
HOST                               DISPLAY
  |                                   |
  |-- REQUEST ↑ (GPIO0 HIGH) -------->|   Host signals: "prepare for a txn"
  |                                   |-- load slave TX buffer
  |                                   |-- READY ↑ (GPIO1 HIGH) -----------|
  |<-- READY ↑ seen                   |
  |-- CS ↓ (assert CS)                |   Host begins transaction
  |-- [SCLK / MOSI / MISO clocked] --|
  |-- CS ↑ (deassert CS)             |   Transaction complete
  |-- REQUEST ↓ (GPIO0 LOW) -------->|   Host signals: "done"
  |                                   |-- process received frame
  |                                   |-- READY ↓ (GPIO1 LOW) ------------|
  |                                   |   Slave marks itself busy again
```

**Idle state:** both REQUEST and READY are LOW.

**Minimum pulse widths:**
- REQUEST must remain HIGH for at least 10 µs before the host asserts CS to
  give the display time to detect it via polling or interrupt.
- READY must remain HIGH until at least the trailing edge of CS.

**READY timeout:**
- The host waits up to `SPI_HANDSHAKE_READY_TIMEOUT_MS` (default 100 ms).
- If READY does not arrive in time a timeout warning is logged and the
  transaction is skipped.  The host then tries again on the next cycle.

## Initial SPI Clock Rate

Start at **1 MHz** (`HOST_SPI_CLOCK_HZ = 1000000`).  This is safe across a
typical 30 cm IDC ribbon.  Once handshake bring-up is confirmed you can raise
to 4–8 MHz; avoid exceeding 10 MHz without adding series termination
resistors.

## Source Files

| File | Purpose |
|------|---------|
| `Captain-v2/include/spi_handshake_config.h` | Pin numbers, polarity, timeout constants |
| `Captain-v2/include/spi_handshake.h`        | API declarations (host + display) |
| `Captain-v2/src/spi_handshake.cpp`          | Implementation with Serial logging |
| `Captain-v2/src/display_main.cpp`           | Display-board ESP32-C6 firmware |
| `Captain-v2/src/host_display_main.cpp`      | Host-side display-link firmware stub |

## PlatformIO Build Environments

| Environment           | Board          | Role |
|-----------------------|----------------|------|
| `captain_display`     | ESP32-C6       | Display board (SPI slave) |
| `captain_host_display`| ESP32-C6       | Host display-link (SPI master) |
| `captain_control`     | ESP32 DevKit   | Main pinball controller |
| `captain_matrix`      | ESP32 DevKit   | Switch/lamp matrix |

Flash the display board:
```
cd Captain-v2
pio run -e captain_display --target upload
```

Flash the host display-link:
```
cd Captain-v2
pio run -e captain_host_display --target upload
```

## Bring-Up Checklist

### 1. Wiring verification (with multimeter, boards unpowered)

- [ ] Continuity: host GPIO0 → IDC pin 5 → display GPIO0
- [ ] Continuity: host GPIO1 → IDC pin 9 → display GPIO1
- [ ] No short between GPIO0 and GPIO1
- [ ] No short between handshake lines and SPI bus lines

### 2. Power-on idle state (oscilloscope or logic analyser)

- [ ] GPIO0 (REQUEST) reads LOW on both boards before any transaction
- [ ] GPIO1 (READY) reads LOW on both boards before any transaction
- [ ] Serial monitor shows `[HS] REQUEST line: idle LOW — OK`
- [ ] Serial monitor shows `[HS] READY line: idle LOW — OK`

### 3. REQUEST toggle test

Flash the host firmware and confirm you can see REQUEST pulse on a scope
at GPIO0:
- Rising edge when `spiHandshakeHostSetRequest(true)` is called
- Falling edge when `spiHandshakeHostSetRequest(false)` is called
- Expected pulse width ≥ 10 µs before CS asserts

### 4. READY toggle test

Flash the display firmware and confirm READY on GPIO1 toggles each time
`spiHandshakeDisplaySignalReady(true/false)` is called.

### 5. First loopback transaction

With both boards connected:
1. Open two Serial monitors (one per board).
2. Host should log `[HOST] tx STATE seq=0 OK` each successful cycle.
3. Display should log `[DISP] rx: type=0x01 seq=…` on each received frame.
4. Confirm no `[HS] … timeout` or `[DISP] rx: CRC mismatch` messages.

### 6. Clock rate increase

Once step 5 is stable for 60 seconds, raise `HOST_SPI_CLOCK_HZ` to
4 MHz and repeat.  Confirm CRC pass rate remains 100 %.

## Debug Logging

All log lines from the handshake module are prefixed `[HS]`.
Display firmware log lines are prefixed `[DISP]`.
Host firmware log lines are prefixed `[HOST]`.

To disable handshake verbose logging in a production build, set:
```
build_flags = -DSPI_HANDSHAKE_DEBUG=0
```
in the relevant `platformio.ini` environment.
