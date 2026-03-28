# ESP32-C6 GPIO Safety During Flashing — Display Board Bring-Up Notes

This document covers **every constraint you must know before wiring or flashing**
the ESP32-C6 display board.  Read it end-to-end the first time; use the
quick-reference table at the bottom for subsequent sessions.

---

## 1. The Display Board Host-Link GPIO Map

| Signal        | GPIO | IDC-10 pin | Direction       | Role                                   |
|---------------|------|------------|-----------------|----------------------------------------|
| SPI SCK       | 4    | 2          | Host → Display  | SPI clock (master-driven)              |
| SPI MOSI      | 5    | 4          | Host → Display  | Data from host to display              |
| SPI MISO      | 6    | 6          | Display → Host  | Data from display to host              |
| SPI CS        | 7    | 8          | Host → Display  | Chip select (active-LOW)               |
| HOST\_REQ     | **0**| 5          | Host → Display  | Host signals it wants a transaction    |
| DISP\_READY   | **1**| 9          | Display → Host  | Display signals its SPI buffer is ready|

> All pin constants live in `display-firmware/include/host_link_config.h`.
> Change them there; do not scatter magic numbers through source files.

---

## 2. ESP32-C6 Strapping Pins — What They Are and Why They Matter

"Strapping pins" are GPIOs that the ROM boot-loader samples **during the
first microseconds of every power-on or reset**.  If the wrong level is read,
the chip boots into the wrong mode and appears completely dead or
unresponsive to normal firmware upload.

### ESP32-C6 Strapping Pin Summary

| GPIO | Sampled at reset | HIGH (default)        | LOW                              |
|------|------------------|-----------------------|----------------------------------|
| **0**| Yes              | SPI-flash boot (normal) | ROM serial download mode (flash)|
| 8    | Yes              | Normal ROM log enabled  | ROM log output disabled         |
| 9    | Yes              | VDD\_SPI = 3.3 V        | VDD\_SPI = 1.8 V (flash voltage)|

GPIO0 is the only one directly relevant to the display board.

---

## 3. GPIO0 — Boot-Strap Wiring (HOST\_REQ line)

### Why GPIO0 was chosen

The user has GPIO0 wired to IDC-10 pin 5 as the **HOST\_REQ** handshake line
(host → display, active-HIGH).  This is feasible, but requires one mandatory
hardware measure.

### The constraint

At power-on and at every reset, GPIO0 **must read HIGH** for the ESP32-C6 to
boot normally from SPI flash.  If GPIO0 is LOW during those first milliseconds,
the chip enters ROM download mode (the flashing bootloader).

### Required hardware fix — 10 kΩ pull-up on the display PCB

Fit a **10 kΩ resistor between GPIO0 and 3.3 V** on the display board PCB
(or breakout board).  Most ESP32-C6 devkits include this pull-up already;
check your schematic.

```
3.3 V ──┬── 10 kΩ ──┬── GPIO0 (ESP32-C6)
         │           │
         │           └── IDC-10 pin 5 (HOST_REQ from host)
         │
        GND
```

With this pull-up installed:
* GPIO0 idles HIGH on power-on → normal boot ✓
* Host drives GPIO0 HIGH to signal a request → still HIGH ✓
* Host releases GPIO0 (tri-state / open-drain low) → 10 kΩ pulls it HIGH ✓
* Host must **never** hard-drive GPIO0 LOW while the display is being powered
  on or reset.

### Host-side firmware requirement

The `captain_control` firmware **must not** assert HOST\_REQ (drive GPIO0 LOW
or HIGH) until the display board has finished booting.  The safest rule:

> The host should not assert HOST\_REQ until it receives at least one valid
> DISP\_READY assertion from the display, or until 3 seconds have elapsed
> after the host's own boot.

### Alternate pins if GPIO0 causes problems

If the 10 kΩ pull-up fix is inconvenient (e.g., on a bare devkit with no
PCB footprint), reassign HOST\_REQ to one of these safe pins:

