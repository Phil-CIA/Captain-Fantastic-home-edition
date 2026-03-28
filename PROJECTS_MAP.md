# Projects Map

This repository currently contains multiple efforts. This file defines cleanup boundaries so reorganization can be done safely in small PRs.

## Project A: Legacy Captain Firmware (active root project)
Purpose:
- Original ESP32 firmware and runtime assets.

Primary locations:
- src/
- include/
- data/
- docs/
- platformio.ini (root, if present)
- .vscode/ (root tasks/settings for legacy build/upload)

Keep on main branch:
- Yes

## Project B: Captain-v2 Split-MPU Firmware
Purpose:
- New architecture with separate control and matrix firmware.

Primary locations:
- Captain-v2/

Keep on main branch:
- Yes

## Project C: Hardware Design and Manufacturing Files
Purpose:
- Electrical and board design artifacts, exports, and fabrication outputs.

Primary locations:
- hardware/
- hardware/kicad/esp32vroom/
- hardware/kicad/libs/
- hardware/gerbers/root-export-esp32vroom/
- hardware/gerbers/legacy-gerber-files/
- hardware/backups/esp32vroom-backups/

Keep on main branch:
- Yes, grouped under hardware/ with stable subfolders.

## Project D: Archives, Experiments, and Recovery Copies
Purpose:
- Historical backups, temporary work, and restore snapshots.

Primary locations:
- archive/backups/
- archive/battery-charger/
- archive/rtos-main-firmware-restore/

Keep on main branch:
- Yes, now grouped under archive/.

## Project E: Display Board Firmware (ESP32-C6)
Purpose:
- Firmware for the ESP32-C6 display board ("C6 Mini" Amazon clone).
- Drives ST7796S TFT, XPT2046 touch, and SD card.
- Acts as SPI slave to the host ESP32-C6 over a 10-pin IDC ribbon.
- Handshake via GPIO0 (HOST_REQ) and GPIO1 (DISP_READY).

Primary locations:
- display-firmware/
- display-firmware/include/host_link_config.h   (SPI + handshake pin defs)
- display-firmware/include/display_local_config.h (TFT/touch/SD pin defs)
- display-firmware/FLASHING_NOTES.md            (GPIO0 boot-safety rules)
- docs/ESP32C6_DEVKITM_AMAZON_PINOUT.md         (board pinout reference)

Keep on main branch:
- Yes

## Cleanup Rules
1. Use one PR per project area.
2. No firmware logic changes mixed with file moves.
3. Keep build-generated artifacts ignored and out of commits.
4. Update README links after each move.
5. Tag PR title with area: [legacy], [v2], [hardware], [archive].

## Suggested PR Order
1. Add map and issue labels (this PR).
2. Normalize archive placement.
3. Normalize hardware folder structure.
4. Final README/index updates.
