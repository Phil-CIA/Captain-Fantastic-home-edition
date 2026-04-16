# System Behavior Contract — Captain Fantastic Control Board Firmware

**Status:** Under Definition
**Last Updated:** 2026-04-16
**Owner:** [You]
**Reference Sources:**
- Captain-Fantastic-home-edition/src/main_firmware.cpp (lines 105-1143: game state machine, scoring, bonus, diagnostics)
- Captain-Fantastic-home-edition/docs/MP3_MUSIC_SETUP.md (audio architecture intent)
- Captain-Fantastic-home-edition/docs/WIFI_STREAMING_SETUP.md (hybrid storage/streaming model)
- Captain-Fantastic-home-edition/docs/DIAGNOSTIC_TEST_MODE.md (Bally diagnostic specification)

---

## I. GAME STATE MACHINE

**Authoritative Source:** main_firmware.cpp lines 1024-1143

### Six States (50ms cycle interval)
1. ATTRACT - Idle loop, random lamp chase, attract music, awaiting START button
2. GAME_START - Player pressed START, play start fanfare, initialize scoring/bonus/multiplier
3. BALL_IN_PLAY - Active gameplay, switches score points, solenoids respond, lamps track features
4. BONUS_COUNTDOWN - Ball drained, count down bonus to score, play countdown music
5. BALL_OVER - End of bonus countdown, return to ATTRACT
6. GAME_OVER - All balls exhausted, play game-over fanfare, flash high-score lamps

### State Transitions
```
ATTRACT --[START button]--> GAME_START
GAME_START --[immediate]--> BALL_IN_PLAY
BALL_IN_PLAY --[drain/tilt]--> BONUS_COUNTDOWN
BONUS_COUNTDOWN --[countdown ends]--> BALL_OVER
BALL_OVER --[immediate OR new ball time]--> BALL_IN_PLAY (repeat) OR GAME_OVER (if no balls left)
GAME_OVER --[display high score]--> ATTRACT
```

---

## II. SCORING AND SWITCH MATRIX (22 Switches)

**Authoritative Source:** main_firmware.cpp lines 620-754

### Switch Functions and Point Values

| Switch | Point Value | Solenoid Coupling | Feature Logic |
|--------|-------------|-------------------|---------------|
| Bumper 1 | 100 | None | Kickback ready if lit |
| Bumper 2 | 100 | None | Kickback ready if lit |
| Bumper 3 | 100 | None | Kickback ready if lit |
| Lane A | 500 | None | Lane complete tracking -> Extra ball at A-B-C-D |
| Lane B | 500 | None | Lane complete tracking |
| Lane C | 500 | None | Lane complete tracking |
| Lane D (Return Left) | 1000 | Multiplier advance | Advance return lane 1X->2X->3X->5X on successive completions |
| Lane E (Return Right) | 1000 | Multiplier advance | Advance return lane multiplier |
| Target 1 | 250 | Solenoid-1 | Target progression 1-2-3 -> Double/Triple bonus at 3-complete |
| Target 2 | 250 | Solenoid-2 | Target progression |
| Target 3 | 250 | Solenoid-3 | Target progression |
| Rollover 1 | 300 | None | Bonus pole increment (1K-19K) |
| Rollover 2 | 300 | None | Bonus pole increment |
| Rollover 3 | 300 | None | Bonus pole increment |
| Spinner | 750 | None | Bonus multiplier advance or score |
| Jet Bumper | 50 | Solenoid-4 | Rapid drain risk, eject on hit |
| Ramp 1 (Start) | 1000 | None | Ramp combo tracking |
| Ramp 2 (Mid) | 2000 | None | Ramp combo tracking |
| Ramp 3 (End) | 5000 | Solenoid-5 | Ramp complete -> Feature lit or bonus advance |
| Slingshot Left | 10 | Multiplier advance | Advance slingshot multiplier on hit |
| Slingshot Right | 10 | Multiplier advance | Advance slingshot multiplier on hit |
| Tilt | 0 | Tilt solenoid | End ball immediately, disable flipper, drain ball fast |

### Extra Ball Awards
- 100K points -> Extra ball light
- 200K points -> Extra ball light
- 300K points -> Extra ball light

