# End Of Day Handoff - 2026-05-15

## Session Focus

Today completed the switch-transfer / gameplay bring-up pass far enough to get the machine into a usable state, then narrowed the remaining lamp artifact instead of continuing broad rewrites.

Primary goals were:
1. Restore trustworthy enough switch visibility on COM5 to resume gameplay bring-up.
2. Eliminate repeated held-switch scoring and obvious transport/polarity mistakes.
3. Correct gameplay-to-solenoid mapping errors found during live playfield tests.
4. Determine whether the remaining lamp blink/flicker was caused by periodic firmware work rather than the base lamp drive itself.
5. Leave a realistic stop-point because the machine is usable but not fully signed off.

## Executive Summary

### What is now known

1. **Matrix-to-control switch transfer is working well enough for gameplay.**
   - Raw switch bytes are reaching COM5.
   - Control-side polarity normalization now matches the real transport polarity.
   - Switches are visible again and user reports the current feel is good / acceptable.

2. **The repeated held-switch "machine gun" symptom was fixed on the control side.**
   - A second-stage control-side stability filter now latches presses immediately and confirms release over multiple polls.
   - Release confirmation was increased to reduce chatter-based retriggers.

3. **Gameplay switch handling is functional again.**
   - Presses now score on press instead of only after long or repeated holds.
   - Raw and filtered switch diagnostics on COM5 made it possible to prove the transport path instead of guessing.

4. **Bumper / slingshot solenoid routing was wrong and is now corrected.**
   - User confirmed the current solenoid behavior is good.

5. **The remaining structured lamp blink was caused by matrix-side periodic debug / housekeeping work in the refresh loop.**
   - Disabling the matrix status heartbeat changed the blink pattern.
   - Disabling the 2-second periodic idle yield nearly removed the blink.
   - Disabling the matrix link log removed the remaining obvious structured blink component.
   - This is the clearest root-cause signal found today.

6. **Some baseline flicker still exists, but the current state is acceptable enough to move on.**
   - User is not fully sold on the current lamp behavior.
   - User also explicitly said it is no longer bad enough to block moving forward.

## Current Runtime Truth

### Active projects / ports

- Matrix board project: `Captain-v2-matrix/`
- Matrix env: `captain_matrix_idf`
- Matrix board port: `COM4`
- Control board project: `Captain-v2/`
- Control env: `captain_control`
- Control board port: `COM5`

### Source of truth

- Matrix runtime source of truth: `Captain-v2-matrix/src/matrix_app_main.cpp`
- Control runtime source of truth: `Captain-v2/src/control_main.cpp`

## What Changed This Session

### 1) Restored switch publication and interpretation

The investigation stopped treating gameplay as the root cause and went back upstream to matrix publication and control acceptance.

Key changes:
- ensured fresh switch response publication after matrix scans
- added raw switch-byte visibility on COM5
- added control-side normalization so active-low transport becomes active-high gameplay bits

Result:
- COM5 now sees real switch transitions again
- gameplay events fire on press

### 2) Added control-side stability filtering to stop repeated counts

The main nuisance symptom after switch visibility returned was repeated scoring while a switch was held.

Control-side filtering was updated so:
- a press is accepted immediately
- release must remain stable across multiple polls before being considered real

Result:
- the "machine gun" behavior is gone
- switch feel stayed acceptable after the final tuning

### 3) Corrected gameplay output mapping

Live bench tests showed left/right sides were broadly correct, but bumper versus slingshot outputs were crossed.

Gameplay handler mappings in `Captain-v2/src/control_main.cpp` were corrected for:
- left slingshot / left bumper
- right slingshot / right bumper

Result:
- user confirmed the solenoids are now correct

### 4) Reduced matrix scan overhead without regressing switch feel too badly

Matrix dedicated switch scan timings were trimmed to the current best-balance state:
- blank = `100 us`
- settle = `100 us`
- release = `25 us`

An every-other-frame scan test was tried and rejected because it made switches and bumpers too sluggish.

Result:
- current scan timings are the best compromise found so far

### 5) Isolated the structured blink to matrix refresh-loop disturbances

This was the most important flicker finding of the day.

