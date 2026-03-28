# Display Board — Flashing Notes & Handshake Pin Rationale

Target hardware: **ESP32-C6 DevKitM** (Amazon clone, "C6 Mini")  
Host-link SPI + handshake defined in `include/host_link_config.h`.

---

## Chosen handshake pins: GPIO0 (HOST\_REQ) and GPIO1 (DISP\_READY)

### Why not GPIO8 or GPIO9?

The ESP32-C6 has two boot/strap pins that are sampled at reset before firmware runs:

| Pin   | Strapping function | Safe level | What goes wrong if held LOW externally |
|-------|--------------------|------------|----------------------------------------|
| **GPIO9** | Boot mode selector — HIGH = SPI-Flash boot (normal run); LOW = Download/USB-Serial boot | Must be HIGH for normal boot | Board enters download mode on every reset; firmware never runs |
| **GPIO8** | JTAG signal source — HIGH = built-in USB-JTAG; LOW = external JTAG IO pins | Normally HIGH (internal pull-up) | USB-JTAG flashing may fail; potential interference with debug interface |

If either of these pins is connected to a handshake line that the host board
drives LOW while the display board resets, the result is a board that **cannot
boot or flash without disconnecting the ribbon**.  
Because the host and display boards power up and reset at slightly different
times, this failure mode is hard to avoid without extra reset-synchronisation
logic.

### Why GPIO0 and GPIO1 are safe

On ESP32-C6 (unlike the original ESP32 or ESP32-S2), **GPIO0 and GPIO1 are
ordinary GPIOs with no strapping function**.  The chip boots normally
regardless of the level on these pins.  This means:

- The display board boots cleanly even if the host board drives GPIO0 or GPIO1
  to any level during reset.
- No special power-sequencing or reset-synchronisation logic is required.
- Flashing via USB-C on the DevKitM is unaffected.

---

## Pin summary

```
GPIO  Direction (display side)  Signal       IDC cable wire
────  ──────────────────────    ──────────   ──────────────
  4   IN  (SPI MISO)            MISO         pin N+0
  5   IN  (SPI CS, active-LOW)  CS           pin N+1
  6   IN  (SPI MOSI)            MOSI         pin N+2
  7   IN  (SPI CLK)             CLK          pin N+3
  0   IN  (handshake input)     HOST_REQ     pin 5  (host → display)
  1   OUT (handshake output)    DISP_READY   pin 9  (display → host)
  8   —   DO NOT CONNECT        (strapping)
  9   —   DO NOT CONNECT        (strapping / BOOT button)
```

---

## Handshake direction and polarity

```
HOST_REQ   (GPIO0) — Host → Display, active-HIGH
                     1 = Host wants to perform a SPI transaction
                     0 = Idle (no request pending)

DISP_READY (GPIO1) — Display → Host, active-HIGH
                     1 = Display has TX/RX buffers ready; host may assert CS
                     0 = Display is busy / not yet ready
```

**Protocol rule:** The host must only assert CS (start a SPI transaction)
when **both** of the following are true:

1. `DISP_READY` is HIGH (display has advertised readiness), **or** the host is
   responding to its own `HOST_REQ` pulse (polled mode).
2. `HOST_REQ` has been asserted HIGH for at least one full bus-clock period.

This prevents the host from clocking garbage when the display is still
initialising or handling a previous transaction.

---

## Electrical recommendations

| Signal      | Series R (at source) | Pull (at receiver) | Notes |
|-------------|---------------------|--------------------|-------|
| HOST\_REQ   | 470 Ω at host board | 10 kΩ pull-**down** at display board | Keeps GPIO0 LOW when host is off or in reset |
| DISP\_READY | 470 Ω at display board | none required | Display drives LOW until ready; no float risk |
| SPI CLK/MOSI/CS | 470 Ω at host board | none | Limits ringing; protects both sides if CS is asserted before slave is ready |
| SPI MISO    | 33 Ω at display board | none | MISO is driven only when CS is asserted |

Minimum pull-down value guideline: **≥ 10 kΩ**.  Do not use pull-ups on GPIO0
or GPIO1; both lines should idle LOW so that an unpowered board looks like
"idle" to the powered one.

---

## Safe flashing procedure

1. Disconnect the IDC ribbon (or ensure the host board is unpowered / outputs
   are high-Z).
2. Connect USB-C to the display DevKitM.
3. Flash normally (`pio run -e display_board -t upload`).
4. The DevKitM BOOT button connects to GPIO9.  Pressing BOOT while clicking
   EN/RESET puts the chip into download mode as designed.  This is unaffected
   by GPIO0/GPIO1 because those pins carry no strapping function.
5. After flashing, reconnect the ribbon and power up both boards.

If flashing ever fails with the ribbon connected, verify that:

- The host board's GPIO0-driving output is configured as an **input (high-Z)**
  during its own boot sequence (before it switches to output mode).
- No strong pull-up holds GPIO0 HIGH — that is fine for GPIO0 (no strapping
  risk), but confirms the line is in a known state.

---

## Boot sequence implemented in firmware

```
Reset → GPIO1 (DISP_READY) driven LOW immediately
      → GPIO0 (HOST_REQ) set as INPUT_PULLDOWN
      → Initialise local TFT / touch / SD
      → Initialise SPI slave on GPIO4-7
      → GPIO1 driven HIGH  ← host may now start transactions
```

The `INPUT_PULLDOWN` on GPIO0 means the pin reads LOW (= no request) if the
host board is unpowered or its output is high-Z.  The 10 kΩ external
pull-down provides the same guarantee even during the brief window before
`pinMode()` is called.
