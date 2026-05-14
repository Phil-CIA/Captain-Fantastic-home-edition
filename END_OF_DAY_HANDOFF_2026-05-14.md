# End Of Day Handoff - 2026-05-14

## Session Focus

Today moved from gameplay implementation into matrix/control fault isolation.

Primary goals were:
1. Explain visible gameplay lamp flicker.
2. Determine whether COM5 was reading valid switch bytes from COM4.
3. Separate matrix lamp-drive problems from matrix switch-scan problems.
4. Leave the repo with a realistic current-state handoff instead of the earlier optimistic checkpoint.

## Executive Summary

### What is now known

1. **Lamp hardware path is good.**
   - A forced local L19 drive on the matrix board was solid.
   - That ruled out downstream lamp wiring, lamp power path, and the individual lamp driver path as the primary cause of the flicker.

2. **The old integrated matrix switch scan was disturbing lamp drive.**
   - With matrix switch scanning disabled, gameplay lamps stopped flickering.
   - This was the strongest proof that the scan architecture, not gameplay lamp ownership, was the source of the visual issue.

3. **Separating lamp refresh from switch acquisition fixed the lamp flicker.**
   - The matrix firmware was changed so lamp refresh and switch scanning are no longer performed in the same row phase.
   - Result: lamps now look good in normal operation.

4. **Ghost / false switch inputs still remain, but are reduced.**
   - False scoring and bonus increments still happen even after the lamp fix.
   - User disconnected solenoids for safety, but score/bonus still climb, confirming the ghost-input problem is still active.

5. **There is still evidence of protocol/readback corruption or mis-windowed reads.**
   - Repeated COM4/COM5 captures showed COM5 `sw0..sw3` values matching the first bytes of COM4 lamp data (`0x0C 0x16 0x04 ...`) rather than behaving like independent switch state.
   - Multiple attempts to reduce this were made. Improvement happened, but the issue is not fully eliminated.

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

### 1) Reframed the problem from gameplay logic to matrix behavior

User reported:
- lamps visibly flickering during gameplay
- switch response requiring long holds
- repeated late counts / ghost inputs
- ball/bonus behavior being polluted by unreliable switch data

The investigation pivoted away from gameplay rules and into matrix firmware behavior.

### 2) Proved the lamp flicker was not caused by control-side lamp ownership

Control-side review showed L19 and other gameplay lamps were being commanded as steady lamps, not intentionally flashed.

The matrix row timing itself was much faster than visible flicker, so a slower modulation or scan coupling issue was suspected.

### 3) Forced-lamp probe on matrix board

A dedicated matrix-side diagnostic was added to force the L19 path locally.

Result:
- lamp was solid
- no flicker in forced-drive mode

Conclusion:
- the physical lamp path was not the root problem
- the normal scan/switch interaction was the real disturbance source

### 4) Separated lamp refresh from switch acquisition

Matrix firmware was changed so:
- lamp refresh continues row-by-row
- switch scanning runs in its own dedicated all-off pass instead of sharing the lamp row phase

This was the key architectural fix.

Result:
- lamps look good now
- visible flicker problem is effectively solved

### 5) Slowed and cleaned the dedicated switch scan pass

Switch scan timing was then adjusted with larger:
- blank time before sampling
- settle time with row asserted
- release time after each sampled row

Result:
- ghost inputs reduced somewhat
- not eliminated

### 6) Investigated matrix-side FIFO / response-window behavior

Repeated captures showed a suspicious pattern:
- COM4 lamp bytes: `lamp=[0C,16,04,1E,0C]`
- COM5 switch bytes: `sw0=0x0C sw1=0x16 sw2=0x04 sw3=0x00`

That strongly suggests the control board is still sometimes consuming lamp-frame-like data as switch bytes.

Matrix-side experiments performed:
- preloading switch response window after switch scan
- removing unsolicited preloading per user request
- simplifying fallback response behavior

Result:
- some improvement, but not a full fix
- user explicitly preferred removing clever FIFO behavior rather than relying on preloads

### 7) Added a control-side guard against lamp-echo switch reads

Control firmware now rejects a switch frame if it exactly matches the first four bytes of the most recently transmitted lamp frame.

