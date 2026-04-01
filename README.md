# Captain Fantastic – Home Edition

Firmware and hardware files for a restored Captain Fantastic pinball machine running on ESP32.

## Repository Layout

| Path | Description |
|------|-------------|
| `src/` `include/` `data/` | **Legacy firmware** – working single-board ESP32 firmware with External Flash OTA |
| `Captain-v2/` | **Captain v2 firmware** – clean split-MPU architecture (control + matrix boards) |
| `hardware/` | KiCad schematics, gerbers, and board-design backups |
| `docs/` | Project documentation, setup guides, and handoff notes |
| `archive/` | Historical backups, experiments, and recovery copies |
| `music/` | Test audio files used with the music streaming server |

## PlatformIO Projects

This repo contains **multiple PlatformIO projects**:

### 1. Legacy Firmware (root)
- `platformio.ini` at the repository root
- Active environment: `combined_rtos`
- Targets a single ESP32 running gameplay, displays, sound, and OTA

### 2. Captain v2 shared source tree
- `Captain-v2/platformio.ini`
- Shared build definitions and source for split-MPU firmware
- Includes environments for control, matrix, and display-link variants
- Active bring-up environment: `captain_matrix_c6_idf` (ESP32-C6 matrix board on COM8)
- Separates control logic and switch/lamp matrix scanning across two ESP32 boards

### 3. Captain v2 board-specific projects
- `Captain-v2-control/platformio.ini` (control board focused project)
- `Captain-v2-matrix/platformio.ini` (matrix board focused project)
- Both reference shared source under `Captain-v2/` and keep board workflows isolated

> **VS Code tip:** Open the specific project folder you are actively working on (`Captain-v2-control/` or `Captain-v2-matrix/`) so tasks and upload settings stay board-specific.

## Current Development Focus

- Matrix board is the current bring-up target (built hardware available now)
- Matrix MCU appears on I2C as a register-based peripheral at `0x24` (HT16K33-style model)
- Matrix protocol details and test workflow are documented in:
	- `Captain-v2-matrix/README.md`
- Hardware iteration note (March 2026): reduce the high-side rail voltage first to improve BSS84/BSS138 gate-drive margin before pursuing transistor substitutions (documented in `Captain-v2-matrix/README.md`, section 11)
- Redesign parking lot for next board iteration: `docs/NEXT_ITERATION_RECOMMENDATIONS.md`

## Music Streaming Server

`music_server.py` / `Start-MusicServer.ps1` stream audio to the pinball machine over Wi-Fi. See `docs/MP3_MUSIC_SETUP.md` and `docs/WIFI_STREAMING_SETUP.md` for setup.

## Documentation

All setup guides and reference docs are in `docs/`. See `docs/REPO_INDEX.md` for a full index.
