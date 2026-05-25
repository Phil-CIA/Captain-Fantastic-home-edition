# End Of Day Handoff - 2026-05-16

## Session Focus

Today moved from gameplay rule reconciliation into control-side gameplay audio bring-up.

Primary goals were:
1. Recover legacy gameplay sound behavior from the old monolithic firmware instead of inventing new tones.
2. Port that behavior into the current split-board control firmware on the existing I2S audio path.
3. Remove the annoying continuous diagnostic beeping from normal runtime.
4. Tune the start flow and score-count tones toward the real machine behavior.
5. Leave a precise stop point for the next session without reopening matrix flicker work.

## Executive Summary

### What is now known

1. **Gameplay audio is now wired into the active control runtime.**
   - Bumpers, slingshots, drain, bonus countdown, start, and score-count events are all driven from `Captain-v2/src/control_main.cpp`.
   - The active audio transport remains the current I2S path, not the old LM384 analog approach.

2. **The old legacy code was useful for sound behavior, not transport design.**
   - `src/main_firmware.cpp` contained concrete legacy tone/event behavior.
   - That legacy behavior was ported into the split-board control firmware as a starting point.

3. **The continuous diagnostic beeping should stay off in normal runtime.**
   - `Captain-v2/include/audio_i2s_config.h` now has `CAPTAIN_AUDIO_CONTINUOUS_DIAGNOSTIC = false`.
   - User previously had to unplug the speaker because the recurring diagnostic tone was too intrusive.

4. **Gameplay scoring/audio tuning moved forward, but the latest pass is not yet machine-verified by ear.**
   - Control firmware builds cleanly.
   - Control firmware uploads cleanly to COM5.
   - The latest retuned target/count/start pass has been flashed, but there is no final user validation yet on the newest audio behavior.

5. **The audio model is still an approximation of the original two-output hardware.**
   - Current firmware serializes queued tone events on one generated I2S waveform path.
   - It approximates the old count sound with a low base pulse plus a higher accent pulse in sequence.
   - It does **not** yet do true simultaneous dual-tone mixing.

## Current Runtime Truth

### Active projects / ports

- Matrix board project: `Captain-v2-matrix/`
- Matrix env: `captain_matrix_idf`
- Matrix port: `COM4`
- Control board project: `Captain-v2/`
- Control env: `captain_control`
- Control port: `COM5`

### Source of truth

- Matrix runtime source of truth: `Captain-v2-matrix/src/matrix_app_main.cpp`
- Control runtime source of truth: `Captain-v2/src/control_main.cpp`

## What Changed This Session

### 1) Gameplay tone hooks were added to the live control firmware

The active control runtime now queues tones for:
- bumpers
- slingshots
- drain
- bonus countdown
- start button / game start
- score-count style awards

These hooks were wired into the real gameplay switch handlers and bonus countdown path in `Captain-v2/src/control_main.cpp`.

### 2) Bumpers and slingshots were shifted to feel more mechanical

The first tone pass fired too early relative to the mech action.

Control firmware was updated so:
- bumper tones are slightly delayed
- slingshot tones are slightly delayed
- non-mechanical awards use count-style chimes instead of the same hit sound

Result:
- bumpers/slings should feel closer to the actual physical hit timing
- the rest of the playfield now sounds more like a scoring/count event instead of generic beeps

### 3) Repeated playfield audio retriggers were reduced

The active control runtime now tracks last-hit time per gameplay switch and suppresses short retriggers for most non-spinner playfield switches.

Result:
- targets and similar playfield awards should be less likely to machine-gun their tones from chatter

### 4) Bonus countdown was slowed down

`BONUS_COUNTDOWN_STEP_MS` in `Captain-v2/src/control_main.cpp` was increased from `200` to `350`.

Result:
- bonus count pacing should be slower than the previous too-fast pass
- it still needs machine verification

### 5) Start flow now waits for a longer game-start routine before serving

The control firmware no longer immediately kicks out the ball on START.

Current behavior:
- START enters new game state
- a longer game-start fanfare is queued
- serve-ball is delayed to match that routine

Current timing target:
- roughly 4 seconds before serve

### 6) Target scoring was corrected again during audio tuning

Current target scoring in `Captain-v2/src/control_main.cpp` is now:
- Target 1 = `500` score, `+2000` bonus, 500-style count
- Target 2 = `500` score, `+2000` bonus, 500-style count
- Target 3 = `1000` score, `+2000` bonus, 1000-style count

This supersedes the earlier temporary 50-point target pass.

