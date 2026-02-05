# MP3 Music System - Captain Fantastic Edition

## Overview
Your pinball machine now supports full MP3 playback for Elton John themed background music! The system uses the ESP32's built-in I2S DAC to decode MP3 files stored in flash memory (SPIFFS).

## Hardware
- **No changes needed!** Uses existing GPIO25 DAC → LM384N amplifier
- MP3 decoder runs in software on ESP32
- Music and sound effects can play simultaneously

## How It Works
1. **MP3 files** stored in SPIFFS (ESP32 flash file system)
2. **ESP8266Audio library** decodes MP3 in real-time
3. **I2S DAC** outputs audio on GPIO25 (same pin as your existing sound effects)
4. **LM384N amplifier** amplifies to speaker
5. **Sound task** continuously feeds audio buffer while processing game sounds

## Required MP3 Files

Store these files in your project's `data/` folder:

### 1. `/attract.mp3` - Attract Mode (Auto-loops)
- **Suggested:** Captain Fantastic theme or piano instrumental
- **Length:** 30-60 seconds
- **Volume:** Moderate (machine at rest)
- **Purpose:** Plays continuously when game is idle

### 2. `/start.mp3` - Game Start Fanfare
- **Suggested:** Rocket Man intro or original fanfare
- **Length:** 5-10 seconds
- **Volume:** Loud and exciting
- **Purpose:** Plays when coin inserted / start button pressed

### 3. `/bonus.mp3` - Bonus Countdown
- **Suggested:** Piano melody or Crocodile Rock rhythm
- **Length:** 10-15 seconds
- **Volume:** Medium-high
- **Purpose:** Plays during end-of-ball bonus collection

### 4. `/gameover.mp3` - Game Over
- **Suggested:** Gentle Elton ballad conclusion
- **Length:** 8-12 seconds
- **Volume:** Medium
- **Purpose:** Final score display sequence

### 5. `/hiscore.mp3` - High Score Achievement
- **Suggested:** Triumphant fanfare (Captain theme)
- **Length:** 10-15 seconds
- **Volume:** Maximum celebration!
- **Purpose:** New high score (future feature)

## Audio Format Specifications

For best performance on ESP32:

```
Format:       MP3 (MPEG-1 Layer 3)
Sample Rate:  22050 Hz or 44100 Hz
Bit Rate:     96-128 kbps (stereo will be mixed to mono)
Channels:     Mono preferred (stereo works but uses more memory)
File Size:    Keep under 2MB each for SPIFFS storage
```

## How to Upload MP3 Files to ESP32

### Step 1: Create `data` Folder
```
Captain-Fantastic-home-edition/
  ├─ src/
  ├─ include/
  ├─ platformio.ini
  └─ data/           ← Create this folder
      ├─ attract.mp3
      ├─ start.mp3
      ├─ bonus.mp3
      ├─ gameover.mp3
      └─ hiscore.mp3
```

### Step 2: Build Filesystem Image
In VS Code terminal:
```powershell
pio run --target buildfs
```

### Step 3: Upload to ESP32 SPIFFS
```powershell
pio run --target uploadfs
```

**⚠️ WARNING:** This erases and rebuilds SPIFFS - don't do this while testing unless needed!

### Step 4: Verify Files Uploaded
After boot, serial monitor will show:
```
[AUDIO] Initializing MP3 player...
      SPIFFS mounted successfully
      Available music files:
        - /attract.mp3 (456789 bytes)
        - /start.mp3 (123456 bytes)
        ...
```

## Testing Music Playback

### Serial Commands
```
m - Play attract mode music (loops)
M - Stop music
1 - Game start music
2 - Bonus countdown music  
3 - Game over music
4 - High score music
```

### Automatic Playback
- **Power-on:** Attract mode starts automatically after boot
- **Coin/Start:** Stops attract, plays game start music
- **Ball drain:** Bonus countdown plays during bonus collection
- **Game end:** Game over music, then returns to attract mode

## Volume Control

Adjust in code (`combined_test_rtos.cpp`, line ~260):

```cpp
out->SetGain(0.5);  // 0.0 (silent) to 1.0 (full volume)
```

**Recommended settings:**
- Attract mode: 0.3-0.4 (quiet background)
- Game play: 0.5-0.7 (exciting but not overwhelming)
- High score: 0.8-1.0 (celebration!)

