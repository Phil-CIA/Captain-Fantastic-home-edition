This folder contains hardware design files, schematics, PCB layouts, and fabrication exports for Captain Fantastic v2.

## Current Active Designs (v2 split-MPU architecture)

Structure:
- kicad/captain_control/: Main control board (ESP32 DevKit) — gameplay, audio, solenoids, headbox lamps, OTA
- kicad/captain_matrix/: Switch/lamp matrix board (ESP32 DevKit) — playfield matrix scanning, I2C slave
- kicad/7seg_display/: Standalone 7-segment display board (I2C HT16K33, 6-digit)

## Project support files
- kicad/libs/: project symbol/footprint libraries and KiCad tables

## Historical / Legacy
- kicad/legacy/: Hat board (superseded single-board v1 design — trace size and SR enable fixed in v2 split)
- kicad/esp32vroom/: Original ESP32Vroom KiCad project files (historical reference)

## Fabrication archives
- gerbers/root-export-esp32vroom/: root-level export set moved from repo root
- gerbers/legacy-gerber-files/: older export set moved from "Gerber files"
- backups/esp32vroom-backups/: historical zipped hardware backups