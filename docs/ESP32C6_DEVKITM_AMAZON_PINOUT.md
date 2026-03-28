# ESP32-C6 DevKitM Amazon Clone — Pinout Reference

## Board overview

This board is sold under various names on Amazon and AliExpress:
- "ESP32-C6 Mini"
- "ESP32-C6 SuperMini"
- "ESP32-C6 DevKitM-1 clone"

It is **not** the official Espressif ESP32-C6-DevKitM-1 board, but it uses the same ESP32-C6 module (WROOM-1 or similar) and exposes the same GPIO numbers.  The USB-C connector connects directly to the ESP32-C6 built-in USB-Serial/JTAG peripheral.

---

## GPIO usage in this project (display board)

| GPIO | Function | Direction | Notes |
|---|---|---|---|
| 0 | HOST_REQ handshake | Input | ⚠ Strapping pin — needs 10 kΩ pull-up; 470 Ω series on ribbon |
| 1 | DISP_READY handshake | Output | Drive HIGH when display is ready for SPI |
| 4 | Host-link SCLK | Input | SPI slave clock from host |
| 5 | Host-link MOSI | Input | SPI slave data in |
| 6 | Host-link MISO | Output | SPI slave data out |
| 7 | Host-link CS | Input | SPI slave chip-select (active LOW) |
| 10 | Local SPI SCLK | Output | Shared by TFT / touch / SD |
| 11 | Local SPI MOSI | Output | Shared by TFT / touch / SD |
| 12 | USB D− | — | Built-in USB-JTAG — do not use |
| 13 | USB D+ | — | Built-in USB-JTAG — do not use |
| 14 | Local SPI MISO | Input | From touch / SD |
| 15 | TFT CS | Output | ST7796S chip select (active LOW) |
| 16 | UART0 TXD | — | Reserved for serial log / recovery flash |
| 17 | UART0 RXD | — | Reserved for serial log / recovery flash |
| 18 | Touch IRQ | Input | XPT2046 interrupt (active LOW) |
| 19 | SD CS | Output | SD card chip select (active LOW) |
| 20 | TFT DC | Output | ST7796S data/command select |
| 21 | TFT RST | Output | ST7796S reset (active LOW) |
| 22 | TFT BL | Output | Backlight PWM (active HIGH) |
| 23 | Touch CS | Output | XPT2046 chip select (active LOW) |

---

## IDC-10 connector pin map (host ↔ display ribbon)

```
IDC-10 (2×5, 2.54 mm pitch)

 Pin 1  ── GND
 Pin 2  ── 3V3
 Pin 3  ── GPIO4  (SCLK)
 Pin 4  ── GPIO5  (MOSI)
 Pin 5  ── GPIO0  (HOST_REQ / RTS)   ← strapping pin; 10kΩ pull-up + 470Ω series
 Pin 6  ── GPIO7  (CS)
 Pin 7  ── NC
 Pin 8  ── NC
 Pin 9  ── GPIO1  (DISP_READY / TP19) ← 470Ω series
 Pin 10 ── GPIO6  (MISO)
```

> **Note:** The schematic labels IDC pin 5 as "RTS/CTS" (mapped to GPIO0) and IDC pin 9 as "GPIO1/TP19" (mapped to GPIO1).  In firmware these signals are named `HOST_REQ` (GPIO0) and `DISP_READY` (GPIO1) respectively.  Verify pin numbering against your board revision before assembly.

---

## Handshake protocol summary

```
Host                          Display (SPI slave)
────                          ───────────────────
                              Boot completes → DISP_READY = HIGH
HOST_REQ = HIGH ──────────→  (interrupt / polling)
                              Prepare TX buffer
                              DISP_READY = LOW (busy)
CS = LOW (SPI frame) ──────→  Receive/send frame
CS = HIGH ─────────────────→  
HOST_REQ = LOW ────────────→  
                              Process received data
                              DISP_READY = HIGH  ←── ready for next frame
```

Both signals are **active HIGH**.  The host must not assert CS until DISP_READY is HIGH.

---

## Boot / strapping pin behaviour

The ESP32-C6 ROM checks GPIO0 at reset:

| GPIO0 at reset | Behaviour |
|---|---|
| HIGH (≥ 0.75 × VDD) | Normal boot — application runs |
| LOW (≤ 0.25 × VDD) | Download mode — application does **not** run |

The on-board BOOT button grounds GPIO0 temporarily.  External circuitry on IDC pin 5 must never pull GPIO0 LOW during a reset event.  The 10 kΩ pull-up and 470 Ω series resistor described in `FLASHING_NOTES.md` ensure this condition is met even when the host board is unpowered.

---

## TFT_eSPI User_Setup.h excerpt

Add the following to `User_Setup.h` (or your PlatformIO `build_flags`) when using TFT_eSPI with this display board:

```cpp
#define USER_SETUP_INFO "ESP32-C6 display board"

// ST7796S
#define ST7796_DRIVER
#define TFT_WIDTH  320
#define TFT_HEIGHT 480

// Local SPI bus
#define TFT_MOSI 11
#define TFT_SCLK 10
#define TFT_MISO 14
#define TFT_CS   15
#define TFT_DC   20
#define TFT_RST  21
#define TFT_BL   22

// XPT2046 touch on same SPI bus
#define TOUCH_CS 23

#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY   2500000
#define SPI_TOUCH_FREQUENCY  2500000
```

---

## References

- `display-firmware/include/host_link_config.h` — C++ pin constants for host-link SPI + handshake
- `display-firmware/include/display_local_config.h` — C++ pin constants for TFT / touch / SD
- `display-firmware/FLASHING_NOTES.md` — detailed GPIO0 safety rules and flash procedure
