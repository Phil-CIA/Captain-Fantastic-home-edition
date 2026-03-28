# Bally Series II Diagnostic Test Mode

## Overview
The diagnostic test mode implements the standardized test procedures from the original Bally Captain Fantastic service manual. This allows technicians to verify all hardware systems are functioning correctly before game operation.

## Entering Diagnostic Mode

### Serial Command Method
1. Open serial monitor at 115200 baud
2. Send command `D` or `d` to enter diagnostic mode
3. Send command `X` or `x` to exit diagnostic mode
4. Send command `?` for command menu

### On Startup
The system displays available commands after boot:
```
SERIAL COMMANDS:
  D - Enter Diagnostic Test Mode
  X - Exit Diagnostic Test Mode
  ? - Show command menu
```

## Diagnostic Test Sequence

The diagnostic mode follows the Bally Series II standardized test procedure with 5 steps:

### Step 0: Logic and Program Test
**Duration:** 3 seconds  
**Display:** `  600d` (or `006000` depending on display format)  
**Purpose:** Verifies that the MPU is executing code correctly  
**Expected Result:** Display shows `600d`. If display shows jibberish or is off, MPU has failed.

---

### Step 1: Score Display Scan
**Duration:** ~90 seconds  
**Display:** Counts from `000000` to `999999` in increments of `111`  
**Purpose:** Tests all segments of all 6 digits  
**Expected Result:** All digit positions should light all segments (0-9) during the scan  
**Troubleshooting:** 
- Missing segments indicate faulty display or HT16K33 wiring
- Stuck segments indicate shorted LEDs or driver issues

---

### Step 2: Lamp Test - Alternating Groups
**Duration:** ~10 seconds (5 cycles of each group)  
**Pattern:** Alternates between GROUP 1 and GROUP 2 every 1 second  
**Purpose:** Tests all playfield lamps in two groups  

**GROUP 1 Lamps:**
- Player lamps: P-1, P-2
- Bumper lamps: B-1, B-2
- Playfield lamps: L1, L2, L4, L5, L10, L11, L14, L15, L16, L17, L18, L22

**GROUP 2 Lamps:**
- Player lamps: P-3, P-4
- Bumper lamps: B-3, B-4, B-5
- Playfield lamps: L3, L6, L7, L8, L9, L12, L13, L18, L19, L20, L21
- Game Over lamp

**Note:** L18 appears in both groups per original Bally specification

**Expected Result:** Lamps should alternate cleanly between two groups  
**Troubleshooting:**
- Lamps that don't light: Check matrix row/column connections
- Dim lamps: Check power supply voltage and P-channel MOSFET gate drive
- Ghosting: Check for shorted matrix connections

---

### Step 3: Solenoid Test Sequence
**Duration:** 5 seconds (1 second per solenoid)  
**Purpose:** Tests each solenoid individually in sequence  
**Solenoid Order:**
1. **A - Ball Return** (SOL0)
2. **B - Left Slingshot** (SOL1)
3. **C - Right Slingshot** (SOL2)
4. **D - Left Thumper-Bumper** (SOL3)
5. **E - Right Thumper-Bumper** (SOL4)

**Expected Result:** Each solenoid should fire for 1 second in sequence  
**Troubleshooting:**
- No activation: Check shift register wiring, solenoid driver transistors, and power supply
- Weak activation: Check power supply voltage and driver transistor gain
- All fire at once: Check shift register latch signal

**CAUTION:** Ensure ball is removed from playfield during solenoid test to prevent damage

---

### Step 4: Switch Test - Stuck Switch Detection
**Duration:** Continuous until exited  
**Display:** Error code if switch is stuck, `000000` if all switches open  
**Purpose:** Identifies switches that are mechanically stuck closed  

**Error Code Format:**  
Error code = `(Row × 10) + Column`

Example error codes:
- `11` = Row 1, Column 1
- `24` = Row 2, Column 4
- `83` = Row 8, Column 3

**Expected Result:** Display shows `000000` when no switches are pressed  
**Troubleshooting:**
- If error code appears: Check corresponding switch in matrix for stuck contacts
- Intermittent codes: Check for loose wiring or bent switch blades
- Multiple codes: Check common row/column connections

**Switch Matrix Reference:**
- Rows: 1-8 (corresponding to M1-M8)
- Columns: 1-4 (corresponding to SW1-SW4)

Refer to playfield switch diagram in service manual for physical locations.

---

## Exiting Diagnostic Mode

1. Send `X` or `x` command via serial
2. System will display: `EXITING DIAGNOSTIC MODE`
3. All solenoids will be cleared (turned off)
4. System returns to normal attract mode

## Implementation Details

### Lamp Groups
The lamp groups are defined in helper functions:
- `setLampGroup1()` - Activates GROUP 1 lamps
- `setLampGroup2()` - Activates GROUP 2 lamps

### Timing
- Diagnostic steps execute in the `gameLogicTask()` at 50ms intervals
- Step transitions use counter-based timing for accuracy
- Solenoid firing uses safe 1-second pulses (adjustable for different hardware)

### Safety Features
1. All solenoids cleared when exiting diagnostic mode
2. Continuous solenoid activation limited to 1 second per solenoid
3. Switch test is non-blocking (doesn't activate any outputs)
4. Display test uses safe score increments to test all segments

## Technical Notes

### Display Pattern
The `600d` boot check uses score value `6000`, which the display system formats as `  600d` with leading spaces and trailing zeros.

### Score Scan Pattern
Incrementing by `111` ensures all digit values (0-9) are tested:
- `000000` → `000111` → `000222` → ... → `999999`
- Total iterations: ~9000 values
- Duration: ~90 seconds at 100ms per update

### Lamp Matrix
Our implementation includes the main playfield lamps (L1-L22). Player-specific lamps (P1-P4), bumper lamps (B1-B5), and Game Over lamp may be wired separately depending on the specific cabinet configuration.

## Serial Monitor Output

Example diagnostic run:
```
[DIAG] Step 0: Logic and Program Test - Display 600d
[DIAG] Step 1: Score Display Scan starting
[DIAG] Step 2: Lamp Test - Alternating Groups
[DIAG] Lamp GROUP 1
[DIAG] Lamp GROUP 2
[DIAG] Lamp GROUP 1
...
[DIAG] Step 3: Solenoid Test Sequence
[DIAG] Solenoid A: Ball Return
[DIAG] Solenoid B: Left Slingshot
[DIAG] Solenoid C: Right Slingshot
[DIAG] Solenoid D: Left Thumper-Bumper
[DIAG] Solenoid E: Right Thumper-Bumper
[DIAG] Step 4: Switch Test - Monitoring for stuck switches
[DIAG] STUCK SWITCH: Row 2, Col 3 (Error: 23)
```

## Modifications from Original Bally Test

The ESP32 implementation maintains the same test sequence and timing as the original Bally MPU test, with these adaptations:

1. **Display Format:** Uses our custom HT16K33 segment mapping
2. **Lamp Groups:** Excludes lamps not present in our matrix (player/bumper lamps may be separate)
3. **Serial Control:** Uses serial commands instead of physical diagnostic switch
4. **Switch Codes:** Uses same row/column error code format as original

## Future Enhancements

Potential improvements for diagnostic mode:
- [ ] Hardware switch to trigger diagnostic (e.g., hold coin switch on boot)
- [ ] Sound test sequence (currently audio task disabled)
- [ ] RAM/Flash test
- [ ] I2C device detection test
- [ ] Voltage monitoring test (if INA3221 sensors added)
