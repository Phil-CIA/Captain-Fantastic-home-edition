# Captain Fantastic - Switch & Lamp Matrix Assignment

## Matrix Overview

The original Captain Fantastic uses an **8-row × 8-column matrix** shared between switches (inputs) and lamps (outputs).

### Matrix Structure:
- **8 Rows (M1-M8)**: Scanned sequentially, shared by both switches and lamps
- **4 Switch Columns (SW1-SW4)**: Input sensing with pull-ups
- **4 Lamp Columns (L4-L7)**: Output driving for lamps

## Switch Assignments (22 switches total)

| Switch # | Type | Location | Matrix Position | Description |
|----------|------|----------|----------------|-------------|
| S1 | Rollover | Lane "a" | | Top rollover lane A |
| S2 | Rollover | Lane "b" | | Top rollover lane B |
| S3 | Rollover | Lane C | | Top rollover lane C |
| S4 | Rollover | Lane D | | Top rollover lane D |
| S5 | Target | 3 | | Standup target 3 |
| S6 | Target | 1 | | Standup target 1 |
| S7 | Bumper | Left | | Left thumper bumper |
| S8 | Bumper | Right | | Right thumper bumper |
| S9 | Target | 2 | | Standup target 2 |
| S10 | Spinner | Left | | Left spinner |
| S11 | Spinner | Right | | Right spinner |
| S12 | Side Switch | | | Side rollover switch |
| S13 | Side Switch | | | Side rollover switch |
| S14 | Slingshot | Left | | Left slingshot |
| S15 | Slingshot | Left | | Left slingshot (alternate) |
| S16 | Slingshot | Right | | Right slingshot |
| S17 | Slingshot | Right | | Right slingshot (alternate) |
| S18 | Rollover | Left | | Left return lane |
| S19 | Rollover | Right | | Right return lane |
| S20 | (Special) | | | (Function TBD) |
| S21 | Rollover | Left | | Left outlane |
| S22 | Rollover | Right | | Right outlane |
| Start | Pushbutton | Cabinet | | Start button |
| Tilt | Plumb Bob | Cabinet | | Tilt mechanism |

## Lamp Assignments (22 lamps + indicators)

### Playfield Lamps (L1-L22):
| Lamp # | Location | Value/Function |
|--------|----------|----------------|
| L1 | A | Top lane A indicator |
| L2 | B | Top lane B indicator |
| L3 | C | Top lane C indicator |
| L4 | D | Top lane D indicator |
| L5 | 3 | Target 3 indicator |
| L6 | 1 | Target 1 indicator |
| L7 | Double Bonus | Double bonus multiplier |
| L8 | Triple Bonus | Triple bonus multiplier |
| L9 | 2 | Target 2 indicator |
| L10 | 10K Bonus | 10,000 point bonus light |
| L11 | 9K Bonus | 9,000 point bonus light |
| L12 | 8K Bonus | 8,000 point bonus light |
| L13 | 7K Bonus | 7,000 point bonus light |
| L14 | 6K Bonus | 6,000 point bonus light |
| L15 | 5K Bonus | 5,000 point bonus light |
| L16 | 4K Bonus | 4,000 point bonus light |
| L17 | 3K Bonus | 3,000 point bonus light |
| L18 | 2K Bonus | 2,000 point bonus light |
| L19 | 1K Bonus | 1,000 point bonus light |
| L20 | Same Player | Same player shoots again |
| L21 | Return Lane L | Left return lane indicator |
| L22 | Return Lane R | Right return lane indicator |

### Status Indicators:
| Indicator | Function |
|-----------|----------|
| Game Over | Game over indicator |
| B1-B5 | Ball in play (1-5) |
| P1-P4 | Player up (1-4) |

## Matrix Schematic Analysis

From the provided schematic image, the matrix is organized as:

### Rows (Vertical Lines - M1 through M8):
- **MX1** through **MX8** are the row drivers
- Each row can activate multiple switches or lamps simultaneously

### Columns (Horizontal Lines):
#### Switch Columns (Left side):
- **SW1** through **SW4** - Input columns with pull-ups
- Switches connect row to column when closed (ground the row)

#### Lamp Columns (Right side):
- **LED columns** - Output columns driving lamps
- Lamps illuminate when both row and column are active

### Diode Protection:
- **Switches**: Diodes (e.g., D70, D71, D72...) prevent ghosting/sneak paths
- **Lamps**: Diodes (e.g., D12, D13, D14...) ensure current flows in correct direction

## ESP32 GPIO Mapping

### Row Drivers (M1-M8) - Shared for Switches & Lamps:
```cpp
#define ROW_M1  23  // GPIO23
#define ROW_M2  19  // GPIO19
#define ROW_M3  18  // GPIO18
#define ROW_M4   5  // GPIO5
#define ROW_M5  17  // GPIO17
#define ROW_M6  16  // GPIO16
#define ROW_M7  15  // GPIO15
#define ROW_M8  13  // GPIO13
```

### Switch Columns (SW1-SW4) - Inputs:
```cpp
#define COL_SW1  35  // GPIO35 (input-only)
#define COL_SW2  34  // GPIO34 (input-only)
#define COL_SW3  39  // GPIO39 (input-only)
#define COL_SW4  36  // GPIO36 (input-only)
```

### Lamp Columns (L4-L7) - Outputs:
```cpp
#define COL_L4  27  // GPIO27
#define COL_L5  26  // GPIO26
#define COL_L6  33  // GPIO33
#define COL_L7  32  // GPIO32
```

## Matrix Scanning Strategy

