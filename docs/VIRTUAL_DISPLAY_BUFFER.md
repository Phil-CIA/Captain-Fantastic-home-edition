# Virtual Display Buffer Architecture

## Concept Overview

This system uses a **pixel/segment matrix approach** to manage 5 physical 4-digit displays as if they were 6 old-style 7-segment displays with 6 digits each.

### The Problem
- Original pinball machines: 6 displays × 6 digits = 36 total digit positions
- Your hardware: 5 displays × 4 physical digits = 20 total positions
- **Solution**: Create a **30-digit virtual buffer** and intelligently slice it to physical displays

## Architecture

```
VIRTUAL BUFFER (30 digits total)
┌─────────────────────────────────────────────────────────────┐
│  0   1   2   3   4   5 │  6   7   8   9  10  11 │ ...       │
│     Player 1 (6 digits)│     Player 2 (6 digits)│           │
└─────────────────────────────────────────────────────────────┘

PHYSICAL DISPLAYS (4 digits each)
┌───────────────┐  ┌───────────────┐  ┌───────────────┐
│  3   4   5   ?│  │  9  10  11   ?│  │ 15  16  17   ?│
│  Display 0    │  │  Display 1    │  │  Display 2    │
└───────────────┘  └───────────────┘  └───────────────┘
    Shows digits      Shows digits      Shows digits
    2, 3, 4, 5        8, 9, 10, 11      14, 15, 16, 17
    (rightmost 4)     (rightmost 4)     (rightmost 4)
```

### Buffer Layout

| Virtual Digits | Allocation | Physical Display | Default Scroll | What Shows |
|----------------|------------|------------------|----------------|------------|
| 0-5 | Player 1 Score | Display 0 | offset=2 | Digits 2-5 (rightmost 4) |
| 6-11 | Player 2 Score | Display 1 | offset=2 | Digits 8-11 (rightmost 4) |
| 12-17 | Player 3 Score | Display 2 | offset=2 | Digits 14-17 (rightmost 4) |
| 18-23 | Player 4 Score | Display 3 | offset=2 | Digits 20-23 (rightmost 4) |
| 24-29 | Status (Ball/Bonus) | Display 4 | offset=0 | Digits 24-27 (all 4) |

## How It Works

### Step 1: Write to Virtual Buffer
Game logic writes segment patterns to the 30-digit virtual buffer:

```cpp
// Example: Player 1 scores 123456
writeNumberToBuffer(PLAYER1_START_DIGIT, 123456, 6, false);

// This writes to positions 0-5:
// virtualDisplayBuffer[0] = pattern for '1'
// virtualDisplayBuffer[1] = pattern for '2'
// virtualDisplayBuffer[2] = pattern for '3'
// virtualDisplayBuffer[3] = pattern for '4'
// virtualDisplayBuffer[4] = pattern for '5'
// virtualDisplayBuffer[5] = pattern for '6'
```

### Step 2: Slice Buffer to Physical Displays
The refresh routine reads chunks of the virtual buffer:

```cpp
// For Display 0 (Player 1) with scroll offset=2:
// Read positions 2, 3, 4, 5 from virtual buffer
// Send to physical display digits 0, 1, 2, 3
// Display shows: "3456"
```

### Step 3: Scroll Control
If score exceeds 4 digits, you can scroll to see all digits:

```cpp
// Score: 123456
// Offset 0: Show digits 0-3 = "1234"
// Offset 1: Show digits 1-4 = "2345"
// Offset 2: Show digits 2-5 = "3456" (default - rightmost)
```

## Segment Pattern Encoding

Each digit in the virtual buffer is stored as a **7-segment bit pattern**:

```
     A
    ───
   │   │
 F │   │ B
   │ G │
    ───
   │   │
 E │   │ C
   │   │
    ───  • DP
     D

Bit 0 (0x01) = Segment A
Bit 1 (0x02) = Segment B
Bit 2 (0x04) = Segment C
Bit 3 (0x08) = Segment D
Bit 4 (0x10) = Segment E
Bit 5 (0x20) = Segment F
Bit 6 (0x40) = Segment G
Bit 7 (0x80) = Decimal Point
```

### Example Patterns

```cpp
'0' = 0x3F = 0b00111111 = ABCDEF
'1' = 0x06 = 0b00000110 = BC
'8' = 0x7F = 0b01111111 = ABCDEFG
'P' = 0x73 = 0b01110011 = ABEFG
'-' = 0x40 = 0b01000000 = G only
```

## Code Flow

### Writing Scores
```cpp
setPlayerScore(1, 123456);
  ↓
writeNumberToBuffer(0, 123456, 6, false)
  ↓
Virtual buffer positions 0-5 filled with segment patterns
  ↓
displayState.needsUpdate = true
```

