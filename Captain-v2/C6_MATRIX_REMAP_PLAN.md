# ESP32-C6 Matrix Remap Plan

Date: 2026-04-01

## Why remap is required
- Confirmed: GPIO9 is tied into `Sw_Col_3` network through an effective 2.2k pull-down.
- GPIO9 is a boot strap pin. Pulling it low at reset forces ROM download mode.
- Therefore, GPIO9 must be removed from matrix switch/lamp assignment on this board revision.

## Hard constraints
- Keep UART debug/program path stable on UART0:
  - UART0 TXD = GPIO16
  - UART0 RXD = GPIO17
- Do not assign boot strap GPIO9 to matrix logic.
- Prefer not to use GPIO12/GPIO13 for new logic if future native USB compatibility is desired.

## Current implemented mapping (2026-04-01)
- Active matrix scaffold is switch-scan only (no local shift-register driving).
- File: `src/idf_main.cpp`

## Switch columns (carrier-aligned implementation)
- Sw_Col3 moved off GPIO9 to eliminate strap conflict.

- Switch columns:
  - Sw_Col0 -> GPIO20
  - Sw_Col1 -> GPIO19
  - Sw_Col2 -> GPIO18
  - Sw_Col3 -> GPIO23 (remapped off GPIO9)

## Switch rows (active scaffold)
- Rows are currently a temporary bench-safe set while carrier pinout is being finalized:
  - Row0 -> GPIO15
  - Row1 -> GPIO5
  - Row2 -> GPIO6
  - Row3 -> GPIO7
  - Row4 -> GPIO8
  - Row5 -> GPIO10
  - Row6 -> GPIO11
  - Row7 -> GPIO1

Note: Row6/Row7 are temporary placeholders and may be revised when the final carrier remap is locked.

## Validation checklist after remap
1. Power on: boot should be `SPI_FAST_FLASH_BOOT` (not download mode).
2. Flash over COM8 succeeds without manual strap workarounds.
3. Serial log shows `Captain ESP-IDF matrix scaffold started`.
4. `switch pressed masks` changes when each switch column is exercised.
5. No watchdog warnings during 60s run.

## Implementation sequence
1. Update schematic/net labels and connector pinout for the new map.
2. Update firmware constants in `src/idf_main.cpp` and `src/idf_matrix_main.cpp`.
3. Update canonical mapping in `include/captain_mapping.h`.
4. Flash and validate on bench, then validate in-carrier.
