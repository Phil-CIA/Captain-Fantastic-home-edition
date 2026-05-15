# Repository Index

## GitHub Repositories
1. Captain Fantastic Home Edition
   - https://github.com/Phil-CIA/Captain-Fantastic-home-edition
2. Development Station Power Supply
   - https://github.com/Phil-CIA/Development-Station-Power-Supply

## Captain Repository Layout
1. Legacy firmware
   - src/
   - include/
   - data/
   - docs/
   - docs/SYSTEM_BEHAVIOR_CONTRACT.md — working definition doc for state machine, scoring, audio, diagnostics, and pending design decisions
2. Captain-v2 split-MPU work
   - Captain-v2/
   - Captain-v2-control/
   - Captain-v2-matrix/
3. Hardware assets (Captain v2 split-MPU)
   - hardware/kicad/captain_control/ — Main control board schematic and PCB
   - hardware/kicad/captain_matrix/ — Switch/lamp matrix board schematic and PCB
   - hardware/kicad/7seg_display/ — Standalone 7-segment display board
   - hardware/kicad/legacy/ — Hat board (old single-board design, superseded)
   - hardware/kicad/esp32vroom/ — Original ESP32Vroom project (historical)
   - hardware/kicad/libs/ — Shared KiCad symbols and footprints
   - hardware/kicad/SOURCE_OF_TRUTH.md — Canonical current KiCad project map and archive policy
   - hardware/kicad/CURRENT_PROJECTS.yaml — Machine-readable current/historical KiCad path map for agents
   - hardware/gerbers/root-export-esp32vroom/ — Legacy gerber exports
   - hardware/gerbers/legacy-gerber-files/ — Older gerber exports
   - hardware/backups/esp32vroom-backups/ — Historical hardware backups
   - Open-KiCad.ps1 — Repo-safe KiCad launcher (manager/control/matrix/hat/7seg)
   - Reset-KiCad-Recents-ToRepo.ps1 — One-time KiCad recent/open-project reset to repo root
4. Archive material
   - archive/backups/
   - archive/battery-charger/
   - archive/rtos-main-firmware-restore/

## Current Firmware/Repo Status (updated 2026-05-14)
1. Matrix/control firmware link migrated to register-style I2C model (HT16K33-like operation)
2. Active matrix board source of truth is the dedicated project:
   - `Captain-v2-matrix/`
   - active runtime source: `Captain-v2-matrix/src/matrix_app_main.cpp`
3. Matrix board address: `0x24` on SDA=21/SCL=22
4. May 2026 bring-up fixes now verified:
   - COM5 no longer reads lamp bytes as switch bytes
   - matrix TX FIFO is preloaded with switch data after lamp writes
   - repeated control-side burst-filter spam fixed by updating baseline state on suppressed burst frames
   - quick live mapping spot-check confirmed `S20 Outhole` and `S9 Target 2`
5. Current normal control state after bring-up validation:
   - `MATRIX_SWITCH_MAPPING_MODE = false`
   - `MATRIX_SWITCH_SOLENOIDS_ENABLED = true`
   - `START_BUTTON_SOLENOID_TEST_ENABLED = false`
6. Matrix register windows implemented:
   - Lamp RAM `0x00..0x07`
   - Switch bytes `0x40..0x43`
   - Diagnostics `0xF0..0xF3`
7. Matrix standalone test support segmented into dedicated module:
   - `Captain-v2/include/matrix_test_support.h`
   - `Captain-v2/src/matrix_test_support.cpp`
8. Separate board-focused PlatformIO projects added:
   - `Captain-v2-control/`
   - `Captain-v2-matrix/`
9. Matrix board datasheet-style firmware contract added:
   - `Captain-v2-matrix/README.md`

## Local-Only Files That Should Stay Out Of GitHub
1. .pio/ build output
2. VS Code machine-specific files like c_cpp_properties.json and launch.json
3. LTspice generated outputs in Captain-v2/hw_sim/ (`*.raw`, `*.db`, `*.log`)
