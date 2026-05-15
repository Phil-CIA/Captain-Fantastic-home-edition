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
- Shared source only; not the current matrix bring-up source of truth
- Separates control logic and switch/lamp matrix scanning across two ESP32 boards

### 3. Captain v2 board-specific projects
- `Captain-v2-control/platformio.ini` (control board focused project)
- `Captain-v2-matrix/platformio.ini` (matrix board focused project)
- Both reference shared source under `Captain-v2/` and keep board workflows isolated
- Current active board workflows:
	- Matrix board: `Captain-v2-matrix`, env `captain_matrix_idf`, COM4
	- Control board: `Captain-v2`, env `captain_control`, COM5

> **VS Code tip:** Open the specific project folder you are actively working on (`Captain-v2-control/` or `Captain-v2-matrix/`) so tasks and upload settings stay board-specific.

## Current Development Focus

- Active matrix bring-up/runtime source of truth is `Captain-v2-matrix/src/matrix_app_main.cpp`
- Matrix MCU appears on I2C as a register-based peripheral at `0x24` (HT16K33-style model)
- Matrix/control communication path is now usable for gameplay bring-up:
	- control-side polarity normalization and stability filtering restored usable switch behavior
	- repeated held-switch scoring was fixed
	- gameplay-side bumper / slingshot output mapping has been corrected
- Matrix protocol details and test workflow are documented in:
	- `Captain-v2-matrix/README.md`
- Remaining open issue is residual lamp flicker, but the structured blink component was traced to matrix-side periodic debug / housekeeping work in the refresh loop
- Current stop-point is good enough to continue gameplay bring-up while leaving a later focused flicker cleanup pass
- Hardware iteration note (March 2026): reduce the high-side rail voltage first to improve BSS84/BSS138 gate-drive margin before pursuing transistor substitutions (documented in `Captain-v2-matrix/README.md`, section 11)
- Redesign parking lot for next board iteration: `docs/NEXT_ITERATION_RECOMMENDATIONS.md`

## Music Streaming Server

`music_server.py` / `Start-MusicServer.ps1` stream audio to the pinball machine over Wi-Fi. See `docs/MP3_MUSIC_SETUP.md` and `docs/WIFI_STREAMING_SETUP.md` for setup.

## Documentation

All setup guides and reference docs are in `docs/`. See `docs/REPO_INDEX.md` for a full index.

## KiCad Current Project Entry Points

If KiCad is opening the wrong local copy, use only these repo project files:

- `hardware/kicad/captain_matrix/Pinball matrix board.kicad_pro`
- `hardware/kicad/captain_control/Pinball Control board.kicad_pro`
- `hardware/kicad/legacy/Hat board.kicad_pro` (historical reference project)

To keep KiCad focused on repo paths:

1. Launch with `Open-KiCad.ps1` from repo root.
2. Run `Reset-KiCad-Recents-ToRepo.ps1` once to clear stale KiCad recent/open project paths.
3. Use `hardware/kicad/SOURCE_OF_TRUTH.md` as the canonical board-path map.
