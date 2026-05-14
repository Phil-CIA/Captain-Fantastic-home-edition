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

## Gameplay Integration Next Steps - 2026-05-14

Goal remains the same: merge the recovered hardware/transport path with the existing gameplay firmware so the machine runs a real gameplay loop end-to-end.

### Where We Are Now

- Matrix board on COM4 boots clean after the 4MB flash-size fix.
- Control board on COM5 already has the gameplay firmware concentrated in Captain-v2/src/control_main.cpp.
- control_main.cpp already contains switch-to-solenoid dispatch, score accumulation, headbox lamp updates, direct-input handling, and tone/audio feedback.
- Remaining blockers are hardware/runtime validation, not large new gameplay-code invention.

### Immediate Priority Order

1. Re-confirm the hardware foundation after the May 13 reboot fix:
   - Flash both boards fresh.
   - Confirm control serial shows matrix ready=1 / linkHealthy=true.
   - Confirm wr_ok and rd_ok counters increment monotonically.
2. Close the matrix heartbeat decision:
   - Option A: keep the current row map and use serial heartbeat only.
   - Option B: free GPIO8 and add WS2812 heartbeat in Captain-v2/src/idf_main.cpp.
3. Run clean switch mapping capture:
   - 30s quiet baseline.
   - 15-20 deliberate presses on at least 3 switches.
   - Compare current behavior against known-good checkpoint aca9ac0.
   - Gate: one dominant bit per press and no suppression by burst filtering.
4. Verify S2 outhole relay status:
   - If relay workaround is not fully installed, finish bench wiring and validate force recovery.
   - If still pending, keep S2 protection limits enforced and do not treat gameplay as fully validated.
5. Validate direct inputs under live polling:
   - START, TILT, SW1, SW2.
   - 20 presses each, zero missed detections, zero false positives.

### Gameplay Bring-Up Sequence After Gates Pass

1. Confirm control board is running the intended Arduino gameplay target, not an IDF scaffold.
2. Press START and verify:
   - score reset
   - attract/headbox behavior starts cleanly
   - expected startup tone/audio path works
3. Trigger representative matrix switches and verify each full path:
   - switch edge detected
   - expected solenoid fires
   - score increments
   - lamp frame updates propagate correctly
4. Press TILT and verify:
   - tilt latches
   - gameplay behavior aborts as expected
   - solenoid behavior respects tilt state
5. Run a 5-minute soak:
   - no link faults
   - no dropped real switch events
   - no spurious coil activity

### Milestone Definition

This milestone is complete when the machine demonstrates at least one stable end-to-end gameplay flow:
- START input works.
- At least one switch-to-solenoid path works reliably.
- Score changes correctly.
- Lamps update correctly.
- Control-to-matrix link stays healthy during continuous run.

### Scope Boundaries

- Do not widen scope into multiplayer, multiball, or full audio-content migration yet.
- Do not treat ESP-IDF scaffolds as the gameplay merge target; control_main.cpp remains the main gameplay path.
- Focus on proving one stable playable loop first, then expand features after that baseline is locked.
