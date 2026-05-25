# End Of Day Handoff - 2026-05-21

## Session Focus

Today shifted from audio bring-up into WS2812 backglass illumination bring-up on the Captain control board.

Primary goals were:
1. Prove a WS2812B strip could be driven safely from the active Captain control firmware.
2. Establish a practical current cap for bench and idle-safe patterns.
3. Physically map the strip behind the backglass.
4. Turn that physical map into firmware segment and zone definitions.
5. Leave the repo in a restartable state with a useful visual test pattern already flashed.

## Executive Summary

### What is now known

1. **WS2812 backglass lighting is working on the live control board.**
   - The active control runtime in `Captain-v2/src/control_main.cpp` now drives the strip on GPIO 15 using the existing Adafruit NeoPixel dependency.
   - Build and upload on `captain_control` / COM5 succeeded repeatedly during the session.

2. **A 3A software current cap is now the intended guardrail for test patterns.**
   - Current strip test logic computes brightness from:
     - `HEARTBEAT_ACTIVE_LED_COUNT = 300`
     - `HEARTBEAT_MAX_CURRENT_MA = 3000`
     - `HEARTBEAT_FULL_WHITE_MA_PER_LED = 60`
   - This does not replace hardware protection, but it is the current firmware safeguard against a stuck bright pattern drawing excessive current.

3. **The strip has been physically mapped behind the backglass and stored in firmware.**
   - The measured 15-segment layout is committed as `HEARTBEAT_SEGMENT_RANGES[]` in `Captain-v2/src/control_main.cpp`.
   - This supersedes the earlier temporary equal-size segment mapper.

4. **The firmware now supports both segment and zone test layers.**
   - `HEARTBEAT_SEGMENT_MAP_MODE = false`
   - `HEARTBEAT_ZONE_MAP_MODE = true`
   - Segment definitions remain in code.
   - The active test pattern is now a zone-by-zone walker built on top of the measured segment map.

5. **Backglass overlay grouping has started and is good enough for the next session to continue from.**
   - The current zone grouping is an art-oriented approximation, not final production choreography.
   - A brighter fixed zone palette was selected because darker colors did not read well through the glass.

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

### 1) WS2812 strip bring-up was added to the active control runtime

Control firmware now includes a NeoPixel-based backglass strip driver on GPIO 15.

This was validated on real hardware with the user observing correct strip response.

### 2) Brightness/current exploration was completed enough to set a working guardrail

The session tested:
- small safe bench patterns
- 240 LED fade / flash coverage checks
- higher brightness passes
- a software current cap strategy

Current working decision:
- keep firmware patterns bounded to roughly a 3A full-white equivalent ceiling until a later design intentionally changes it

### 3) The strip was physically counted and remapped behind the backglass

The original later-segment mapping was wrong after rearrangement.

A corrected measured physical map was captured and turned into firmware ranges.

### 4) Segment and zone test modes were built on top of the measured map

The session produced:
- a raw segment walker for physical validation
- static counting patterns for recount work
- a larger zone walker for art-region verification

### 5) Zone colors were retuned for visibility through the backglass

The active zone test no longer uses dark generic colors.

Current fixed zone colors are brighter, higher-luminance hues chosen to survive behind the printed glass.

## Files Touched During The Session

### Control firmware
- `Captain-v2/src/control_main.cpp`
  - added WS2812 strip support on GPIO 15
  - added software current cap / brightness limit logic
  - added measured `HEARTBEAT_SEGMENT_RANGES[]`
  - added named `HEARTBEAT_ZONE_DEFINITIONS[]`
  - added segment walker and zone walker test paths
  - set current active test path to zone mode
  - changed test palette to fixed brighter backglass-friendly colors

### Repo memory / working notes
- `/memories/repo/captain-ws2812-bringup-2026-05-21.md`
- `/memories/repo/captain-headbox-segment-map-2026-05-21.md`

## Current Working State

### Good

1. Control firmware builds cleanly.
2. Control firmware uploads cleanly to COM5.
3. WS2812 strip bring-up is proven on real hardware.
4. Measured 15-segment physical map is stored in code.
5. Named zone test pattern is active and flashed.
6. Bright fixed colors read better through the backglass than darker hues.

### Still not signed off

1. The final art-region grouping is not production-final; it is only good enough for next-pass tuning.
2. Controlled lamp flicker appeared worse when strip activity increased, but that was intentionally deferred instead of debugged today.
3. No permanent gameplay lighting choreography was designed yet.
4. Audio verification from the 2026-05-16 session still remains open unless separately checked on hardware.

## Important Current Diagnostic / Runtime State

### Matrix state to preserve

Do not reopen broad matrix flicker work by default.

The user explicitly chose not to chase the controlled-lamp flicker interaction today while segment and brightness mapping were still being established.

### LED state to preserve

Current intended WS2812 test configuration in `Captain-v2/src/control_main.cpp`:
- `HEARTBEAT_SEGMENT_MAP_MODE = false`
- `HEARTBEAT_ZONE_MAP_MODE = true`
- `HEARTBEAT_ACTIVE_LED_COUNT = 300`
- 3A software current cap logic is enabled through the brightness calculation
- active pattern is zone-by-zone walking test

## Measured Segment Map

Current physical segment map, 1-based inclusive in bench notes:

- Seg 0: 1-11
- Seg 1: 12-22
- Seg 2: 23-52
- Seg 3: 53-84
- Seg 4: 85-114
- Seg 5: 115-137
- Seg 6: 138-146
- Seg 7: 147-168
- Seg 8: 169-194
- Seg 9: 195-212
- Seg 10: 213-231
- Seg 11: 232-245
- Seg 12: 246-264
- Seg 13: 265-272
- Seg 14: 273-300

Stored in code as 0-based end-exclusive ranges.

## Current Zone Layer

Current named zone grouping in code:
- start marker
- score panel
- right creature edge
- title banner
- left moon edge
- bottom stage
- lower-left transition
- right interior column
- organ and rocket trail
- performer row
- center split
- captain ring
- rabbit and left interior

These are test labels for visual alignment, not locked gameplay design.

## Recommended Next Steps

### Immediate next work

1. Power the machine and review the fixed-color zone walker through the real backglass again.
2. Decide which zone memberships are correct enough to keep and which need segment reassignment.
3. Freeze a final zone map for the first gameplay lighting pass.
4. Only after the zone layout is accepted, design one or two simple gameplay-reactive effects.

### After that

5. Revisit strip interaction with controlled-lamp flicker if it still matters once lighting patterns settle.
6. Decide whether the 3A software current cap remains the production-safe ceiling or only a bring-up limit.
7. Resume the older audio verification work only if intentionally choosing to reopen that scope.

## Suggested Restart Prompt

Continue from `END_OF_DAY_HANDOFF_2026-05-21.md`.

Current state: WS2812 backglass lighting is now working on the active Captain control board on COM5 using GPIO15 in `Captain-v2/src/control_main.cpp`. A measured 15-segment strip map is stored in firmware, named art-oriented zone definitions are layered on top of it, and the currently flashed test pattern is a fixed-color zone walker with bright hues chosen to read through the backglass better than dark colors. The firmware uses a software brightness cap derived from a 3A full-white budget across 300 active LEDs. Controlled-lamp flicker interaction was noticed during brighter strip tests but intentionally deferred. Next step is to refine the zone grouping and freeze a final backglass zone layout before implementing gameplay-reactive lighting effects.

## Agent-Readable Companion

Use `NEXT_CHAT_PROMPT_2026-05-21.txt` for the short restart version.
