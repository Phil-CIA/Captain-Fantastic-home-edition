# OLED Display System - Architecture Documentation

## Overview
Captain Fantastic pinball machine display system using **5 × SSD1306 OLED displays (128×64 pixels each)** to mimic vintage 7-segment displays with authentic slanted/italicized segments.

## Hardware Configuration

### Displays
- **Type**: SSD1306 OLED (monochrome, 128×64 pixels)
- **Quantity**: 5 displays per row
- **Total Canvas**: 640 × 64 pixels (128px × 5)
- **Interface**: I2C (address 0x3C)
- **Multiplexer**: TCA9548A (address 0x70)

### Physical Layout
```
[Display 0][Display 1][Display 2][Display 3][Display 4]
 0-127px    128-255px  256-383px  384-511px  512-639px
```

### Original Display Reference
**Lumex LDS-C814** (or similar)
- 0.8" 7-segment display
- Character height: ~20mm
- Italicized/slanted segments (~10-12°)
- Segment width: ~2-3mm
- Character spacing: 9mm

## Display Architecture

### Character Layout
**6 characters per row** spanning 640 pixels:
- Character width: 74 pixels
- Character spacing: 33 pixels
- Total per character: 107 pixels
- Character height: 60 pixels (using 60 of 64 available)

**Character positions** (X coordinates):
```
Position 0: X=2
Position 1: X=109
Position 2: X=216  
Position 3: X=323
Position 4: X=430
Position 5: X=537
```

### Row Configuration
- **Row 0**: Player 1 score (6 digits)
- **Row 1**: Player 2 score (6 digits)
- **Row 2**: Player 3 score (6 digits)
- **Row 3**: Player 4 score (6 digits)
- **Row 4**: Status display (Player/Ball/Bonus)

## Virtual Pixel Buffer

### Buffer Structure
```cpp
uint8_t pixelBuffer[5120];  // 640×64 pixels = 5120 bytes (monochrome)
```

**Organization**: 
- 1 bit per pixel (monochrome)
- Organized as horizontal rows
- Byte packing: 8 pixels per byte

### Pixel Addressing
```cpp
// To set pixel at (x, y):
byteIndex = (y / 8) * BUFFER_WIDTH + x;
bitMask = 1 << (y % 8);
```

## 7-Segment Rendering

### Segment Layout (with slant)
```
       AAAA
      F    B
      F    B
       GGGG
      E    C
      E    C
       DDDD
```

**All segments have ~10-12° italic slant** to match vintage displays.

### Segment Dimensions
- **Length** (horizontal segments): 28 pixels
- **Height** (vertical segments): 24 pixels
- **Thickness**: 10 pixels
- **Slant offset**: 6 pixels horizontal

### Segment Positions
Relative to character origin (x, y):

**Horizontal segments** (A, G, D):
- Segment A (top): y=2 to y=12
- Segment G (middle): y=30 to y=40
- Segment D (bottom): y=58 to y=68 (actually 58-60 in 64px height)

**Vertical segments** (B, C, E, F):
- All slanted ~10-12° from vertical
- Top segments (F, B): y=0 to y=24
- Bottom segments (E, C): y=34 to y=58

### Character Patterns
Each character defined by segment pattern:
```cpp
struct SegmentPattern {
    bool segA, segB, segC, segD, segE, segF, segG, dp;
};
```

**Supported characters**:
- Digits: 0-9
- Hex: A-F
- Letters: C, E, L, U, P, b, d, u
- Special: blank, dash

## Display Update Pipeline

### Rendering Flow
1. **Clear pixel buffer** (optional)
2. **Render characters** → Virtual buffer
   - Convert character to segment pattern
   - Draw segments with slant
3. **Update physical displays** → I2C
   - Select multiplexer channel
   - Copy buffer region to display
   - Send to SSD1306