| GPIO | Safe? | Notes                              |
|------|-------|------------------------------------|
| 2    | ✅ Yes | No strapping role, no peripheral   |
| 3    | ✅ Yes | No strapping role, no peripheral   |
| 10   | ✅ Yes | No strapping role, no peripheral   |
| 11   | ✅ Yes | No strapping role, no peripheral   |
| 8    | ⚠️ No  | Strapping pin (same issue as GPIO0)|
| 9    | ⚠️ No  | Strapping pin (flash voltage)      |
| 12   | ⚠️ No  | USB D- on USB-JTAG devkits         |
| 13   | ⚠️ No  | USB D+ on USB-JTAG devkits         |

To reassign: update `HOST_REQ_PIN` in `host_link_config.h` and re-route
the IDC-10 ribbon wire to the new GPIO.

---

## 4. GPIO1 — DISP\_READY Line

GPIO1 is **not a strapping pin** on the ESP32-C6.  It has no special
boot-time requirement.

**Potential conflict — UART0 TX on some breakout boards:**
Some ESP32-C6 development boards route GPIO1 to the onboard USB-Serial bridge
as UART0 TX (debug output).  If `Serial.print()` output is needed during
bring-up while GPIO1 is also used as DISP\_READY, you have two options:

1. **Preferred**: Use the built-in USB-JTAG CDC port (GPIO12/13) for
   `Serial` output instead.  On ESP32-C6 devkits with `ARDUINO_USB_CDC_ON_BOOT=1`
   (set in `platformio.ini`), `Serial` goes over USB-CDC, freeing GPIO1.

2. **Alternative**: Reassign DISP\_READY to GPIO2 or GPIO3 and update
   `DISPLAY_READY_PIN` in `host_link_config.h`.

---

## 5. GPIO4–7 — SPI Slave Lines

These GPIOs have **no strapping roles** and no default peripheral assignments
that conflict with the host-link SPI slave.

The ESP32-C6 SPI2 (GPSPI2) peripheral can be routed to any GPIO via the GPIO
matrix; GPIO4–7 are a clean choice for a 4-wire SPI slave bus.

**No special wiring is required for GPIO4–7 beyond the SPI signal lines.**

---

## 6. USB-JTAG vs. UART Flashing

### Method A — USB-JTAG/CDC (recommended, default in platformio.ini)

Most ESP32-C6 devkits expose a USB-JTAG/Serial peripheral through the SoC's
built-in USB controller.

* Uses GPIO12 (USB D-) and GPIO13 (USB D+) — **not** GPIO0–7.
* Does **not** require GPIO0 to be held LOW; esptool resets the chip
  automatically via a USB control transfer.
* Safe to use with the IDC-10 ribbon connected (GPIO0–7 are not touched by
  the USB flash path).
* Set `upload_protocol = esptool` (the default in `[env:display_board]`).

### Method B — UART via external USB-Serial adapter

If your board does not have the USB-JTAG feature (or it is broken), use a
CH340/CP2102 adapter on UART0:

* UART0 TX = GPIO16 (connect to adapter RX)
* UART0 RX = GPIO17 (connect to adapter TX)
* **GPIO0 must read LOW when the board is reset to enter download mode.**
  Either hold the BOOT button while resetting, or wire a momentary switch
  from GPIO0 to GND (this temporarily overrides the 10 kΩ pull-up).
* **Disconnect the IDC-10 ribbon** before flashing via UART.  The host ESP32
  is still powered and could accidentally drive GPIO0 at an inconvenient time.
* Use the `display_board_uart` environment in `platformio.ini`.

---

## 7. Bring-Up Checklist (First Power-On)

Follow this sequence the very first time you power the board with the host-link
wired up.

- [ ] **Step 1**: Confirm 10 kΩ pull-up between GPIO0 and 3.3 V on the display
  board PCB (measure with a multimeter at power-off).

- [ ] **Step 2**: Disconnect the IDC-10 ribbon.  Flash the display board via
  USB-JTAG using the `display_board` PlatformIO environment.

- [ ] **Step 3**: Open the serial monitor.  You should see:
  ```
  [display] setup start — ESP32-C6 display board
  [display] Host-link SPI: SCK=GPIO4 MOSI=GPIO5 MISO=GPIO6 CS=GPIO7
  [display] Handshake: DISP_READY=GPIO1  HOST_REQ=GPIO0
  [host_link] SPI slave ready. SCK=4 MOSI=5 MISO=6 CS=7
  [host_link] Handshake: DISP_READY=GPIO1 HOST_REQ=GPIO0
  [display] setup complete
  ```