### Refreshing Displays
```cpp
updateAllDisplays();
  ↓
refreshAllPhysicalDisplays();
  ↓
For each display 0-4:
  ↓
  updatePhysicalDisplay(displayNum)
    ↓
    Calculate: virtualStart = displayNum × 6
    Get scroll offset (default = 2 for scores)
    ↓
    Read 4 bytes from virtual buffer at positions:
      [virtualStart + offset]
      [virtualStart + offset + 1]
      [virtualStart + offset + 2]
      [virtualStart + offset + 3]
    ↓
    Select multiplexer channel
    Send 4 segment patterns to physical display
    Disable multiplexer
```

## Key Functions

### Virtual Buffer Management
```cpp
clearVirtualBuffer()                    // Zero out all 30 digits
writeDigitToBuffer(pos, pattern)        // Write raw segment pattern
writeCharToBuffer(pos, 'A')             // Write character
writeNumberToBuffer(start, 123456, 6)   // Write multi-digit number
writeTextToBuffer(start, "P1", 2)       // Write text string
```

### Display Control
```cpp
updateAllDisplays()                     // Write buffer → physical displays
refreshAllPhysicalDisplays()            // Update all 5 displays
updatePhysicalDisplay(0)                // Update specific display
setScrollOffset(0, 2)                   // Set which 4 of 6 to show
```

### Debugging
```cpp
printVirtualBuffer()                    // Print buffer to Serial
```

## Advantages of This Approach

✅ **Flexibility**: 6 virtual digits per display, even with 4 physical  
✅ **Scrolling**: Can show full 6-digit scores by scrolling  
✅ **Clean Code**: Game logic doesn't care about physical limitations  
✅ **Expandable**: Easy to add more displays or change layout  
✅ **Testable**: Can test without hardware (prints to Serial)  
✅ **True to Original**: Mimics behavior of 6×7-segment vintage displays  

## Example Usage

### Simple Score Update
```cpp
// Player 1 scores 5000 points
addToPlayerScore(1, 5000);
updateAllDisplays();

// Virtual buffer now has:
// Positions 0-5: [ ][5][0][0][0]
// Physical display shows positions 2-5: "5000"
```

### Full 6-Digit Score
```cpp
// Player scores 999999
setPlayerScore(1, 999999);
updateAllDisplays();

// Virtual buffer:
// Positions 0-5: [9][9][9][9][9][9]
// Physical display (offset=2): "9999"

// To see first two digits:
setScrollOffset(0, 0);
updateAllDisplays();
// Physical display (offset=0): "9999" (first 4)
```

### Status Display
```cpp
// Show "P2 b3" (Player 2, Ball 3)
setCurrentPlayer(2);
setBallNumber(3);
updateAllDisplays();

// Virtual buffer positions 24-29:
// [P][2][ ][b][3][0]
// Physical display shows: "P2 b"
```

## Memory Usage

```
Virtual Buffer:     30 bytes  (30 digits × 1 byte each)
Display State:      ~32 bytes (scores, flags, offsets)
Segment Patterns:   ~21 bytes (const lookup table)
Total:              ~83 bytes (negligible on ESP32)
```

## Hardware Integration

When you connect actual HT16K33 displays, the TODO sections become:

```cpp
void updatePhysicalDisplay(uint8_t displayNum) {
    // ... calculate positions ...
    
    selectMuxChannel(displayNum);
    
    // Initialize display
    display.begin(DISPLAY_I2C_ADDRESS);
    
    // Write 4 segment patterns
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t bufferPos = virtualStart + offset + i;
        display.writeDigitRaw(i, virtualDisplayBuffer[bufferPos]);
    }
    
    display.setBrightness(displayState.brightness);
    display.writeDisplay();  // Send to physical display
    
    disableMuxChannels();
}
```

## Testing the System

Run `main_display_test.cpp` to see:
- Virtual buffer creation and updates
- Segment pattern encoding
- Buffer slicing to displays
- Scroll offset demonstration
- All printed to Serial Monitor

The test cycles through:
1. Clear displays
2. Single player scoring
3. Full 6-digit scores (123456)
4. All 4 players
5. Maximum scores (999999)
6. Scroll demonstration
7. Incremental counting

## Future Enhancements

- [ ] **Animations**: Smooth score counting with intermediate buffer updates
- [ ] **Flashing**: Toggle specific digits on/off (e.g., current player)
- [ ] **Auto-scroll**: Automatically scroll through 6 digits if > 4
- [ ] **Decimal points**: Use DP bit for thousands separators
- [ ] **Special effects**: Digit wipes, fades, chase patterns
- [ ] **Compression**: Right-justify small numbers, left-justify large ones

---

**This architecture gives you the best of both worlds**: the flexibility of a modern virtual framebuffer with the authentic look of vintage pinball displays!
