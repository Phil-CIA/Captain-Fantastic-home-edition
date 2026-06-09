# Sound Model - Current Firmware Behavior

## Status
Draft documentation for the current code path. This is a documentation-only pass and is intentionally separate from any future runtime branch work.

## What The Sound System Does Today
The current firmware uses two different audio paths:

1. **Short sound effects** run through the DAC on GPIO25 and are queued through the audio task.
2. **Music playback plumbing** exists in code, but MP3 playback is still disabled in the current runtime path.

That means the machine currently behaves like a classic pinball sound system first, with MP3 music support present in the source but not yet enabled as a normal gameplay feature.

## Short Sound Effects Path
The control firmware defines a queue-driven sound effect system:

- `triggerSound()` queues sound events from gameplay logic.
- `audioMP3Task()` consumes those sound events.
- The boot path plays `soundStartup()`.
- Gameplay can trigger sounds for bumper hits, slingshots, targets, drains, bonus counting, and switch debug events.

Current sound effect hooks in `src/main_firmware.cpp` include:
- `soundStartup()`
- `soundCoinInsert()`
- `soundGameStart()`
- `soundGameOver()`
- `soundBumperHit()`
- `soundSlingshotHit()`
- `soundTargetHit()`
- `soundRolloverHit()`
- `soundDrain()`
- `soundBonusCount()`
- `soundSpecial()`

The sound effect output is centered around the ESP32 DAC on GPIO25 and the LM384N amplifier path documented elsewhere in the repo.

## Music Path In The Source
Music support is already defined in `src/main_firmware.cpp`, but it is not yet wired into the active gameplay state machine.

The current music-related pieces are:
- `MusicTrack` enum with attract, start, bonus, game over, and high score roles.
- `getMusicFilename()` for mapping roles to local MP3 file names.
- `playMusic()` for starting a track.
- `updateMusicPlayer()` for continuing or looping playback.
- `requestMusic()` for asking the audio task to change tracks.

Current mapping in code:
- `MUSIC_ATTRACT` -> `/rocket_man.mp3`
- `MUSIC_START` -> `/crocodile_rock.mp3`
- `MUSIC_BONUS` -> `/crocodile_rock.mp3`
- `MUSIC_GAMEOVER` -> `/rocket_man.mp3`
- `MUSIC_HISCORE` -> `/rocket_man.mp3`

Important limitation:
- The audio task currently prints that MP3 playback is disabled and runs the sound-effect path only.
- There is no live state-machine call path in the current runtime that automatically requests music when the game enters attract, start, bonus, or game-over states.

## How Game Code Uses Sound
Game logic already calls sound effects at specific events:

- Startup test and boot-related paths call `soundStartup()`.
- Start of play can call `soundGameStart()`.
- Ball drain and bonus countdown logic call `triggerSound()` for drain and bonus events.
- Game-over logic can call `soundGameOver()`.

This is the important split:
- **Sound effects** are already event-driven.
- **Music** is still a defined subsystem waiting for a policy decision and wiring into the state machine.

## Current Model Summary
The current sound model is:

- short effects for immediate feedback,
- music support in source for later use,
- attract/game event mapping planned but not yet activated,
- no continuous gameplay background music in the current runtime path.

## What Should Stay Separate
This doc is intentionally separate from the next branch of implementation work.

When code work resumes, keep the following separate concerns separate:
- sound effects vs music playback,
- current runtime behavior vs planned track mapping,
- documentation work vs branch implementation.

## Related Files
- `src/main_firmware.cpp`
- `docs/MP3_MUSIC_SETUP.md`
- `docs/WIFI_STREAMING_SETUP.md`
- `docs/SOUND_SYSTEM.md`
- `docs/SYSTEM_BEHAVIOR_CONTRACT.md`