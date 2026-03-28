# W25Q64 External SPI Flash Setup Instructions
## Captain Fantastic Pinball - MP3 Storage Upgrade

---

## What You Ordered
- **Part:** W25Q64 SPI Flash Memory
- **Capacity:** 8MB (64Mbit)
- **Purpose:** Store all 5 MP3 files (2.4MB total) with room to spare
- **Interface:** SPI (Serial Peripheral Interface)

---

## Hardware Setup

### 1. Pin Connections (W25Q64 → ESP32)

```
W25Q64 Pin #  | Pin Name | Connect To        | ESP32 GPIO
-------------|----------|-------------------|------------
Pin 1        | /CS      | ESP32 GPIO32      | GPIO32
Pin 2        | DO       | ESP32 GPIO26      | GPIO26 (MISO)
Pin 3        | /WP      | 3.3V              | 3.3V rail
Pin 4        | GND      | Ground            | GND
Pin 5        | DI       | ESP32 GPIO27      | GPIO27 (MOSI)
Pin 6        | CLK      | ESP32 GPIO33      | GPIO33 (SCK)
Pin 7        | /HOLD    | 3.3V              | 3.3V rail
Pin 8        | VCC      | 3.3V              | 3.3V rail
```

### 2. Required Components
- W25Q64 chip (what you ordered)
- 0.1µF (100nF) ceramic capacitor - **REQUIRED!**
- Breadboard or PCB for mounting
- Jumper wires

### 3. Assembly Steps

**Step 1:** Mount W25Q64 on breadboard or SOIC-to-DIP adapter

**Step 2:** Connect power:
- Pin 8 (VCC) → 3.3V
- Pin 4 (GND) → GND
- **IMPORTANT:** Solder 0.1µF capacitor between pin 8 and pin 4 (as close to chip as possible)

**Step 3:** Connect control pins:
- Pin 3 (/WP - Write Protect) → 3.3V (disables write protection)
- Pin 7 (/HOLD) → 3.3V (disables hold function)

**Step 4:** Connect SPI signals:
- Pin 1 (/CS - Chip Select) → ESP32 GPIO32
- Pin 2 (DO - Data Out) → ESP32 GPIO26
- Pin 5 (DI - Data In) → ESP32 GPIO27
- Pin 6 (CLK - Clock) → ESP32 GPIO33

**Step 5:** Double-check all connections with multimeter (continuity test)

### 4. Visual Pinout Diagram

```
        W25Q64 (Top View)
        ┌─────────────┐
/CS   1 │●            │ 8  VCC (3.3V)
DO    2 │             │ 7  /HOLD (3.3V)
/WP   3 │             │ 6  CLK
GND   4 │             │ 5  DI
        └─────────────┘
         (Pin 1 marked with dot)

[0.1µF cap between pins 4 and 8]
```

---

## Software Configuration

### Code Changes Required

**1. Add SPI Flash Library**
Edit `platformio.ini` and add to `lib_deps`:
```ini
lib_deps = 
    adafruit/Adafruit LED Backpack Library
    adafruit/Adafruit BusIO
    earlephilhower/ESP8266Audio @ ^1.9.7
    SPI  # <-- Add this line
```

**2. Define SPI Pins**
In `combined_test_rtos.cpp`, update pin definitions:
```cpp
// External SPI Flash (W25Q64) - 8MB for MP3 storage
#define SPI_FLASH_MOSI  27  // GPIO27 (was COL_L4)
#define SPI_FLASH_MISO  26  // GPIO26 (was COL_L5)
#define SPI_FLASH_SCK   33  // GPIO33 (was COL_L6)
#define SPI_FLASH_CS    32  // GPIO32 (was COL_L7)
```

**3. Initialize SPI Flash**
Replace SPIFFS initialization with external flash access.

---

## Testing Procedure

### Test 1: Flash Detection
After wiring, upload test code to verify chip is detected:
- Should print: "W25Q64 detected, 8MB capacity"
- If fails: Check wiring, especially /CS, MISO, MOSI, SCK

### Test 2: Write/Read Test
- Write test pattern to flash
- Read back and verify
- Confirms chip is working properly

### Test 3: MP3 Upload
- Upload all 5 MP3 files to external flash
- Verify file sizes match originals
- Total should be ~2.4MB

### Test 4: Playback
- Test each song plays correctly
- Verify looping works
- Check audio quality

---

## Uploading MP3 Files to W25Q64

**Method 1: Via Code (Recommended)**
Files will be embedded in firmware and copied to flash on first boot.

**Method 2: Direct Upload Tool**
Use ESP32 flash download tool to write MP3s directly to specific addresses.

---

## Troubleshooting

### Problem: Chip not detected
**Checks:**
- Verify 3.3V on pin 8 (VCC)
- Verify ground on pin 4
- Check all SPI connections with multimeter
- Confirm 0.1µF capacitor installed
- Try slower SPI clock speed in code

