# Display System - Captain Fantastic Home Edition

## Overview

**NEW ARCHITECTURE: Virtual Display Buffer System**

The display system uses a **pixel/segment matrix approach** to manage 5 physical 4-digit displays as if they were 6 vintage 7-segment displays with 6 digits each.

### Key Innovation
- **30-digit virtual buffer** (5 displays × 6 virtual digits each)
- Game logic writes segment patterns to virtual buffer
- Display refresh routine slices buffer chunks to physical 4-digit displays
- Each display shows 4 of its 6 allocated digits (scrollable)

**This mimics how original pinball machines worked internally!**

See detailed documentation:
- 📘 **[VIRTUAL_DISPLAY_BUFFER.md](VIRTUAL_DISPLAY_BUFFER.md)** - Complete architecture explanation
- 📋 **[DISPLAY_BUFFER_REFERENCE.md](DISPLAY_BUFFER_REFERENCE.md)** - Quick reference guide

## Display Configuration

## Hardware Requirements

### I2C Multiplexer: Adafruit TCA9548A
- **Default Address**: 0x70 (can be changed to 0x71-0x77 with solder jumpers)
- **Channels Used**: 0-4 (out of 8 available)
- **Footprint**: 24-pin DIP (12 pins per side)
- **Power**: 5V via VIN pin (same external supply as MCP23017)
- **Datasheet**: [TCA9548A](https://www.ti.com/product/TCA9548A)

### Pin Configuration
```
Adafruit TCA9548A Multiplexer:
Left Side (12 pins):          Right Side (12 pins):
1.  VIN  (5V power)          13. SC7 (Channel 7 SCL)
2.  GND                      14. SD7 (Channel 7 SDA)
3.  SC0  (Channel 0 SCL)     15. SC6 (Channel 6 SCL)
4.  SD0  (Channel 0 SDA)     16. SD6 (Channel 6 SDA)
5.  SC1  (Channel 1 SCL)     17. SC5 (Channel 5 SCL)
6.  SD1  (Channel 1 SDA)     18. SD5 (Channel 5 SDA)
7.  SC2  (Channel 2 SCL)     19. SC4 (Channel 4 SCL)
8.  SD2  (Channel 2 SDA)     20. SD4 (Channel 4 SDA)
9.  SC3  (Channel 3 SCL)     21. SCL (Main I2C SCL - to ESP32 GPIO22)
10. SD3  (Channel 3 SDA)     22. SDA (Main I2C SDA - to ESP32 GPIO21)
11. A0   (Address select)    23. RESET (Pull HIGH or leave open)
12. A1   (Address select)    24. A2 (Address select)
```

### Address Selection
TCA9548A base address is **0x70**. Address can be modified:
- A0, A1, A2 pulled LOW = 0x70 (default)
- A0 HIGH, A1/A2 LOW = 0x71
- A1 HIGH, A0/A2 LOW = 0x72
- etc.

**IMPORTANT**: Displays must use addresses OTHER than 0x70 to avoid conflict!

## Display Options

### Option 1: 7-Segment LED Displays (Recommended for Pinball)
**Adafruit 0.56" 4-Digit 7-Segment Display w/ HT16K33**
- **Display Driver**: HT16K33
- **Default Address**: 0x70 ⚠️ CONFLICTS with multiplexer!
  - **Solution**: Change display address to 0x71 using solder jumpers
- **Interface**: I2C
- **Digits**: 4 digits + colon (good for scores up to 9999)
- **Brightness**: 16 levels (0-15)
- **Libraries**: 
  - `Adafruit_GFX`
  - `Adafruit_LEDBackpack`

**Wiring per display:**
```
Display --> Multiplexer Channel (e.g., SC0/SD0)
VIN --> 5V
GND --> GND
```

### Option 2: OLED Displays
**SSD1306 128x64 OLED Display**
- **Default Address**: 0x3C or 0x3D (no conflict!)
- **Advantages**: Can show text, graphics, animations
- **Disadvantages**: Slower refresh, more complex code
- **Library**: `Adafruit_SSD1306`

### Option 3: LCD Displays
**I2C LCD 16x2 or 20x4**
- **Default Address**: 0x27 or 0x3F (no conflict!)
- **Advantages**: Large text area, cheap
- **Disadvantages**: Slow, limited refresh rate

## Channel Assignments

```cpp
Channel 0: Player 1 Score Display
Channel 1: Player 2 Score Display
Channel 2: Player 3 Score Display
Channel 3: Player 4 Score Display
Channel 4: Bonus/Ball Display
Channels 5-7: Available for expansion
```

## Software Architecture

### Files Created
```
include/
  displayMux.h      - TCA9548A multiplexer control
  displays.h        - High-level display management

src/
  displayMux.cpp    - Multiplexer implementation
  displays.cpp      - Display logic (framework)
  main_display_test.cpp - Test program
```

### Key Functions

#### Multiplexer Control (`displayMux.cpp`)
```cpp
void initMux();                      // Initialize multiplexer
bool selectMuxChannel(uint8_t ch);   // Select channel 0-7
void disableMuxChannels();           // Disable all channels
bool scanMuxChannels();              // Scan for connected devices
```

#### Display Management (`displays.cpp`)
```cpp
void initDisplays();                 // Initialize all displays
void updateAllDisplays();            // Update all displays

// Score control
void setPlayerScore(uint8_t player, uint32_t score);
void addToPlayerScore(uint8_t player, uint32_t points);

// Game state
void setCurrentPlayer(uint8_t player);
void setBallNumber(uint8_t ball);
void setBonusValue(uint16_t bonus);

// Display control
void setDisplayBrightness(uint8_t brightness);
void clearAllDisplays();
void testDisplaySequence();
```

## I2C Bus Management

The ESP32 has **ONE** I2C bus shared between:
1. **MCP23017** (address 0x20) - Solenoid control
2. **TCA9548A** (address 0x70) - Display multiplexer
3. **5 Displays** (addresses 0x71 or 0x3C, etc.) - via multiplexer channels

### Bus Conflict Prevention
- **MCP23017**: Fast updates needed for solenoids (priority)
- **Displays**: Slow updates acceptable (100ms intervals)
- **Solution**: FreeRTOS task scheduling or careful timing in loop()

### Recommended I2C Speed
```cpp
Wire.setClock(100000);  // 100kHz - reliable for all devices
```

## Testing Without Hardware

The current code is a **framework** that will:
1. ✅ Initialize I2C bus
2. ✅ Attempt to communicate with TCA9548A at 0x70
3. ✅ Scan all multiplexer channels
4. ✅ Print debug information to Serial
5. ⚠️ Show warnings if hardware not detected

When you connect the actual hardware:
- Warnings will disappear
- Scan will show detected devices
- You'll need to add display-specific library code

## Next Steps - When You Have Hardware

### Step 1: Connect Multiplexer
1. Connect TCA9548A VIN to external 5V supply (same as MCP23017)
2. Connect GND to common ground
3. Connect SDA to ESP32 GPIO21
4. Connect SCL to ESP32 GPIO22
5. Leave RESET pin open (has internal pull-up)

### Step 2: Test Multiplexer
```cpp
// Rename main_display_test.cpp to main.cpp (remove current main.cpp)
// Upload and check Serial Monitor
// Should see: "✓ TCA9548A found at address 0x70"
```

### Step 3: Connect First Display
1. Connect display to Channel 0 (SC0/SD0)
2. Power display from 5V/GND
3. **Change display address to 0x71** (if using HT16K33)
4. Run scan - should see device on Channel 0

### Step 4: Add Display Library
For HT16K33 7-segment displays:
```cpp
// Add to platformio.ini:
lib_deps = 
    adafruit/Adafruit GFX Library
    adafruit/Adafruit LED Backpack Library

// In displays.cpp, add:
#include <Adafruit_LEDBackpack.h>

Adafruit_7segment display = Adafruit_7segment();

// In updatePlayerDisplay():
selectMuxChannel(channel);
display.begin(0x71);  // Display address
display.print(score);
display.writeDisplay();
disableMuxChannels();
```

### Step 5: Complete Implementation
Replace TODO comments in `displays.cpp` with actual display commands.

## Troubleshooting

### "TCA9548A NOT found at address 0x70"
- Check wiring (SDA/SCL correct?)
- Verify 5V power to VIN pin
- Check I2C pull-up resistors (4.7kΩ already on bus from MCP23017)
- Try I2C scanner sketch to verify address

### "No devices found" on scan
- Displays not connected yet (expected)
- OR displays connected but powered off
- OR wrong I2C address on displays

### Display shows nothing
- Check display power (5V/GND)
- Verify display I2C address (not 0x70!)
- Check channel selection in code matches physical wiring
- Try increasing brightness: `setDisplayBrightness(15);`

### I2C bus lockup / frozen ESP32
- Bus conflict between MCP23017 and multiplexer
- Add delays between I2C operations
- Use FreeRTOS tasks with proper mutex/semaphore
- Ensure disableMuxChannels() called after each use

## Integration with Solenoid System

The solenoid system (`Solenoid.cpp`) uses the same I2C bus:
```cpp
// Both systems share:
#define SDA_PIN 21
#define SCL_PIN 22
Wire.begin(SDA_PIN, SCL_PIN);
```

**To run both systems together:**
1. Call `Wire.begin()` only once in setup()
2. Initialize solenoids first (higher priority)
3. Initialize displays second
4. In loop(): Update solenoids frequently, displays slowly
5. Never write to MCP23017 while multiplexer channel is active

## Future Enhancements

- [ ] Add display-specific library integration
- [ ] Implement FreeRTOS display update task
- [ ] Add I2C mutex for bus arbitration
- [ ] Implement score animations (counting up)
- [ ] Add display test patterns
- [ ] Support for different display types
- [ ] Attract mode with scrolling text
- [ ] High score table display
- [ ] Error/tilt messages on displays

## Reference Links

- [TCA9548A Datasheet](https://www.ti.com/lit/ds/symlink/tca9548a.pdf)
- [Adafruit TCA9548A Guide](https://learn.adafruit.com/adafruit-tca9548a-1-to-8-i2c-multiplexer-breakout)
- [HT16K33 7-Segment Display](https://learn.adafruit.com/adafruit-led-backpack/0-dot-56-seven-segment-backpack)
- [ESP32 I2C Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2c.html)

---

**Hardware Status**: ⚠️ TCA9548A multiplexer board not yet received  
**Software Status**: ✅ Framework complete, ready for hardware testing  
**Next Action**: Test with actual multiplexer when it arrives
