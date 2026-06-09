# Audio and Game Mode Mapping

## Summary
The game already has distinct mode names and a partial music system, but the intended audio behavior is not yet settled into one canonical policy. This issue tracks how audio should map onto the cabinet's startup and gameplay modes using the existing Elton-related clips and the current firmware/docs history.

## Current Mode Vocabulary
The control firmware already distinguishes these gameplay modes:
- attract
- serve ball
- ball in play
- bonus countdown
- game over

That vocabulary should stay consistent across the audio discussion so the issue does not introduce a second, conflicting set of mode names.

## Current Audio State
The repo already has music plumbing in `src/main_firmware.cpp`:
- `MusicTrack` exists with entries for attract, start, bonus, game over, and high score.
- `getMusicFilename()` currently maps several tracks to a small set of local MP3 files.
- `audioMP3Task()` still reports MP3 playback as disabled in favor of sound effects only.

The docs also describe a fuller MP3 plan:
- `docs/MP3_MUSIC_SETUP.md` describes attract, start, bonus, game over, and hiscore files.
- `docs/WIFI_STREAMING_SETUP.md` describes a five-song attract playlist and a WiFi streaming path with local fallback.
- `docs/SYSTEM_BEHAVIOR_CONTRACT.md` still treats music playback policy as an open decision.

## Local Audio Assets
The local `music/` folder currently contains the following Elton-related files:
- `pinball_wizard.mp3`
- `crocodile_rock.mp3`
- `bennie_jets.mp3`
- `rocket_man.mp3`
- `yellow_brick.mp3`
- `Tiny dancer clip.mp3`

This is enough to start designing the mode-to-audio mapping, but not enough to claim a final assignment for every clip.

## Planning Assumptions For This Issue
This issue should start from the following scope decisions:
- Full-song playback is attract-only for now.
- In-game continuous background music is deferred.
- WiFi streaming is deferred for the first implementation pass.
- Game start, bonus, and game over can still become future event hooks, but they are not part of the first delivery.  

## First-Pass Mapping Proposal
This is the working mapping to use for the issue thread until we validate it on the machine:

| Game mode | Proposed audio behavior | Proposed local clip |
| --- | --- | --- |
| Startup / power-up | Short startup sting, not a full song | `Tiny dancer clip.mp3` or existing tone-based startup cue |
| Attract / idle | Full-song rotation | `pinball_wizard.mp3`, `crocodile_rock.mp3`, `bennie_jets.mp3`, `rocket_man.mp3`, `yellow_brick.mp3` |
| Start / new game | One-shot fanfare | `rocket_man.mp3` |
| Ball in play | No continuous background music in phase 1 | None |
| Bonus countdown | Short game-event cue | `crocodile_rock.mp3` |
| Game over | End-of-game cue | `yellow_brick.mp3` |

The mapping above is intentionally conservative: it preserves attract-only full songs, uses the current Elton-related files already in the repo, and leaves gameplay background music for a later pass.

## Open Questions
The issue should explicitly leave these items open for the next pass:
- Which clip, if any, should represent startup versus attract?
- Should `Tiny dancer clip.mp3` be treated as a special-purpose clip, a placeholder attract track, or excluded from the first mapping?
- Should bonus and game over use short cues first, or full tracks later?
- Should the local track names be normalized before any code wiring begins?

## Recommended Next Step
Before any code change, define a simple mode-to-track table and a canonical asset naming scheme so the audio path can be wired once without revisiting the same mapping decision repeatedly.

## Acceptance Criteria For The Issue
- The issue documents the current mode names and current audio behavior without inventing new states.
- The issue distinguishes implemented behavior from documented intent.
- The issue records the current local music inventory, including the extra Tiny Dancer clip.
- The issue captures the current planning assumption that attract-only playback is the near-term target and streaming is deferred.

## Related Files
- `Captain-v2/src/control_main.cpp`
- `src/main_firmware.cpp`
- `docs/MP3_MUSIC_SETUP.md`
- `docs/WIFI_STREAMING_SETUP.md`
- `docs/SYSTEM_BEHAVIOR_CONTRACT.md`
- `HANDOFF_2026-06-05.md`