### Problem: Random read errors
**Fixes:**
- Add shorter wires (reduce noise)
- Check decoupling capacitor is close to chip
- Verify /WP and /HOLD tied to 3.3V
- Lower SPI clock frequency

### Problem: Write fails
**Checks:**
- Confirm /WP (pin 3) connected to 3.3V (not GND!)
- Check chip isn't write-protected by previous use
- Verify power supply stable (measure with scope)

### Problem: MP3 playback stutters
**Fixes:**
- Increase SPI read buffer size
- Check for other SPI bus traffic conflicts
- Verify ESP32 running at full 240MHz
- Ensure audio task has high priority

---

## GPIO Pins Freed Up

By moving lamp columns to shift registers and using these 4 GPIOs for SPI:

**Previously:**
- GPIO27: Lamp column L4
- GPIO26: Lamp column L5  
- GPIO33: Lamp column L6
- GPIO32: Lamp column L7

**Now:**
- GPIO27: SPI MOSI (flash)
- GPIO26: SPI MISO (flash)
- GPIO33: SPI SCK (flash)
- GPIO32: SPI CS (flash)

**Lamp columns now driven by:** 74HC595 shift register (part of solenoid+lamp 16-bit register chain)

---

## File Storage Layout on W25Q64

```
Address Range    | Size   | Content
-----------------|--------|---------------------------
0x000000-0x07FFFF | 512KB | rocket_man.mp3 (482KB)
0x080000-0x0FFFFF | 512KB | crocodile_rock.mp3 (363KB)
0x100000-0x17FFFF | 512KB | pinball_wizard.mp3 (535KB)
0x180000-0x1FFFFF | 512KB | bennie_jets.mp3 (485KB)
0x200000-0x27FFFF | 512KB | yellow_brick.mp3 (556KB)
0x280000-0x7FFFFF | 5.6MB | WiFi Stream Buffer (see below)
```

Total MP3 storage: ~2.4MB
Remaining space: ~5.6MB

### WiFi Stream Buffer Strategy

**Using the leftover 5.6MB for WiFi streaming buffer:**

Instead of wasting the remaining flash space, use it as a **cache/buffer** for WiFi streaming:

**Benefits:**
- **Dropout protection:** If WiFi hiccups, playback continues from buffer
- **Seamless playback:** Preload while playing, no gaps between songs
- **Faster response:** Cached songs load instantly
- **Hybrid mode:** 5 favorites in permanent storage + unlimited streaming from PC

**How It Works:**
1. WiFi streams MP3 from PC music server (http://192.168.0.60:8000/)
2. ESP32 writes incoming data to flash buffer (0x280000-0x7FFFFF)
3. MP3 decoder reads from flash buffer (not WiFi directly)
4. If WiFi drops: Buffer keeps feeding decoder for ~5 minutes
5. When WiFi returns: Resume download to buffer

**Buffer Capacity:**
- 5.6MB at 128kbps MP3 = ~5.8 minutes of buffered audio
- Enough to survive WiFi glitches, router reboots, microwave interference

**Implementation Modes:**

**Mode 1: Fixed Songs (Current)**
- 5 MP3s stored permanently in flash (2.4MB)
- Buffer unused
- No WiFi required

**Mode 2: WiFi Streaming with Buffer**
- Stream unlimited songs from PC
- Use 5.6MB as circular buffer
- Playback immune to WiFi drops

**Mode 3: Hybrid (Best of Both)**
- 5 favorite songs in permanent flash (instant access)
- Stream additional songs from PC with buffer
- Fall back to local songs if WiFi fails

This is exactly how Spotify/YouTube work - you just invented embedded streaming!

**Code Implementation:** Ready when W25Q64 arrives and tested.

---

## Future Expansion Ideas

With 5.6MB free, you could add:
- More songs (10-12 additional MP3s)
- Sound effects library
- Voice callouts
- Game rule configurations
- High score persistent storage
- Display animations/graphics

---

## When It Arrives - Checklist

- [ ] Inspect chip for physical damage
- [ ] Identify pin 1 (marked with dot or notch)
- [ ] Mount on breadboard or adapter
- [ ] Solder 0.1µF capacitor between VCC and GND
- [ ] Wire power (3.3V and GND)
- [ ] Wire control pins (/WP and /HOLD to 3.3V)
- [ ] Wire SPI signals (CS, MISO, MOSI, SCK)
- [ ] Double-check all connections with multimeter
- [ ] Notify me - I'll provide updated code
- [ ] Upload test firmware
- [ ] Verify chip detection
- [ ] Upload MP3 files
- [ ] Test playback
- [ ] Rock out to Elton John! 🎸🚀

---

## Contact

When your W25Q64 arrives, let me know and I'll:
1. Provide the updated code with SPI flash support
2. Help with any wiring questions
3. Walk through the testing procedure
4. Upload all 5 MP3 files
5. Get your pinball machine playing full-quality music!

---

**Estimated Time:**
- Wiring: 15-20 minutes
- Code update: 5 minutes
- Testing: 10 minutes
- MP3 upload: 5 minutes
- **Total: ~40 minutes to full music playback**
