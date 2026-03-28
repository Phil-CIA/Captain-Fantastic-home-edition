# Display Board — Flashing Notes and Bring-Up Guide

Target board: **ESP32-C6 "C6 mini" (Amazon clone)**  
Framework: Arduino / PlatformIO  
Firmware location: `display-firmware/`

---

## Quick reference: what changed from original ESP32

| Concern | Original ESP32 | ESP32-C6 |
|---|---|---|
| Boot-mode strapping pin | **GPIO0** (LOW = download) | **GPIO8** (LOW = download) |
| UART0 default TX/RX | GPIO1 / GPIO3 | GPIO16 / GPIO17 |
| SPI slave support | SPI2/SPI3 | SPI2 |
| USB flashing method | External USB-UART chip | Built-in USB-JTAG (USB-C) |

**GPIO0 and GPIO1 are NOT strapping pins on the ESP32-C6.**  
If you are familiar with the original ESP32, do not assume the same rules apply here.

---

## Pin assignments summary

| GPIO | Direction | Signal | Notes |
|---|---|---|---|
| 0 | Input | HOST_REQ | Handshake: host→display. 10 kΩ pull-up, idle HIGH |
| 1 | Output | DISP_READY | Handshake: display→host. Push-pull, idle LOW |
| 4 | Input (slave) / Output (master) | SPI2 MISO | Host-link MISO or local-peripheral MISO |
| 5 | Input | SPI2 CS | Host-link chip select, active LOW |
| 6 | Input (slave) / Output (master) | SPI2 MOSI | Host-link MOSI or local-peripheral MOSI |
| 7 | Input | SPI2 CLK | Host-link clock |
| 8 | — | *(strapping)* | **Boot mode pin — do not drive externally** |
| 9 | — | *(strapping / BOOT button)* | ROM print / BOOT button on most C6 mini boards |
| 10 | Output | TFT CS | ST7796S chip select |
| 11 | Output | TFT DC | ST7796S data/command |
| 12 | Output | TFT RST | ST7796S reset |
| 13 | Output | TFT BL | Backlight PWM |
| 14 | Output | TOUCH CS | XPT2046 chip select |
| 15 | Input | TOUCH IRQ | XPT2046 interrupt, active LOW |
| 16 | Output | UART0 TX | **Reserved — flash / serial debug** |
| 17 | Input | UART0 RX | **Reserved — flash / serial debug** |
| 20 | Output | SD CS | SD card chip select |

---

## Preferred flashing method: USB-C (built-in USB-JTAG)

The ESP32-C6 has a built-in USB-JTAG/Serial controller connected to its internal  
USB PHY. The C6 mini exposes this through the USB-C connector.

**This is the recommended flashing method** because:
- GPIO4–7, GPIO0, GPIO1 are not involved in USB-JTAG flashing.
- No external USB-UART adapter is needed.
- Automatic reset and download mode entry works without touching the BOOT button.

### PlatformIO USB-C upload settings

```ini
[env:display_board_usb]
platform = espressif32
board = esp32-c6-devkitc-1
framework = arduino
upload_protocol = esptool
upload_speed = 921600
; COM port assigned to the USB-JTAG interface (adjust for your OS)
upload_port = COM3        ; Windows example
; upload_port = /dev/ttyACM0   ; Linux example
; upload_port = /dev/cu.usbmodem*  ; macOS example
monitor_port = ${this.upload_port}
monitor_speed = 115200
```

> If `esp32-c6-devkitc-1` is not recognized, try `esp32c6` or install the  
> latest Espressif Arduino core: `platformio pkg update`.

---

## Alternate flashing method: external USB-UART adapter

Use only if the USB-C port is unavailable (e.g., broken connector, custom board).

UART0 defaults on ESP32-C6:

| Signal | GPIO |
|---|---|
| TXD0 | 16 |
| RXD0 | 17 |

Connect the adapter:
- Adapter TX → GPIO17 (display board RX)
- Adapter RX → GPIO16 (display board TX)
- Adapter GND → GND

To enter download mode manually:
1. Hold the **BOOT** button (GPIO9 on most C6 mini boards).
2. Press and release **RESET**.
3. Release **BOOT**.
4. Flash with `esptool` or PlatformIO.

**Do not use GPIO0 or GPIO1 pins with the adapter.** They are not UART0 on the C6.

---

## Before flashing: disconnect or high-Z the host board

The host (SPI master) drives MOSI (GPIO6) and CLK (GPIO7) and controls CS (GPIO5).  
If the host is active during the display board's reset/boot window:

- A LOW CS (GPIO5) during reset will cause the SPI slave to start capturing bits  
  as soon as the boot completes, before buffers are set up. This results in a  
  garbled first transaction.
