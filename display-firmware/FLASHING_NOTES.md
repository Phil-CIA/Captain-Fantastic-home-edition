# Flashing Notes — Display ESP32-C6 Board

## Board identification

The display board is an **ESP32-C6 DevKitM Amazon clone** (commonly sold as "C6 Mini" or "C6 SuperMini").  It carries a USB-C connector wired directly to the ESP32-C6 built-in **USB-Serial/JTAG** peripheral—no external USB-to-serial bridge chip is present.  These boards are widely available on Amazon and AliExpress.

---

## Normal flash procedure

1. Disconnect or power off the host board (or ensure its GPIO0 / GPIO1 outputs are high-impedance—see below).
2. Connect the display board to your PC via its USB-C cable.
3. Press and hold **BOOT** (GPIO0 button), tap **RESET** (EN button), then release **BOOT**.  
   Alternatively, PlatformIO and `esptool` can enter download mode automatically via the built-in USB-JTAG RTS/DTR toggling—no button press needed in most cases.
4. Flash normally:
   ```
   pio run -e display_board -t upload
   ```
   or
   ```
   esptool.py --chip esp32c6 write_flash 0x0 firmware.bin
   ```
5. Reconnect the host board after flashing completes.

---

## ⚠ GPIO0 strapping-pin hazard

GPIO0 is sampled by the ROM bootloader **at every reset**.

| GPIO0 state at reset | Result |
|---|---|
| HIGH (or floating with pull-up) | Normal application boot |
| LOW | Download / flash mode — application does **not** start |

The on-board BOOT button pulls GPIO0 LOW intentionally.  Any external circuit that does the same—even accidentally—will prevent the application from booting.

**GPIO0 is connected to the IDC-10 ribbon on pin 5 (schematic label: RTS/CTS) as the `HOST_REQ` handshake line.**

---

## Required hardware safeguards (apply before connecting the ribbon)

### 1. 10 kΩ pull-up on GPIO0 (display side)

Place a 10 kΩ resistor between GPIO0 and 3.3 V **on the display PCB or IDC breakout**, before the series resistor.  This ensures GPIO0 stays HIGH when the host is unpowered or when the host pin is high-impedance.

```
3V3 ──[ 10kΩ ]──┬── GPIO0 (display)
                │
           [ 470Ω ]   ← series resistor on ribbon
                │
           IDC pin 5 ── HOST_REQ output (host side)
```

### 2. 470 Ω series resistors on both handshake lines

Add a 470 Ω resistor in series on the IDC cable (or on the host PCB) for each handshake line:
- **IDC pin 5** (GPIO0 / HOST_REQ)
- **IDC pin 9** (GPIO1 / DISP_READY)

These resistors limit contention current and slow edge transitions enough to prevent glitches during simultaneous power-on of both boards.

### 3. Host MCU must default to high-impedance on handshake pins

Configure the host MCU pins driving `HOST_REQ` and `DISP_READY` as **inputs** from power-on until the host application firmware is running.  Add this to the host board's early startup code:

```cpp
// Host board early setup — before any SPI transactions
pinMode(HOST_LINK_HOST_REQ_PIN,   INPUT);   // high-Z until app is ready
pinMode(HOST_LINK_DISP_READY_PIN, INPUT);   // high-Z until app is ready
```

Switch them to their active direction only after both boards have fully booted:

```cpp
// Host: switch to output once app is running
pinMode(HOST_LINK_HOST_REQ_PIN,   OUTPUT);
digitalWrite(HOST_LINK_HOST_REQ_PIN, LOW);  // idle LOW = no request pending
```

---

## GPIO1 notes

GPIO1 is **not** a strapping pin and is generally safe to use as the `DISP_READY` output.  However, on some Amazon clone boards GPIO1 may be routed to a test point (TP19 on the schematic).  Verify continuity between the IDC pin 9 trace and GPIO1 with a multimeter before first use.  A 470 Ω series resistor is still recommended.

---

## USB D+/D− lines — do not use GPIO12/GPIO13

The built-in USB-Serial/JTAG controller uses:
- **GPIO12** — USB D−
- **GPIO13** — USB D+

Do not connect external signals to these pins.  Doing so will break USB connectivity and may damage the USB controller.

---

## UART0 flash path — reserved GPIO16/GPIO17

UART0 defaults to **GPIO16 (TXD)** and **GPIO17 (RXD)** on the ESP32-C6.  These pins are left unconnected on the IDC ribbon to preserve the ability to use an external USB-to-serial adapter for recovery flashing if the USB-JTAG path becomes unavailable.

---

## Quick "is it safe to flash?" checklist

- [ ] Host board powered off **or** host handshake pins (GPIO0/1 equivalents) confirmed as inputs
- [ ] 10 kΩ pull-up installed on GPIO0 (display side)
- [ ] 470 Ω series resistors installed on IDC pins 5 and 9
- [ ] USB-C cable connected display board → PC
- [ ] `esptool` / PlatformIO can see the board at the correct COM/tty port

---

## SPI pin reference (display firmware)

| Signal | GPIO | IDC-10 pin | Direction |
|---|---|---|---|
| HOST_LINK SCLK | 4 | 3 | Input (host drives) |
| HOST_LINK MOSI | 5 | 4 | Input (host drives) |
| HOST_LINK MISO | 6 | 10 | Output (display drives) |
| HOST_LINK CS | 7 | 6 | Input (host drives, active LOW) |
| HOST_REQ | 0 | 5 | Input — ⚠ strapping pin; see pull-up rule |
| DISP_READY | 1 | 9 | Output |

> `HOST_REQ` (firmware name) corresponds to the schematic label "RTS/CTS" on IDC pin 5.

See `include/host_link_config.h` for the corresponding C++ `constexpr` definitions.
