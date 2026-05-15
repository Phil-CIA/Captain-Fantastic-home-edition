# Captain v2 (split-MPU architecture)

This project is the clean start for the new hardware split:

- `captain_control`: gameplay, sound, display, solenoids, headbox lamps (`2x 74HC595`)
- `captain_matrix`: playfield switch/lamp matrix scan and I2C reporting

## Why this exists

The previous single-board design had switching-noise/ground-loop issues during attract mode. This version isolates matrix scanning and lamp drive on a dedicated MPU and sends matrix state to control via I2C.

## Environments

| Environment            | Board          | Role |
|------------------------|----------------|------|
| `captain_control`      | ESP32 DevKit   | Main game controller (default) |
| `captain_control_ota`  | ESP32 DevKit   | Same, OTA upload variant |
| `captain_matrix`       | ESP32 DevKit   | Switch/lamp matrix board |
| `captain_display`      | ESP32-C6       | Display-board SPI slave |
| `captain_host_display` | ESP32-C6       | Host display-link SPI master |

Edit COM ports in `platformio.ini` as needed.

## UART Bring-Up Checklist (ESP32-C6 Matrix)

Use this checklist for current matrix board revisions.

1. Use `captain_matrix_c6_idf` environment.
2. Connect through UART bridge on `COM8` (TX, RX, GND).
3. Prefer UART port for flashing/monitoring; native USB is not the primary path on this board.
4. Close any existing serial monitor before upload.
5. Upload command:

```powershell
platformio run -e captain_matrix_c6_idf -t upload
```

6. Open monitor:

```powershell
platformio device monitor -p COM8 -b 115200
```

7. Expected success signature on reset:
	- `boot:0x1c (SPI_FAST_FLASH_BOOT)`
	- ESP-IDF bootloader lines
	- `app_init: Project name: Captain-v2`

8. If monitor says `waiting for download`, verify BOOT/GPIO9 is not asserted and ensure only one tool has COM8 open.

## SPI Handshake (display link)

Two extra GPIO lines added to the IDC ribbon between host and display ESP32-C6
boards for RTS/CTS-style flow control:

- **GPIO0 / IDC pin 5** — REQUEST (host → display)
- **GPIO1 / IDC pin 9** — READY   (display → host)

See `docs/SPI_HANDSHAKE.md` for full protocol description, timing diagrams,
and bring-up checklist.  Source: `include/spi_handshake_config.h`,
`include/spi_handshake.h`, `src/spi_handshake.cpp`.

## First migration pass included

- Display driver module copied from legacy (`HT16K33`)
- Shared control-matrix I2C protocol contract (`include/captain_protocol.h`)
- Shared canonical matrix mapping (`include/captain_mapping.h`)
- Clean entry points for both MPUs (`src/control_main.cpp`, `src/matrix_main.cpp`)

## Mapping status

- Switch matrix names and coordinates mapped from legacy firmware (`M1..M8` × `SW1..SW4`)
- Lamp matrix names and coordinates mapped from legacy firmware (`M1..M8` × lamp columns)
- Matrix GPIO pin map centralized in one file for both firmware targets
- Headbox `2x74HC595` lamp map is implemented from schematic (`Ball`, `Player`, `Tilt`, `Game Over`)
- Solenoid outputs are mapped to direct GPIO (not shift-register): `S2=GPIO23`, `S3=GPIO19`, `S4=GPIO18`, `S5=GPIO16`, `S6=GPIO17`
- Flash safety note: keep solenoid power removed while uploading firmware; avoid leaving coil power on during reset/flash windows.
- Four cabinet/control inputs moved off matrix to direct GPIO: `SW1=GPIO36`, `SW2=GPIO39`, `Tilt=GPIO34`, `Start=GPIO35`
- I2S audio output mapped for MAX98357A: `DIN=GPIO25`, `BCLK=GPIO14`, `LRCK=GPIO13`
- External SPI flash upgraded to `W25Q128` (16MB) with same SPI mapping: `CS=GPIO32`, `MOSI=GPIO26`, `MISO=GPIO27`, `SCK=GPIO33`
- I2C bus is shared by both devices: display (`0x70`) and matrix-board MCU (`0x24`) on `SDA=GPIO21`, `SCL=GPIO22`
- Matrix lamp commands now drive real lamp outputs on matrix board via local `74HC595` (`DS=GPIO2`, `SHCP=GPIO12`, `STCP=GPIO4`)
- Matrix switch labels for `Tilt` and `Start` are annotated as moved to direct GPIO inputs on control board

## Hardware Design Files

KiCad schematics and PCB layouts are organized under `../hardware/kicad/`:

| Board | Location | PlatformIO env | Purpose |
|-------|----------|-----------------|---------|
| **Pinball Control board** | `../hardware/kicad/captain_control/` | `captain_control` | Main game controller |
| **Pinball matrix board** | `../hardware/kicad/captain_matrix/` | `captain_matrix` | Playfield matrix scanner/lamp driver |
| **7-segment display** | `../hardware/kicad/7seg_display/` | — | Standalone I2C HT16K33 score display |
| **Hat board (legacy)** | `../hardware/kicad/legacy/` | — | Old single-board design (superseded v1) |

See `../docs/` for hardware documentation, design notes, and integration guides.

## Board-Specific Projects

Board-specific PlatformIO project folders live at the repository root for isolated bring-up and board-by-board work:

| Project folder | Purpose |
|----------------|---------|
| `../Captain-v2-control/` | Separate PlatformIO project for the main control board firmware |
| `../Captain-v2-matrix/` | Separate PlatformIO project for the matrix board firmware and standalone test support |

Both folders point at the shared source tree under `Captain-v2/`, so firmware logic stays in one place while build/upload configuration stays isolated per board.

Matrix-board protocol and bring-up reference:
- `../Captain-v2-matrix/README.md` (datasheet-style firmware contract)

## Next

1. Replace placeholder pin maps in each `main` file.
2. Move verified game-state logic from legacy into `captain_control`.
3. Implement real matrix scan and lamp command execution in `captain_matrix`.
4. Implement headbox lamp control with `74HC595` in `captain_control`.