- HOST_REQ (GPIO0) is safe to leave driven by the host since it is not a strapping  
  pin, but keep it HIGH (idle / no request) during display reset.

**Recommended procedure:**
1. Power the host board first and let it finish its boot sequence.
2. Configure all host SPI outputs as inputs (or hold CS HIGH, MOSI/CLK idle LOW).
3. Flash the display board via USB-C.
4. Reset both boards together after flashing.

If single-power-supply: set the host firmware to hold CS HIGH and keep  
MOSI/CLK LOW (SPI idle state) for the first 500 ms after reset before  
attempting any SPI transaction. This gives the display firmware time to init.

---

## Bring-up checklist

- [ ] Verify GPIO8 has no external connection that can pull it LOW.
- [ ] Install 10 kΩ pull-up on GPIO0 (HOST_REQ) to 3.3 V.
- [ ] Install 220 Ω series resistor on GPIO1 (DISP_READY) between board and IDC.
- [ ] Install 100 Ω series resistor on GPIO4 (MISO) between board and IDC.
- [ ] Verify host CS line is HIGH before powering display board.
- [ ] Flash via USB-C first (no external drivers interfering).
- [ ] Open serial monitor at 115200 baud and confirm startup messages on GPIO16.
- [ ] Verify backlight lights up on TFT (GPIO13 HIGH).
- [ ] Verify DISP_READY (GPIO1) goes HIGH after boot completes.

---

## Flashing troubleshooting

### Board does not enter download mode automatically

- The C6 mini relies on USB-JTAG for automatic reset. If the board does not  
  reset automatically during `esptool` connect, try:
  1. Hold BOOT (GPIO9), press RESET, release BOOT, then run flash command.
  2. Some cheaper Amazon clones omit the auto-reset circuitry. You will always  
     need to manually trigger the sequence above.

### "Failed to connect" / timeout during flash

1. Check the COM port is correct (Device Manager on Windows / `ls /dev/tty*` on Linux).
2. Reduce upload speed to 460800 or 115200 in `platformio.ini`.
3. Try a different USB cable (some cables are charge-only, no data lines).
4. Disconnect the IDC ribbon cable from the display board before flashing.  
   External devices driving GPIO4–7 or GPIO0/1 during boot can occasionally  
   produce enough noise to confuse the USB-JTAG stack on marginal boards.

### Board boots only when RESET is held or USB cable disconnected

This is a symptom of GPIO8 being held LOW by external circuitry.  
- Inspect connections to GPIO8; remove any accidental pull-down.  
- If using a multimeter, check GPIO8 pin voltage at power-on: must be > 2 V  
  (with internal pull-up active) for normal SPI boot.

### DISP_READY line stays LOW after boot

- Confirm `DISPLAY_READY_PIN` is configured as `OUTPUT` in firmware setup.
- Check for short between GPIO1 and GND (measure resistance with board powered off).
- Verify no conflicting peripheral on GPIO1 on your specific board variant.

### Garbage on serial monitor during normal operation

- GPIO16 (UART0 TX) may be affected by capacitive loading if routed near  
  high-speed SPI lines. Keep GPIO16/17 traces away from the IDC connector  
  or add a 100 Ω series resistor on the TX line.
- Also check that the host is not clocking SPI at a moment the display firmware  
  is writing to Serial — consider disabling `Serial.print` in hot paths.

### SPI transactions succeed but data is wrong / offset by a byte

- CS (GPIO5) was LOW before the slave firmware had queued a TX buffer.  
  Increase the host delay after display RESET before asserting CS.
  See the "Before flashing" section above for timing guidance.
- Ensure SPI MODE is 0 (CPOL=0, CPHA=0) on both sides. Mismatched MODE  
  causes a one-bit or one-byte shift in received data.

---

## Series resistors and pull resistors — quick reference

| Signal | Resistor | Value | Reason |
|---|---|---|---|
| GPIO0 HOST_REQ | Pull-up to 3.3 V | 10 kΩ | Idle HIGH when host pin is high-Z |
| GPIO0 HOST_REQ (host side) | Series | 220 Ω | Limits drive current if line is contested |
| GPIO1 DISP_READY | Series | 220 Ω | Limits current, suppresses ringing |
| GPIO4 MISO | Series | 100 Ω | Protects against simultaneous drive |
| GPIO5 CS | Pull-up to 3.3 V (host side) | 10 kΩ | Keeps CS HIGH when host is high-Z |

---

*Last updated: 2026-03 — ESP32-C6 C6 mini, PlatformIO Arduino framework.*
