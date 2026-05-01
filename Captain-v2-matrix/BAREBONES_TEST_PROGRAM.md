# Bare-Bones Matrix Test Program (May 2026)

## Purpose

`matrix_barebones_main.cpp` is a minimal firmware path for direct SR row/column testing without higher-level runtime effects.

It intentionally removes:
- I2C protocol handling
- switch scan and debounce
- OLED diagnostics
- complex matrix scheduler and command interactions

This gives one direct control loop over the shift-register frame output.

## File

- Source: `Captain-v2-matrix/src/matrix_barebones_main.cpp`
- Environment: `captain_matrix_idf_barebones`

## Core behavior

At boot, firmware:
1. Configures SR pins and OE#.
2. Composes row/column frame from constants.
3. Applies ON/OFF pattern using `TEST_ON_US` and `TEST_PERIOD_US`.
4. Optionally limits active window with `TEST_BOOT_WINDOW_MS`.

If `TEST_ENABLE_OUTPUTS=false`, OE# is forced disabled and firmware stays in a safe all-off loop.

## Safety controls (top-of-file constants)

- `TEST_ENABLE_OUTPUTS`
  - `false`: hard safety lockout (recommended default)
  - `true`: arm test output
- `TEST_BOOT_WINDOW_MS`
  - `0`: run indefinitely
  - non-zero: auto-expire test window
- `TEST_ON_US` and `TEST_PERIOD_US`
  - define duty cycle and average stress

Recommended starting point:
- `TEST_ENABLE_OUTPUTS=false`
- lamp rail disconnected

## Mapping/polarity A-B controls

- `TEST_ROW_INDEX`
- `TEST_COL_INDEX`
- `SR_CHAIN_IS_COL_THEN_ROW`
- `SR_ROW_ACTIVE_LOW`
- `SR_COL_ACTIVE_LOW`

Change one of these at a time between flashes.

## Build and flash

From `Captain-v2-matrix/`:

```powershell
C:/Users/user/.platformio/penv/Scripts/platformio.exe run -e captain_matrix_idf_barebones
C:/Users/user/.platformio/penv/Scripts/platformio.exe run -e captain_matrix_idf_barebones -t upload --upload-port COM4
```

## Safe bench sequence

1. Lamp rail disconnected.
2. Flash bare-bones with `TEST_ENABLE_OUTPUTS=false`.
3. Verify no lamp activation and no fuse heating.
4. Enable outputs only for short bounded runs.
5. Reconnect lamp rail only for brief controlled observations.
6. If whole-row activation appears, cut power immediately and return to lockout.

## Current known issue

Whole-row activation has been observed under some polarity settings (example: all Row 5 lamps energizing together). This is a high-current risk for the 1.85 A polyfuse and is the primary reason the lockout-first workflow is required.
