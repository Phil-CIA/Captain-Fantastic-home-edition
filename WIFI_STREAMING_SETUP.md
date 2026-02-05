# Captain Fantastic - WiFi Music Streaming Setup

## Overview
Your pinball machine now has **hybrid audio storage**:
- **WiFi Streaming** → Attract mode music (unlimited songs!)
- **SPIFFS (1.9MB)** → Critical game sounds (instant response)

## Quick Start (5 Minutes)

### Step 1: Start the Music Server on Your PC

```powershell
# Navigate to project folder
cd "C:\Users\user\Esp32 projects VScode\Captain-Fantastic-home-edition"

# Create music folder (if it doesn't exist)
New-Item -ItemType Directory -Force -Path "music"

# Copy your MP3 files to the music folder
Copy-Item "C:\Users\user\OneDrive\Pinball graphics\Captain Fabtastic\Audio files\Project clips\*.mp3" -Destination "music\"

# Start the Python music server
python music_server.py
```

**Expected output:**
```
Found 5 MP3 file(s):
  - bennie_jets.mp3 (473.2 KB)
  - crocodile_rock.mp3 (354.4 KB)
  - pinball_wizard.mp3 (522.9 KB)
  - rocket_man.mp3 (482.1 KB)
  - yellow_brick.mp3 (542.6 KB)  ← NOW AVAILABLE!

============================================================
  CAPTAIN FANTASTIC - MUSIC SERVER
============================================================
Server running at: http://192.168.1.XXX:8000/
Music directory:   C:\Users\user\Esp32 projects VScode\Captain-Fantastic-home-edition\music

ESP32 Configuration:
  1. Update MUSIC_SERVER_IP to: "192.168.1.XXX"
  2. ESP32 will stream from: http://192.168.1.XXX:8000/music/
```

**IMPORTANT:** Note the IP address shown (e.g., `192.168.1.XXX`)

---

### Step 2: Update ESP32 Code with Your PC's IP

Open `src/combined_test_rtos.cpp` and find this line (around line 81):

```cpp
const char* MUSIC_SERVER_IP = "192.168.1.100";  // TODO: Update to your PC's IP address
```

**Change it to your PC's actual IP** (from Step 1):

```cpp
const char* MUSIC_SERVER_IP = "192.168.1.XXX";  // Replace XXX with your IP
```

---

### Step 3: Upload Firmware

```powershell
# Compile and upload
platformio run --target upload --environment combined_rtos

# Open serial monitor
platformio device monitor --baud 115200
```

---

## Expected Serial Output (Success!)

```
[WiFi] Connecting to network...
[WiFi] SSID: forche
..................
[WiFi] Connected!
[WiFi] IP Address: 192.168.1.150
[WiFi] Signal: -45 dBm
[WiFi] Music server: http://192.168.1.XXX:8000/

[MP3] Initializing music system...
[MP3] SPIFFS mounted successfully
[MP3] SPIFFS capacity: 1966080 bytes (1.87 MB)
[MP3] SPIFFS used: 1875968 bytes (1.79 MB)
[MP3] SPIFFS free: 90112 bytes (0.09 MB)
[MP3] Audio system ready

[AUDIO TEST] Starting continuous MP3 playback...
[AUDIO TEST] Using WiFi streaming (5-song playlist)
[MP3] Streaming: http://192.168.1.XXX:8000/music/pinball_wizard.mp3
[MP3] Streaming started (WiFi)

♪ Now playing: Pinball Wizard ♪
```

---

## How It Works

### WiFi Connected → 5-Song Playlist (Unlimited Quality)
```
Attract Mode Tracks (WiFi Streaming):
1. Pinball Wizard      ← From your PC
2. Crocodile Rock      ← From your PC
3. Bennie and the Jets ← From your PC
4. Rocket Man          ← From your PC
5. Yellow Brick Road   ← NEW! (5th song now available!)
```

### WiFi Down → 4-Song Fallback (SPIFFS)
```
Attract Mode Tracks (Local Storage):
1. Pinball Wizard      ← From SPIFFS
2. Crocodile Rock      ← From SPIFFS
3. Bennie and the Jets ← From SPIFFS
4. Rocket Man          ← From SPIFFS
(Yellow Brick Road unavailable - doesn't fit in 1.9MB SPIFFS)
```