### 7) Score-count chimes were retuned toward the machine behavior described today

The current approximation is:
- low base around `400 Hz`
- a second accent pulse to distinguish count class
- slower pacing so a 500-point count takes roughly three quarters of a second instead of finishing almost instantly

This was driven directly from the user's description of the machine behavior.

## Files Touched During The Session

### Control firmware
- `Captain-v2/src/control_main.cpp`
  - added delayed tone queue support
  - added gameplay tone hooks
  - added per-switch retrigger suppression for gameplay audio/events
  - slowed bonus countdown pacing
  - changed start-button behavior to play fanfare before serve
  - changed target scoring to 500 / 500 / 1000
  - retuned score-count chimes toward a 400 Hz base behavior

### Audio config
- `Captain-v2/include/audio_i2s_config.h`
  - left startup boot test enabled
  - set continuous diagnostic tone mode off for normal runtime

### Restart docs
- `NEXT_CHAT_PROMPT_2026-05-15.txt`
  - was previously updated with gameplay-rule reconciliation details
  - does not fully reflect the final 2026-05-16 audio state; use the new 2026-05-16 files instead

## Current Working State

### Good

1. Control firmware builds cleanly.
2. Control firmware uploads cleanly to COM5.
3. Gameplay audio events are now present in the active runtime.
4. Continuous diagnostic beeping is off by default.
5. Start now waits for a longer routine before serving the ball.
6. Target 1 / 2 / 3 scoring is currently coded as 500 / 500 / 1000.

### Still not signed off

1. The latest audio retune is not yet verified on the machine by ear.
2. The count-style chime is still an approximation, not a true dual-output recreation.
3. Boot-time startup test tones are still enabled in `Captain-v2/include/audio_i2s_config.h` and may later need to be shortened or disabled once gameplay sounds are trusted.
4. Bonus countdown pacing may still need more tuning.
5. Matrix baseline flicker remains deferred and should stay out of scope unless intentionally reopened.

## Important Current Diagnostic / Runtime State

### Matrix state to preserve

The matrix board should still be treated as frozen in the earlier good-enough diagnostic state:
- `MATRIX_ENABLE_STATUS_HEARTBEAT = false`
- `MATRIX_ENABLE_PERIODIC_IDLE_YIELD = false`
- `MATRIX_ENABLE_LINK_LOG = false`

Do not reopen matrix flicker work by default next session.

### Audio state to preserve

In `Captain-v2/include/audio_i2s_config.h`:
- `CAPTAIN_AUDIO_CONTINUOUS_DIAGNOSTIC = false`
- `CAPTAIN_AUDIO_STARTUP_TEST_ENABLED = true`

That means:
- recurring bench beeping is disabled
- short boot audio test still runs on power-up

## Recommended Next Steps

### Immediate next work

1. Power the machine and verify the newest audio pass by ear on real hardware.
2. Check START first: confirm the game-start routine length and that serve occurs after the routine, not during it.
3. Check Target 1 and Target 2: verify 500-point count feel and timing.
4. Check Target 3: verify 1000-point count feel and whether the higher-count variant is distinct enough.
5. Check bonus countdown pacing and decide whether `350 ms` per step is still too fast.

### After that, depending on what the machine says

6. If the tone character is close but not exact, retune pitch / pulse / gap constants in `Captain-v2/src/control_main.cpp` without changing gameplay rules again.
7. If the count needs to sound more like the original two-output hardware, implement real mixed dual-tone generation in the I2S sample writer instead of sequential queue events.
8. Only after audio feels acceptable should the session consider any broader music / MP3 behavior.

## Suggested Restart Prompt

Continue from `END_OF_DAY_HANDOFF_2026-05-16.md`.

Current state: gameplay audio is now wired into the active control firmware on COM5, continuous diagnostic beeping is disabled, bumpers/slings/draint/start/bonus-count/score-count events are all connected, and the latest pass retuned the count chime toward a low ~400 Hz base with a second accent pulse. Target scoring is currently coded as Target 1 = 500, Target 2 = 500, Target 3 = 1000, each with count-style audio, and START now waits for a longer roughly 4-second routine before serving the ball. This newest pass builds and uploads cleanly but has not yet been fully verified on the machine by ear. Next step is hardware verification of START timing, target count feel, and bonus countdown pacing. Preserve the current matrix diagnostic state and do not reopen flicker work unless explicitly choosing to.

## Agent-Readable Companion

Use `NEXT_CHAT_PROMPT_2026-05-16.txt` for the short restart version.