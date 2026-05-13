# End Of Day Handoff - 2026-05-13

## Session Focus

Today focused on two items:
1. Verify that switch-to-solenoid mapping was not accidentally wrong.
2. Recover matrix board boot stability so matrix heartbeat and switch traffic can resume.

## What Was Completed

### 1) Mapping sanity check on control firmware

Verified control dispatch in Captain-v2/src/control_main.cpp is internally consistent with Captain-v2/include/captain_mapping.h:
- row0,col0 -> S2 pulse path (outhole handling)
- row2,col1 -> S3
- row3,col1 -> S4
- row1,col3 -> S5
- row4,col3 -> S6

Also confirmed rows 6-7 are still masked in readMatrixSwitches(), so undefined matrix rows do not drive coils.

### 2) Matrix reboot-loop root cause identified and fixed

Observed recurring boot crash on COM4:
- Detected size(4096k) smaller than binary header(8192k)
- assert failed: __esp_system_init_fn_init_flash

Root cause:
- Flash size config mismatch in upload/build path (board still treated as 8MB even after partial config edits).

Fix applied:
- Captain-v2/platformio.ini (env captain_matrix_c6_idf):
  - board_build.flash_size = 4MB
  - board_upload.flash_size = 4MB
- Captain-v2/sdkconfig.captain_matrix_c6_idf:
  - CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
  - CONFIG_ESPTOOLPY_FLASHSIZE="4MB"

Validation:
- Clean erase/upload on COM4 succeeded.
- Boot log now reports SPI Flash Size : 4MB.
- Flash-size mismatch assert no longer appears.

### 3) Why heartbeat LED was not visible

Heartbeat confusion resolved:
- Active compiled source for Captain-v2 matrix IDF env is src/idf_main.cpp (set by src/CMakeLists.txt).
- That file currently has no status LED heartbeat code.
- GPIO8 is used in that active row pin map, so onboard WS2812 heartbeat on GPIO8 is not available in current scaffold mapping.

Note:
- A heartbeat implementation was added to src/idf_matrix_main.cpp, but that file is not currently compiled by Captain-v2/src/CMakeLists.txt.

## Files Touched Today

- Captain-v2/platformio.ini
- Captain-v2/sdkconfig.captain_matrix_c6_idf
- Captain-v2/src/idf_matrix_main.cpp

## Current Hardware/Runtime State

- Control board upload path on COM5 is working.
- Matrix board on COM4 now boots clean (no flash assert loop).
- Matrix heartbeat LED still not visible in active scaffold because status LED code is not in currently compiled entrypoint and GPIO8 is consumed by row mapping there.

## Open Items For Next Session

1. Decide heartbeat strategy for matrix board:
   - Option A: Keep current row map and use serial heartbeat only.
   - Option B: Free GPIO8 from row mapping and add WS2812 heartbeat in src/idf_main.cpp.
2. Resume clean switch mapping capture now that matrix is stable again.
3. Confirm matrix link from control returns ready=1 under normal run after reboot fix.

## Suggested Restart Prompt

Continue from END_OF_DAY_HANDOFF_2026-05-13.md.

Matrix flash-size reboot loop is fixed (4MB config + upload override). Next, implement chosen matrix heartbeat path in the active Captain-v2 ESP-IDF entrypoint (src/idf_main.cpp), then run a clean switch mapping capture and confirm control reports matrix ready=1.