### Bonus Pole (Displayed as 1K-19K)
- Incremented by Rollover switches (each +1K)
- Counted down at end of ball (1K per 50ms tick = ~20s total countdown)
- Multiplied by Return Lane multiplier (1X, 2X, 3X, or 5X)

---

## III. AUDIO ARCHITECTURE

**Authoritative Sources:**
- main_firmware.cpp lines 301-341 (sound effect enums, music track enums)
- main_firmware.cpp lines 1300-1435 (audio function definitions)
- MP3_MUSIC_SETUP.md (intended storage and policy)

### Sound Effects (13 Types)
1. Bumper hit (700 Hz, 50ms)
2. Slingshot (rising tone 400->900 Hz, 100ms)
3. Target hit (1300 Hz, 80ms)
4. Rollover (bell tone, 200ms)
5. Ball drain (descending 1000->200 Hz, 300ms)
6. Bonus counting (1000 Hz, rapid pulses 100ms each)
7. Feature lit (fanfare 2-note melody)
8. Extra ball (descending fanfare)
9. Startup tone (system boot confirmation)
10. Game start (game-start fanfare, ~2-3s)
11. Game over (game-over fanfare, ~2-3s)
12. Tilt (harsh buzzer, 500ms)
13. Switch open/close debug (low tone debug alert)

### Music Tracks (5 Roles)

| Track Role | Filename | Duration | Loop Policy | Volume |
|-----------|----------|----------|-------------|--------|
| Attract | /attract.mp3 | 3-5 min | Loop indefinitely | 0.3-0.4 (background) |
| Game Start | /game_start.mp3 | 1-3 sec | Play once | 0.8 (prominent) |
| Bonus Countdown | /bonus.mp3 | 20 sec | Play once | 0.6-0.7 |
| Game Over | /game_over.mp3 | 2-3 sec | Play once | 0.8 (prominent) |
| High Score | /hiscore.mp3 | 3-5 sec | Play once | 0.8 (prominent) |

#### Legacy Implementation Status
- Music functions defined: requestMusic(), playMusic(), updateMusicPlayer()
- Audio event matrix defined (game state -> music trigger mapping)
- NOT implemented: Music functions never called from game state machine in current main_firmware.cpp
- NOT implemented: Audio task explicitly disables MP3 playback ("MP3 playback disabled - sound effects only")

---

## IV. STORAGE BUDGET MODEL

**Calculation Basis:** ESP32 app partition ~1.31MB; W25Q128 external flash 16MB

| Component | Size | Notes |
|-----------|------|-------|
| Firmware binary | ~1.12 MB | Current code with all subsystems |
| OTA staging reserve | ~2.0 MB | Conservative; covers max firmware + overhead |
| Usable audio library | ~14 MB | Remaining for MP3 assets on W25Q128 |

### Audio Capacity (W25Q128 14MB)
- At 128 kbps: ~14-15 minutes total content
- At 96 kbps: ~19-20 minutes total content
- Recommended: Attract (3min) + 4 game tracks (30s each) = ~5 min total; leaves 9-15 min for future expansion

### Secondary Storage (SPIFFS Internal Flash)
- ~1.9 MB usable for bootstrap/fallback files
- Use for: Short sound cues, startup test tone, fallback attract track if WiFi fails

---

## V. DIAGNOSTIC SYSTEM (Bally Series II)

**Authoritative Source:** DIAGNOSTIC_TEST_MODE.md, main_firmware.cpp lines 930-1143

### Five-Step Diagnostic Sequence

#### Step 1: Logic/Program Test
- Display "600d" on 7-segment
- Duration: 3 seconds
- Purpose: Confirm CPU can reach diagnostics code

#### Step 2: Score Display Scan
- Increment display 0 -> 999999 by 111 each cycle
- Duration: ~90 seconds (cycles until test button released)
- Purpose: Verify all segment drivers and multiplexing

#### Step 3: Lamp Test
- Alternate between two groups (GROUP 1 and GROUP 2)
- GROUP 1: Bumpers, Targets, Returns, extra-ball lamps
- GROUP 2: Lanes, Spinners, Ramps, feature lamps
- 5 cycles each group, ~10 seconds total
- Purpose: Verify all lamp matrix output drivers

#### Step 4: Solenoid Test
- Fire solenoids sequentially: 0 (kickback) -> 1 (target 1) -> 2 (target 2) -> 3 (target 3) -> 4 (ramp eject)
- 1 second pulse each
- Duration: ~5 seconds total
- Purpose: Verify all solenoid driver circuits and coil continuity

