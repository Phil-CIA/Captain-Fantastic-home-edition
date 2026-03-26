# Captain v2 (split-MPU architecture)

This project is the clean start for the new hardware split:

- `captain_control`: gameplay, sound, display, solenoids, headbox lamps (`2x 74HC595`)
- `captain_matrix`: playfield switch/lamp matrix scan and I2C reporting

## Why this exists

The previous single-board design had switching-noise/ground-loop issues during attract mode. This version isolates matrix scanning and lamp drive on a dedicated MPU and sends matrix state to control via I2C.

## Environments

- `captain_control` (default)
- `captain_matrix`

Edit COM ports in `platformio.ini` as needed.

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
- Solenoid outputs are mapped to direct GPIO (not shift-register): `S2=GPIO23`, `S3=GPIO19`, `S4=GPIO18`, `S5=GPIO5`, `S6=GPIO17`
- Four cabinet/control inputs moved off matrix to direct GPIO: `SW1=GPIO36`, `SW2=GPIO39`, `Tilt=GPIO34`, `Start=GPIO35`
- I2S audio output mapped for MAX98357A: `DIN=GPIO25`, `BCLK=GPIO14`, `LRCK=GPIO13`
- External SPI flash upgraded to `W25Q128` (16MB) with same SPI mapping: `CS=GPIO32`, `MOSI=GPIO26`, `MISO=GPIO27`, `SCK=GPIO33`
- I2C bus is shared by both devices: display (`0x70`) and matrix-board MCU (`0x24`) on `SDA=GPIO21`, `SCL=GPIO22`
- Matrix lamp commands now drive real lamp outputs on matrix board via local `74HC595` (`DS=GPIO2`, `SHCP=GPIO12`, `STCP=GPIO4`)
- Matrix switch labels for `Tilt` and `Start` are annotated as moved to direct GPIO inputs on control board

## Next

1. Replace placeholder pin maps in each `main` file.
2. Move verified game-state logic from legacy into `captain_control`.
3. Implement real matrix scan and lamp command execution in `captain_matrix`.
4. Implement headbox lamp control with `74HC595` in `captain_control`.
