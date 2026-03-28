# Display Firmware — ESP32-C6 Display Board

This folder contains the firmware for the **display board** in the Captain Fantastic Home Edition project.

## Hardware

- **MCU:** ESP32-C6 "C6 mini" (Amazon clone)
- **Display:** TFT ST7796S over SPI master
- **Touch:** XPT2046 over SPI master (shared bus)
- **Storage:** SD card over SPI master (shared bus)
- **Host link:** SPI slave to host ESP32-C6 via IDC-10 ribbon cable
- **Handshake:** Two-wire RTS/CTS-style flow control (GPIO0, GPIO1)

## Pin configuration files

| File | Purpose |
|---|---|
| `include/host_link_config.h` | SPI slave pins (GPIO4–7) and handshake pins (GPIO0, GPIO1) with full safety notes |
| `include/display_local_config.h` | TFT, touch, SD card, UART0 pin assignments |

Read the safety notes in `host_link_config.h` before wiring or testing.  
Specifically: **GPIO8 is the boot-mode strapping pin on ESP32-C6** — keep it free.

## Flashing

See **[FLASHING_NOTES.md](FLASHING_NOTES.md)** for:
- Recommended USB-C (USB-JTAG) flashing method
- Alternate external USB-UART method (GPIO16/GPIO17)
- Bring-up checklist
- Troubleshooting guide

## Quick start

1. Install [PlatformIO](https://platformio.org/).
2. Open this folder (`display-firmware/`) as a PlatformIO project.
3. Edit `platformio.ini` to set the correct COM port for your OS.
4. Build and upload with the `display_board_usb` environment.

## Architecture notes

The display MCU acts as **SPI slave** toward the host and **SPI master** toward  
local peripherals (TFT, touch, SD). Both roles share the same SPI2 peripheral  
and the same GPIO4/6/7 signal lines; only the CS lines differ.

The host must not initiate an SPI transaction while the display is busy with a  
local peripheral. The **DISP_READY** line (GPIO1) is LOW during local peripheral  
access and goes HIGH only when the display has prepared a response and is ready  
for the next host transaction.

See `include/host_link_config.h` for the full handshake protocol description.
