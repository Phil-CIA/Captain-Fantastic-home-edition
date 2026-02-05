# Virtual Display Buffer - Quick Reference

## System Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                    30-DIGIT VIRTUAL BUFFER                      │
│  All game logic writes HERE (segment patterns)                  │
└─────────────────────────────────────────────────────────────────┘
                              ↓
                    ┌─────────────────┐
                    │ SCROLL OFFSET   │ (Which 4 of 6 to show)
                    └─────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│              5 PHYSICAL DISPLAYS × 4 DIGITS EACH                │
│  Displays receive sliced chunks of virtual buffer               │
└─────────────────────────────────────────────────────────────────┘
```

## Virtual Buffer Map

```
Position:  0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15  16  17  18  19  20  21  22  23  24  25  26  27  28  29
          ┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┐
Content:  │               │               │               │               │                   │
          │  Player 1     │  Player 2     │  Player 3     │  Player 4     │  Status/Ball      │
          │  Score (6)    │  Score (6)    │  Score (6)    │  Score (6)    │  Bonus (6)        │
          └───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┘

Physical:     └───────────┘       └───────────┘       └───────────┘       └───────────┘       └───────────┘
Display:      Display 0           Display 1           Display 2           Display 3           Display 4
Shows:        Pos 2-5             Pos 8-11            Pos 14-17           Pos 20-23           Pos 24-27
              (rightmost 4)       (rightmost 4)       (rightmost 4)       (rightmost 4)       (leftmost 4)
```

## Example: Score 123456 on Player 1

### Virtual Buffer Write
```
Position:     0    1    2    3    4    5
Pattern:    0x06 0x5B 0x4F 0x66 0x6D 0x7D
Displays:     1    2    3    4    5    6
```

### Physical Display (with scroll offset = 2)
```
Reads positions 2, 3, 4, 5
Shows: "3456"
```

### Scroll Options
```
Offset 0:  Shows positions 0-3  →  "1234"
Offset 1:  Shows positions 1-4  →  "2345"
Offset 2:  Shows positions 2-5  →  "3456" ← Default
```

## 7-Segment Bit Encoding

```
        Bit Pattern: 0xABCDEFG_
                    (hex value)

         Bit 0 (A) ──┐
                     │
         ┌───────────┴───────────┐
         │           A           │
         │         ─────         │
         │        │     │        │
     Bit 5 (F) → │       │ ← Bit 1 (B)
         │        │  G  │        │
         │         ─────         │
         │        │     │        │
     Bit 4 (E) → │       │ ← Bit 2 (C)
         │        │     │        │
         │         ─────  •      │
         │           D    DP     │
         └───────────┬───────────┘
                     │
         Bit 3 (D) ──┘
         Bit 7 (DP) = Decimal Point
