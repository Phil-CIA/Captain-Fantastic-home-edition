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
- backups/
- Battery charger/
- New folder/
- RTOS main_firmware restore/

Keep on main branch:
- Prefer archive branch or archive/ folder after review.

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