#### Step 5: Switch Stuck Detection
- Poll all 22 switches for stuckness (constant press without release)
- Error code format: (Row x 10) + Column
  - Example: Row 2, Col 3 -> Error code 23
- Duration: Continuous until test button released
- Purpose: Catch stuck/shorted switch inputs before game starts

---

## VI. CRITICAL DESIGN DECISIONS - USER INPUT REQUIRED

These decisions must be locked in before code begins. Edit this section with your choices.

### DECISION 1: Audio Storage Model

**Question:** How should the firmware handle audio file storage and playback?

**Options:**
- A) Local-Only (W25Q128): All 5 MP3 tracks stored on external flash; no network dependency
- B) Hybrid (Local + WiFi Streaming): Attract mode streams unlimited songs from music_server.py on PC (LAN fallback to 1-2 pre-loaded tracks on W25Q128)
- C) Defer Streaming: Local-only now; WiFi streaming as future feature after core gameplay stabilizes

**Decision:**
```
[ ] A) Local-Only
[ ] B) Hybrid Local + WiFi Streaming
[ ] C) Defer Streaming
```

**Reasoning (user notes):**
```
[Edit here to explain choice]
```

---

### DECISION 2: Music Playback Policy

**Question:** Which game events should trigger music, and which events can interrupt?

**Options:**
- A) Attract-Mode Only: Full-song MP3 loops only during ATTRACT; no in-game background tracks
- B) Event-Triggered Full Immersion: Start event -> game-start fanfare; during BALL_IN_PLAY -> continuous dynamic background; Bonus -> bonus countdown music; Game Over -> game-over fanfare
- C) Hybrid: Attract full songs + Game Start/Game Over/Bonus events, but no continuous background during play

**Decision:**
```
Playback Scope: [ ] A) Attract-Only  [ ] B) Full Immersion  [ ] C) Hybrid Events
Interrupt on Tilt: [ ] Yes  [ ] No
Interrupt on Drain: [ ] Yes  [ ] No
```

**Reasoning (user notes):**
```
[Edit here to explain choice]
```

---

### DECISION 3: Canonical Game Logic Source

**Question:** Which legacy code repository should be the authoritative source for game scoring, state machine, and bonus logic during control-board migration?

**Current Status:**
- Authoritative: Captain-Fantastic-home-edition/src/main_firmware.cpp (fully tested, complete game parity)
- Reference Only: Captain-Fantastic-Control-and-Hat-Board/.../legacy_control_main.cpp (hardware validation, not full gameplay parity)

**Decision:**
```
Use home-edition main_firmware.cpp as authoritative source: [ ] Agree  [ ] Discuss Further

If discussing further, please note concerns:
[Edit here]
```

---

### DECISION 4: Diagnostic Parity Timeline

**Question:** Should the firmware implement the full Bally Series II diagnostic sequence immediately, or phase it after core gameplay/audio?

**Options:**
- A) Now (Full Parity): Implement all 5 diagnostic steps immediately
- B) Phased: Core diagnostics now (logic test + display scan), defer solenoid/switch tests until core gameplay stabilizes
- C) Minimal: Only logic test + display scan now; full sequence as maintenance feature after v1.0 stable

**Decision:**
```
Timeline: [ ] A) Now (Full Parity)  [ ] B) Phased (2 weeks)  [ ] C) Minimal v1.0
```

**Reasoning (user notes):**
```
[Edit here to explain choice]
```

---

## VII. NEXT STEPS (Blocked Until Decisions Locked)

Once the four decisions above are locked, code will follow this sequence:

1. Audio Module -> Integrate game-state-to-music wiring (requestMusic() called from state machine)
2. Scoring Module -> Port 22-switch scoring matrix from legacy code
3. Bonus Module -> Countdown logic and multiplier advancement
4. Diagnostics Module -> Implement chosen diagnostic scope (Decision 4)
5. Integration Test -> Verify state transitions, lamp updates, audio playback, solenoid firing

---

## Meta: Document Revision Log

| Date | Status | Changes |
|------|--------|---------|
| 2026-04-16 | Draft | Created from archaeology phase; awaiting user decisions on 4 critical questions |
| [NEXT] | Locked | [Timestamp when decisions are committed] |