### Update Functions
```cpp
// High-level API
renderNumber(123456, false);       // Render 6-digit number
renderText("P1 b3", 0);            // Render text at position
renderCharAtPosition(2, '8');      // Single character

// Low-level
setPixel(x, y, true);              // Individual pixel
drawSlantedLine(x1, y1, x2, y2, thickness, color);
```

## Code Organization

### File Structure
```
include/
  displayFonts.h          - Character bitmap definitions & segment patterns
  displays.h              - Main display API & configuration
  displayMux.h            - TCA9548A multiplexer control
  
src/
  displays_oled.cpp       - OLED rendering implementation
  displayMux.cpp          - Multiplexer implementation
  main_oled_test.cpp      - Test program
```

### Key Components

**displayFonts.h**:
- Segment pattern definitions
- Character lookup table
- Rendering primitives

**displays.h**:
- Configuration constants
- Display API
- Buffer management

**displays_oled.cpp**:
- Pixel buffer operations
- Segment rendering (with slant)
- Character rendering
- Display update logic

## Configuration

### Scalability
Easily adapt to different configurations by changing:
```cpp
#define NUM_DISPLAYS 5           // Number of physical displays
#define NUM_CHARS_PER_ROW 6      // Characters per row
#define NUM_PLAYER_ROWS 4        // Number of player rows
#define HAS_STATUS_ROW true      // Enable status row
```

### I2C Settings
```cpp
Wire.begin(21, 22);              // SDA=21, SCL=22 (ESP32)
Wire.setClock(400000);           // 400kHz I2C fast mode
```

### Display Settings
```cpp
#define SSD1306_I2C_ADDRESS 0x3C
#define OLED_RESET -1            // No hardware reset pin
```

## Performance

### Timing
- **I2C speed**: 400kHz (fast mode)
- **Display update**: ~50-100ms per full refresh
- **Channel switching**: ~5ms delay between displays

### Memory Usage
- **Pixel buffer**: 5,120 bytes (640×64 pixels)
- **Display objects**: ~1KB per display × 5 = ~5KB
- **Total RAM**: ~10-15KB for display system

## Testing

### Test Commands
Send via Serial (115200 baud):
```
0-9  - Display specific digit
t    - Run full test sequence
s    - Test segment rendering
c    - Test character rendering
x    - Clear all displays
b    - Test brightness levels
p    - Print display mapping
d    - Dump pixel buffer stats
m    - Scan I2C multiplexer
?/h  - Help menu
```

### Test Sequence
1. Scan I2C channels (verify all displays detected)
2. Clear all displays
3. Count 0-9 with leading zeros
4. Display various numbers (123456, 999999, etc.)
5. Test all characters (letters, symbols)

## Future Enhancements

### Potential Improvements
1. **Row multiplexing** - Cycle between player rows if needed
2. **Animations** - Scrolling scores, transitions
3. **Custom characters** - Additional game-specific symbols
4. **Brightness control** - Per-display brightness adjustment
5. **Buffer optimization** - Dirty rectangle tracking
6. **Anti-aliasing** - Smooth segment edges
7. **Font variations** - Different segment styles/sizes

### Hardware Expansion
- Support for additional display rows
- Different display types (larger OLEDs)
- Color OLED displays
- Matrix/dot displays

## Comparison: Old vs New

### Original System (7-Segment)
- Physical 7-segment LED displays
- 7 segments + decimal point
- Simple segment patterns (8 bits)
- Limited character set
- Fixed appearance

### New OLED System
- SSD1306 pixel displays
- Full pixel control (640×64)
- Bitmap rendering
- Unlimited character possibilities
- Authentic vintage appearance with modern flexibility
- Scalable and configurable

## Summary
The OLED display system provides **authentic vintage 7-segment appearance** with the **flexibility of modern pixel displays**. By rendering slanted segments as bitmaps, we perfectly mimic the Lumex LDS-C814 style while gaining the ability to display any character or graphic needed for the game.