You can also adjust per-track before calling `playMusic()`:
```cpp
if (out) out->SetGain(0.3);  // Quiet for attract mode
playMusic(MUSIC_ATTRACT);
```

## Memory Considerations

**SPIFFS Partition Size:** Check `platformio.ini` for partition scheme:
```ini
board_build.partitions = min_spiffs.csv  ; Less SPIFFS
board_build.partitions = default.csv      ; Balanced (recommended)
board_build.partitions = huge_app.csv     ; More program space
```

**Typical storage:**
- Code: ~500KB
- SPIFFS available: 1.5-2MB (default partition)
- 5 MP3 files @ 128kbps, 10s each: ~1.2MB
- **Fits comfortably!**

## Legal / Copyright Notes

### Public Domain / Creative Commons
✅ Safe to use royalty-free music
✅ Original compositions "in the style of" Elton John
✅ Short clips may qualify as fair use (check local laws)

### Commercial Recordings
❌ Full Elton John songs are copyrighted
⚠️ For personal/home use only
⚠️ Do NOT distribute ROM images with copyrighted music
⚠️ Consider licensing if machine is commercial/public

### Recommended Sources
- **FreePD.com** - Public domain music
- **Incompetech.com** - Royalty-free (Kevin MacLeod)
- **Musopen.org** - Classical public domain
- **Your own compositions!** - Record piano versions

## Converting Audio Files

Use **Audacity** (free) to prepare files:

1. Import your audio
2. **Project Rate:** 22050 Hz (lower) or 44100 Hz
3. **Convert to Mono:** Tracks → Mix → Mix Stereo Down to Mono
4. **Normalize:** Effect → Normalize (-1.0 dB)
5. **Trim silence:** Effect → Truncate Silence
6. **Export:** File → Export Audio
   - Format: MP3
   - Bit rate: 96-128 kbps

## Troubleshooting

### "SPIFFS mount failed"
- Need to upload filesystem first: `pio run --target uploadfs`
- Check partition scheme in `platformio.ini`

### "File not found" when playing music
- Verify filename matches exactly (case-sensitive on Linux)
- Check SPIFFS file list in serial monitor at boot
- Filenames must start with `/` (e.g., `/attract.mp3`)

### Crackling / Distortion
- Reduce `SetGain()` to 0.3-0.5
- Check MP3 bit rate (too high = decoding struggles)
- Try 22050 Hz sample rate instead of 44100 Hz
- Ensure LM384N isn't clipping (see `SOUND_SYSTEM.md`)

### Music stops abruptly
- File might be corrupted - re-encode
- MP3 header issues - use constant bit rate (CBR), not variable (VBR)
- Try smaller file size

### Sound effects not working during music
- Both should work! Music plays via I2S, effects via DAC writes
- If conflict occurs, adjust sound task timing
- Effects are prioritized (interrupt music briefly)

### Out of memory
- Reduce MP3 file sizes (lower bit rate or sample rate)
- Use mono instead of stereo
- Delete unused files from SPIFFS

## Code Integration Points

### Where music is triggered:
- `setup()` - Attract mode starts after boot
- `startGame()` - Game start music
- `bonusCountdown()` - Bonus countdown music
- `endGame()` - Game over, then back to attract

### Modify playback behavior:
Edit these functions in `combined_test_rtos.cpp`:
- `playMusic()` - Change filenames or add new tracks
- `updateMusicPlayer()` - Auto-loop logic
- `soundTask()` - Buffer update frequency

## Future Enhancements

1. **Context-aware volume:** Louder during multiball, quieter at night
2. **Multi-track attract mode:** Cycle through multiple songs
3. **Player-specific themes:** Different music per player
4. **Dynamic mixing:** Fade music when playing sound effects
5. **High score names:** Record player voice callouts
6. **Ball-specific music:** Different themes per ball number

## Performance Notes

✅ **MP3 decoding:** ~30-40% CPU on one core  
✅ **I2S DMA:** Hardware handles audio output, minimal CPU  
✅ **Sound effects:** Still work via direct DAC writes  
✅ **FreeRTOS tasks:** Music runs on Core 0, game logic on Core 1  
✅ **No lag:** Matrix scanning unaffected (Core 1, high priority)

---

**Status:** Implemented and ready for testing  
**Last Updated:** December 6, 2025  
**Dependencies:** ESP8266Audio library v1.9.7+  
**Storage:** SPIFFS (ESP32 flash)  
**Output:** GPIO25 (DAC1) → I2S → LM384N amplifier