This reduces one known bad readback pattern and adds a summary counter (`sup_le`) for visibility.

Result:
- did not fully clear the ghost-input problem
- confirms there is still a deeper issue beyond that one obvious echo case

## Files Touched This Session

### Matrix firmware
- `Captain-v2-matrix/src/matrix_app_main.cpp`
  - removed integrated switch sampling from lamp refresh
  - restored dedicated switch scan pass
  - increased switch scan blank/settle/release timing
  - experimented with switch-response FIFO handling
  - simplified matrix-side response preload behavior

### Control firmware
- `Captain-v2/src/control_main.cpp`
  - added score-display update guard (test only; not root cause)
  - added lamp-echo switch-read suppression
  - added `sup_le` visibility in matrix link summary

### Matrix config / build recovery
- `Captain-v2-matrix/sdkconfig.captain_matrix_idf`
  - adjusted during build recovery to get the real matrix project building again under PlatformIO/ESP-IDF

## What Is Working Now

1. COM4 real matrix firmware is restored and buildable from `Captain-v2-matrix`.
2. COM5 control firmware builds and uploads normally.
3. Lamps look good in gameplay.
4. The forced-lamp diagnostic proved the lamp path is healthy.
5. Separate lamp/switch scan architecture is in place.

## What Is Not Solved Yet

### Primary open issue

**Ghost switch inputs still exist.**

Observed by user as:
- score increments without real playfield hits
- bonus increments without intended switch events
- earlier coil activations before solenoids were disconnected

### Strong current hypotheses

1. **Switch readback path is still not fully trustworthy.**
   - COM5 switch bytes still resemble COM4 lamp bytes too closely in repeated captures.

2. **There may still be a matrix-side slave-response / request-order problem.**
   - The protocol may be too permissive about what is present in the slave TX path when the master requests switch registers.

3. **There may also be a real switch-matrix bias / polarity issue layered on top.**
   - The remaining ghost inputs are reduced, not unchanged, so more than one mechanism may be involved.

## Important Negative Findings

These are now effectively ruled out as primary root causes:

1. Gameplay lamp logic in `control_main.cpp`
2. Simple 10 Hz score-display rewrite as the main lamp-flicker source
3. Downstream lamp wiring / single-lamp hardware path failure
4. “All flicker is just because the matrix scan rate is too slow”

## Current Safety State

- User disconnected solenoids for now.
- This was the right move while ghost inputs are still present.
- Do **not** re-enable normal coil testing until switch-state trust is improved.

## Recommended Next Steps

### Highest priority

1. Instrument the actual switch-register request/response path more aggressively.
   - Prove what COM4 thinks it is returning for `0x40..0x43` at the moment COM5 requests it.
   - Prove what COM5 requested immediately before each bad read.

2. Add one narrow protocol sanity check on the control side.
   - If switch bytes equal a known impossible or lamp-like pattern, ignore them and log them.
   - Keep solenoids disabled while doing this.

3. Confirm whether the remaining bad frames are:
   - stable repeating patterns
   - or truly random patterns

### After protocol truth is established

4. If the protocol path is clean, move to physical switch bias investigation:
   - row polarity
   - column pull-up behavior
   - discharge time between sampled rows
   - whether switch columns need stronger pull-ups or different scan polarity assumptions

## Suggested Restart Prompt

Continue from `END_OF_DAY_HANDOFF_2026-05-14.md`.

The lamp flicker issue is effectively solved by separating matrix lamp refresh from switch scanning in `Captain-v2-matrix/src/matrix_app_main.cpp`. The remaining blocker is ghost switch input: score and bonus still increment without valid hits, even though lamps are now stable. Repeated COM4/COM5 captures still suggest COM5 sometimes sees switch bytes that numerically resemble COM4 lamp bytes (`0x0C 0x16 0x04 ...`). Solenoids are intentionally disconnected by the user for safety. Next step is protocol-truth instrumentation of the matrix switch-register request/response path before making broader electrical assumptions.

## Agent-Readable Companion

Use `NEXT_CHAT_PROMPT_2026-05-14.txt` for the short restart version.