```

## Common Segment Patterns

```
┌──────┬────────┬────────────────┐
│ Char │  Hex   │    Binary      │
├──────┼────────┼────────────────┤
│  0   │  0x3F  │  0b00111111    │
│  1   │  0x06  │  0b00000110    │
│  2   │  0x5B  │  0b01011011    │
│  3   │  0x4F  │  0b01001111    │
│  4   │  0x66  │  0b01100110    │
│  5   │  0x6D  │  0b01101101    │
│  6   │  0x7D  │  0b01111101    │
│  7   │  0x07  │  0b00000111    │
│  8   │  0x7F  │  0b01111111    │
│  9   │  0x6F  │  0b01101111    │
│  A   │  0x77  │  0b01110111    │
│  P   │  0x73  │  0b01110011    │
│  b   │  0x7C  │  0b01111100    │
│  -   │  0x40  │  0b01000000    │
│ SPC  │  0x00  │  0b00000000    │
└──────┴────────┴────────────────┘
```

## Function Quick Reference

### Write to Buffer
```cpp
writeNumberToBuffer(0, 123456, 6);      // Write 6-digit number starting at pos 0
writeCharToBuffer(24, 'P');             // Write 'P' at position 24
writeTextToBuffer(24, "P1", 2);         // Write "P1" starting at pos 24
writeDigitToBuffer(5, 0x7F);            // Write raw pattern (8) at pos 5
```

### Update Displays
```cpp
updateAllDisplays();                    // Write entire buffer → all displays
refreshAllPhysicalDisplays();           // Refresh all 5 displays now
updatePhysicalDisplay(0);               // Update only Display 0
```

### Scroll Control
```cpp
setScrollOffset(0, 0);                  // Display 0: show leftmost 4 digits
setScrollOffset(0, 1);                  // Display 0: show middle 4 digits
setScrollOffset(0, 2);                  // Display 0: show rightmost 4 (default)
```

### Game State
```cpp
setPlayerScore(1, 123456);              // Set player 1 score
addToPlayerScore(1, 5000);              // Add points to player 1
setCurrentPlayer(2);                    // Set active player
setBallNumber(3);                       // Set ball number
setBonusValue(7500);                    // Set bonus value
```

### Debug
```cpp
printVirtualBuffer();                   // Print entire buffer to Serial
```

## Typical Game Loop

```cpp
void loop() {
    // 1. Game logic updates scores
    if (switchHit) {
        addToPlayerScore(currentPlayer, 500);
    }
    
    // 2. Update virtual buffer and refresh displays
    updateAllDisplays();
    
    // 3. Handle other game logic
    // ...
    
    delay(10);
}
```

## Memory Map

```
┌────────────────────────────────────────┐
│ virtualDisplayBuffer[30]               │ 30 bytes
│   [0-5]   Player 1 Score               │
│   [6-11]  Player 2 Score               │
│   [12-17] Player 3 Score               │
│   [18-23] Player 4 Score               │
│   [24-29] Status Display               │
├────────────────────────────────────────┤
│ displayState                           │ 32 bytes
│   .player1Score                        │ 4 bytes
│   .player2Score                        │ 4 bytes
│   .player3Score                        │ 4 bytes
│   .player4Score                        │ 4 bytes
│   .currentPlayer                       │ 1 byte
│   .currentBall                         │ 1 byte
│   .bonusValue                          │ 2 bytes
│   .needsUpdate                         │ 1 byte
│   .brightness                          │ 1 byte
│   .scrollOffset[5]                     │ 5 bytes
├────────────────────────────────────────┤
│ SEGMENT_PATTERNS[21]                   │ 21 bytes (const)
└────────────────────────────────────────┘
Total: ~83 bytes
```

## Hardware Connections

```
ESP32 GPIO21 (SDA) ───┬─── TCA9548A Pin 22 (SDA)
                      │
                      ├─── MCP23017 Pin 13 (SDA)  [Solenoids]
                      
ESP32 GPIO22 (SCL) ───┬─── TCA9548A Pin 21 (SCL)
                      │
                      ├─── MCP23017 Pin 12 (SCL)  [Solenoids]

TCA9548A Channels:
  SC0/SD0 (Pins 3-4)   → Display 0 (Player 1)
  SC1/SD1 (Pins 5-6)   → Display 1 (Player 2)
  SC2/SD2 (Pins 7-8)   → Display 2 (Player 3)
  SC3/SD3 (Pins 9-10)  → Display 3 (Player 4)
  SC4/SD4 (Pins 19-20) → Display 4 (Status/Ball)
```

## Display Update Flow

```
Game Event
    ↓
addToPlayerScore(1, 500)
    ↓
displayState.player1Score += 500
displayState.needsUpdate = true
    ↓
updateAllDisplays()
    ↓
writeNumberToBuffer(0, score, 6)  ← Converts score to segment patterns
    ↓
virtualDisplayBuffer[0-5] = segment patterns
    ↓
refreshAllPhysicalDisplays()
    ↓
For each display 0-4:
  selectMuxChannel(displayNum)
  Read 4 bytes from buffer (with scroll offset)
  Send to HT16K33 chip via I2C
  disableMuxChannels()
    ↓
Physical displays show updated scores!
```

---

**Key Insight**: Game code never touches physical displays directly. Everything goes through the virtual buffer, which gets sliced and sent to hardware automatically. This is exactly how vintage pinball machines worked internally!