### Switch Scanning (Input):
1. Set all rows HIGH (inactive)
2. Set one row LOW (activate)
3. Read all 4 switch columns (SW1-SW4)
4. If column reads LOW → switch is closed at that row/column intersection
5. Move to next row, repeat

### Lamp Driving (Output):
1. For each lamp to illuminate:
   - Set appropriate row LOW
   - Set appropriate column HIGH
   - Lamp lights when voltage differential exists
2. Multiplex at 100-200Hz for persistence of vision

### Timing:
- **Switch scan rate**: 1-2ms per row = 8-16ms full scan (60-125Hz)
- **Lamp refresh rate**: Similar timing, interleaved with switch scan
- **Debounce**: 10-20ms for mechanical switches

## Game Logic Mapping

### Switch Actions:

**Rollovers (S1-S4, S18-S19, S21-S22):**
- Score points (1000-5000)
- Light corresponding lane indicator lamp
- Trigger lane completion bonus
- Sound: Target beep (FREQ_HIGH)

**Targets (S5, S6, S9):**
- Score points (500-1000)
- Light target indicator lamp
- Trigger bonus multiplier if all 3 hit
- Sound: Target beep (FREQ_HIGH)

**Bumpers (S7, S8):**
- Score points (100)
- Fire corresponding bumper solenoid
- Increment bumper hit counter
- Sound: Bumper buzz (FREQ_LOW)

**Spinners (S10, S11):**
- Score points per revolution (10-100)
- Increment spinner count
- Sound: Rapid target beeps

**Slingshots (S14-S17):**
- Score points (10)
- Fire corresponding slingshot solenoid
- Sound: Slingshot buzz (FREQ_LOW rising)

**Special Switches:**
- **Start Button**: Begin new game or add player
- **Tilt**: End current ball, no bonus

### Lamp Sequences:

**Lane Indicators (L1-L4):**
- Light when rollover lane completed
- Flash when all 4 lit (bonus awarded)
- Reset between balls

**Bonus Ladder (L10-L19):**
- Advance one lamp per bonus-awarding event
- Multiply by 1×/2×/3× at end of ball
- Count down at end-of-ball bonus sequence

**Special Indicators:**
- **L20 (Same Player)**: Extra ball or replay awarded
- **L21/L22 (Return Lanes)**: Return lane active indicators
- **L7/L8 (Double/Triple)**: Bonus multiplier active

## Implementation Notes

### Switch Debouncing:
- Mechanical switches bounce for 5-20ms
- Require state to be stable for 10-20ms before registering
- Use timestamp-based debouncing per switch

### Lamp PWM (Future):
- Can implement brightness control via PWM
- Useful for flashing effects (bonus countdown)
- Keep PWM frequency >200Hz to avoid flicker

### Solenoid Integration:
- Bumpers (S7, S8) → Fire bumper solenoids immediately
- Slingshots (S14-S17) → Fire slingshot solenoids immediately
- Pulse duration: 30-50ms typical

### Sound Integration:
- Rollovers/Targets → High beep (1300Hz)
- Bumpers/Slingshots → Low buzz (700Hz)
- Spinners → Rapid clicks (1000Hz, 50ms)
- Bonus countdown → Mid-range beeps (1000Hz)

## Matrix State Tracking

### Switch State Array:
```cpp
volatile bool switchMatrix[8][4];  // Current state
volatile bool switchPrevious[8][4]; // Previous state for edge detection
volatile uint32_t switchDebounce[8][4]; // Debounce timestamps
```

### Lamp State Array:
```cpp
volatile bool lampMatrix[8][4];  // Desired lamp states
```

## Physical Matrix Wiring

Based on schematic:
- **Diodes**: 1N4148 or equivalent for switches
- **Wire gauge**: 22-24 AWG for switch matrix, 18-20 AWG for lamp matrix
- **Connectors**: Row connectors (MX1-MX8) on right side of schematic
- **Protection**: Diodes prevent current backflow and sneak paths

## Testing Procedure

### 1. Switch Matrix Test:
```cpp
// Scan all switches and report which are closed
void testSwitchMatrix() {
    for (uint8_t row = 0; row < 8; row++) {
        digitalWrite(rowPins[row], LOW);
        delay(1);
        for (uint8_t col = 0; col < 4; col++) {
            if (digitalRead(switchCols[col]) == LOW) {
                Serial.printf("Switch detected: Row M%d, Col SW%d\n", row+1, col+1);
            }
        }
        digitalWrite(rowPins[row], HIGH);
    }
}
```

### 2. Lamp Matrix Test:
```cpp
// Light each lamp individually
void testLampMatrix() {
    for (uint8_t row = 0; row < 8; row++) {
        for (uint8_t col = 0; col < 4; col++) {
            setLamp(row, col, true);
            delay(200);
            setLamp(row, col, false);
        }
    }
}
```

### 3. Integration Test:
- Press each switch → Verify serial output shows correct switch
- Manually light each lamp → Verify correct lamp illuminates
- Test switch→solenoid mapping
- Test switch→sound mapping
- Test switch→lamp mapping

## Future Enhancements

1. **Switch Statistics**: Track hits per switch, time between hits
2. **Lamp Effects**: Chasing lights, pulsing, fade in/out
3. **Game Modes**: Different rule sets, timed modes
4. **High Score Tracking**: Save to EEPROM/SPIFFS
5. **Attract Mode**: Lamp sequences when idle
6. **Diagnostics**: Built-in switch/lamp test modes

---
**Last Updated:** December 5, 2025
**Status:** Matrix mapping complete, ready for implementation
**Reference:** Original Captain Fantastic schematic
