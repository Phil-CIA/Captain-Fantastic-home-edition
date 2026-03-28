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

This repo contains **two separate PlatformIO projects**:

### 1. Legacy Firmware (root)
- `platformio.ini` at the repository root
- Active environment: `combined_rtos`
- Targets a single ESP32 running gameplay, displays, sound, and OTA

### 2. Captain v2 (split-MPU)
- `Captain-v2/platformio.ini`
- Active environment: `captain_control`
- Separates control logic and switch/lamp matrix scanning across two ESP32 boards

> **VS Code tip:** Open each project folder separately in VS Code so PlatformIO picks up the correct `platformio.ini`. Opening the repo root will load the legacy project; opening `Captain-v2/` will load the v2 project.

## Music Streaming Server

`music_server.py` / `Start-MusicServer.ps1` stream audio to the pinball machine over Wi-Fi. See `docs/MP3_MUSIC_SETUP.md` and `docs/WIFI_STREAMING_SETUP.md` for setup.

## Documentation

All setup guides and reference docs are in `docs/`. See `docs/REPO_INDEX.md` for a full index.