### Game Sounds (Always Local - Zero Latency)
```
Critical sounds remain on SPIFFS for instant response:
- Bumper sounds
- Slingshot sounds
- Target hits
- Rollover lanes
- Bonus countdown
```

---

## Troubleshooting

### WiFi Won't Connect
```
[WiFi] Connection failed!
[WiFi] Will use SPIFFS for all music (limited library)
```

**Fix:**
1. Check SSID: `"forche"` (case-sensitive)
2. Check password: `"gizmoa22"`
3. ESP32 antenna range (~30 feet typical)
4. Router 2.4GHz enabled (ESP32 doesn't support 5GHz)

---

### "Stream Failed" Errors
```
[MP3] ERROR: Failed to start streaming
```

**Fix:**
1. Make sure Python server is running on your PC
2. Check ESP32 can ping your PC: `ping 192.168.1.XXX`
3. Update `MUSIC_SERVER_IP` in code to correct IP address
4. Check firewall (Windows may block Python HTTP server)

**Test server from browser:**
- Open: `http://YOUR_PC_IP:8000/music/pinball_wizard.mp3`
- Should download/play the MP3

---

### Music Cuts Out During Gameplay
**This is NORMAL!** WiFi streaming uses ~200-500ms buffering. During attract mode (idle), this is fine. For game sounds, we use SPIFFS (instant).

---

## Advanced: Adding More Songs

### Option 1: Add to WiFi Streaming (Easy!)

1. Copy new MP3 files to `music/` folder on your PC
2. Update `attractPlaylistWiFi[]` in code:

```cpp
const char* attractPlaylistWiFi[] = {
    "pinball_wizard.mp3",
    "crocodile_rock.mp3",
    "bennie_jets.mp3",
    "rocket_man.mp3",
    "yellow_brick.mp3",
    "your_new_song.mp3"  // ← Add here!
};
```

3. Update playlist size: `const int attractPlaylistSize = 6;`
4. Restart Python server
5. Upload firmware

**No storage limit!** Add as many songs as you want.

---

### Option 2: Add to SPIFFS (Limited to ~1.9MB)

Only do this for critical game sounds that need instant response.

See `SPIFFS_UPLOAD.md` for instructions.

---

## Performance Stats

### WiFi Streaming
- **Latency:** 200-500ms startup (buffering)
- **Bandwidth:** ~48 kbps MP3 = 6 KB/s (negligible on gigabit)
- **Quality:** Can use high bitrate (96-320 kbps) without storage worry
- **Library:** Unlimited songs
- **Reliability:** Falls back to SPIFFS if WiFi drops

### SPIFFS Local
- **Latency:** <1ms startup (instant)
- **Storage:** 1.9MB usable (~4 songs at 48 kbps)
- **Quality:** Limited to 48 kbps (compressed)
- **Library:** Fixed 4 songs
- **Reliability:** Always works (no network needed)

---

## Network Requirements

**Minimum:**
- WiFi signal: -70 dBm or better
- Bandwidth: 10 KB/s per ESP32
- Latency: <100ms to music server

**Your Setup (Excellent!):**
- ✅ Strong WiFi
- ✅ Gigabit backbone
- ✅ Low latency local network

**You're good to go!** WiFi streaming will work flawlessly.

---

## Commands

### Start Music Server (Windows PowerShell)
```powershell
cd "C:\Users\user\Esp32 projects VScode\Captain-Fantastic-home-edition"
python music_server.py
```

### Upload ESP32 Firmware
```powershell
platformio run --target upload --environment combined_rtos
```

### Monitor Serial Output
```powershell
platformio device monitor --baud 115200
```

### Find Your PC's IP Address
```powershell
ipconfig | Select-String "IPv4"
```

---

## Next Steps

Once WiFi streaming works, you can:

1. **Add more Elton John songs** (unlimited library!)
2. **Use higher quality MP3s** (96 kbps, 128 kbps, even 320 kbps)
3. **Organize playlists** (different songs for different game modes)
4. **Add speech samples** ("Extra ball!", "Tilt!", etc.)
5. **Stream from NAS** (if you have one on your network)

Enjoy your unlimited music library! 🎵📡