The matrix firmware had several periodic tasks running in the same main loop as lamp refresh:
- WS2812 status heartbeat
- periodic link logging
- a forced `vTaskDelay(1)` every `2,000,000 us`

Observed behavior during probes:
- after reducing matrix periodic work, the blink period changed instead of staying fixed
- when the `2 s` idle-yield path was disabled, the blink almost disappeared
- after the matrix link log was disabled, the obvious structured blink component was effectively gone

Conclusion:
- the distinct blink was not a lamp hardware mystery
- it was refresh interruption from debug / housekeeping work sharing the lamp loop

## Files Touched During The Session

### Matrix firmware
- `Captain-v2-matrix/src/matrix_app_main.cpp`
  - retained dedicated switch scan architecture
  - tuned switch scan timing to the current best compromise
  - disabled matrix status-heartbeat activity for diagnosis
  - disabled periodic idle-yield activity for diagnosis
  - disabled periodic matrix link logging for diagnosis

### Control firmware
- `Captain-v2/src/control_main.cpp`
  - added / tuned control-side switch normalization and filtering
  - improved raw vs filtered matrix diagnostics
  - reduced redundant lamp writes
  - corrected bumper / slingshot gameplay-to-solenoid mapping
  - left polling at the later slower test value that slightly helped flicker

## Current Working State

### Good

1. Matrix and control firmware both build and upload cleanly.
2. Switch visibility is back.
3. Repeated held-switch scoring is fixed.
4. Solenoid mapping is correct enough for gameplay bring-up.
5. Switch feel is currently good / acceptable.
6. The big structured blink was traced to matrix periodic debug work and is largely gone in the current diagnostic state.

### Still not ideal

1. There is still some baseline lamp flicker.
2. The current matrix firmware still contains bench-style diagnostic disables that should not be treated as the final architectural cleanup.
3. Spinner responsiveness is acceptable, but not fully polished.
4. The user is willing to move on, but is not calling the flicker solved.

## Important Current Diagnostic State

The matrix board is currently running with these periodic features disabled in `Captain-v2-matrix/src/matrix_app_main.cpp`:
- `MATRIX_ENABLE_STATUS_HEARTBEAT = false`
- `MATRIX_ENABLE_PERIODIC_IDLE_YIELD = false`
- `MATRIX_ENABLE_LINK_LOG = false`

This is intentional for diagnosis and should be treated as the current known-good-enough runtime, not as polished final behavior.

## Recommended Next Steps

### Immediate next work

1. Resume gameplay bring-up while preserving the current matrix diagnostic state.
2. Avoid reopening broad switch-transfer rewrites unless a new concrete regression appears.
3. Keep using the current matrix and control sources of truth rather than older shared-source assumptions.

### When returning to the flicker issue

4. Convert the periodic debug behaviors into explicit optional bench-only flags / build modes instead of leaving them as ad hoc edits in the main loop.
5. Measure whether `serviceI2C()` still causes visible refresh modulation under real control traffic.
6. If flicker is still worth chasing later, move the remaining investigation toward lamp-refresh ownership and scheduling boundaries, not switch polarity or gameplay logic.

## Suggested Restart Prompt

Continue from `END_OF_DAY_HANDOFF_2026-05-15.md`.

Current state: switch transport is working well enough for gameplay again, control-side polarity/filtering fixed the repeated-count problem, bumper/slingshot solenoid routing is corrected, and user says switches now feel good enough to move on. The remaining lamp issue is no longer a mysterious 1-second blink: today proved that the structured blink was caused by matrix-side periodic debug / housekeeping work inside `Captain-v2-matrix/src/matrix_app_main.cpp` (status heartbeat, link logging, and especially the 2-second idle yield). Those periodic tasks are currently disabled in the matrix firmware, which largely removed the structured blink, though some baseline flicker still exists. User is not fully satisfied with the lamp behavior, but it is no longer blocking progress. Next step is to preserve this state, move gameplay bring-up forward, and only later return to baseline flicker with a focused `serviceI2C()` / refresh-scheduling investigation.

## Agent-Readable Companion

Use `NEXT_CHAT_PROMPT_2026-05-15.txt` for the short restart version.