- [ ] **Step 4**: Verify the TFT shows "Display FW ready / Waiting for host…"
  and the backlight is on.

- [ ] **Step 5**: Connect the IDC-10 ribbon with the **host board powered off**.
  Power on the host board.  Confirm the display board still boots normally
  (it should, because the 10 kΩ pull-up holds GPIO0 HIGH).

- [ ] **Step 6**: Flash the host board (`captain_control`) with HOST\_REQ
  support enabled.  Watch the serial monitors on both boards for:
  * Host: `[host_link_master] Sending STATE to display…`
  * Display: `[host_link] rx=1 tx=1 rxErr=0 …`

- [ ] **Step 7**: After 10 seconds, the display should print a stats line:
  ```
  [host_link] rx=N tx=N rxErr=0 txDrop=0 hostReq=N
  ```
  Non-zero `rxErr` or growing `txDrop` indicate wiring or timing problems.

---

## 8. Quick Reference Table

| GPIO | Used as           | Strapping? | Safe during USB-JTAG flash? | Notes                                    |
|------|-------------------|------------|-----------------------------|------------------------------------------|
| 0    | HOST\_REQ (in)    | **YES**    | Yes (USB doesn't touch it)  | Needs 10 kΩ pull-up to 3.3 V on PCB    |
| 1    | DISP\_READY (out) | No         | Yes                         | May conflict with UART0 TX on some boards|
| 4    | SPI SCK (slave)   | No         | Yes                         | No constraints                           |
| 5    | SPI MOSI (slave)  | No         | Yes                         | No constraints                           |
| 6    | SPI MISO (slave)  | No         | Yes                         | No constraints                           |
| 7    | SPI CS (slave)    | No         | Yes                         | No constraints                           |
| 8    | —                 | YES        | Yes                         | Do not use for handshake                 |
| 9    | —                 | YES        | Yes                         | Do not use for handshake                 |
| 10   | TFT CS            | No         | Yes                         | Local peripheral; no flash-path conflict |
| 11   | TFT DC            | No         | Yes                         | Local peripheral; no flash-path conflict |
| 12   | USB D-            | No         | USB flash path               | Do not use if board has USB-JTAG        |
| 13   | USB D+            | No         | USB flash path               | Do not use if board has USB-JTAG        |
| 14   | TFT RST           | No         | Yes                         | Local peripheral; no flash-path conflict |
| 15   | Touch CS          | No         | Yes                         | Local peripheral; no flash-path conflict |
| 16   | UART0 TX          | No         | UART flash path              | **Not used by display firmware** — kept free for UART flashing |
| 17   | UART0 RX          | No         | UART flash path              | **Not used by display firmware** — kept free for UART flashing |
| 18   | Backlight PWM     | No         | Yes                         | Local peripheral; no flash-path conflict |
| 19   | Local SPI MOSI    | No         | Yes                         | TFT / touch / SD bus master              |
| 20   | Local SPI MISO    | No         | Yes                         | TFT / touch / SD bus master              |
| 21   | Local SPI SCK     | No         | Yes                         | TFT / touch / SD bus master              |
| 22   | Touch IRQ         | No         | Yes                         | Local peripheral; no flash-path conflict |
| 23   | SD CS             | No         | Yes                         | Local peripheral; no flash-path conflict |

---

## 9. Recommended Pull-Up / Pull-Down Summary

| GPIO | Direction | Recommended external resistor | Reason                                  |
|------|-----------|-------------------------------|-----------------------------------------|
| 0    | Input     | 10 kΩ to 3.3 V (pull-up)     | Ensure HIGH at power-on for normal boot |
| 1    | Output    | None required                 | Driven by ESP32-C6 firmware             |
| 4    | Input (SCK)| None required                | SPI clock line driven by master         |
| 5    | Input (MOSI)| None required               | SPI data driven by master               |
| 6    | Output (MISO)| None required              | Driven by ESP32-C6 (Hi-Z when CS high) |
| 7    | Input (CS) | 10 kΩ to 3.3 V (pull-up)    | Prevent spurious CS assertion if host is tristated |

---

*Last updated: 2026-03-28*  
*Applies to: ESP32-C6 (all revisions) + captain\_control ↔ display-board IDC-10 host link*
