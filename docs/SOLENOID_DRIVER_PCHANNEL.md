# P-Channel High-Side Solenoid Driver Circuit

## Overview
Switching from N-channel to P-channel IRF4905 MOSFETs for high-side solenoid switching to match the lamp matrix driver topology.

## Circuit Configuration

### Signal Flow
```
ESP32 GPIO (GPIO2, GPIO4, GPIO12)
    ↓
74HC138 3-to-8 Decoder (Y0-Y7 outputs, active LOW)
    ↓
1kΩ Series Resistor (gate protection)
    ↓
10kΩ Pull-up to +12V (ensures OFF state)
    ↓
IRF4905 P-channel MOSFET Gate
    ↓
IRF4905 Source (+12V) → Drain → Solenoid → GND
    ↓
1N4004 Flyback Diode (cathode to +12V, anode to GND side of solenoid)
```

## Component Requirements

### Per Channel (8 total):
- **IRF4905 P-channel MOSFET** (Vds = -55V, Id = -74A, Rds(on) = 20mΩ)
- **10kΩ Resistor** (1/4W) - Pull-up from gate to +12V
- **1kΩ Resistor** (1/4W) - Series resistor from 74HC138 output to gate
- **1N4004 Diode** (1A, 400V) - Flyback protection across solenoid

### Shared:
- **74HC138** 3-to-8 Decoder (1 chip drives all 8 channels)
- **+12V Power Supply** for solenoid power

## Critical Design Details

### Gate Voltage Problem & Solution

**Problem:**
- 74HC138 outputs: 0V (active) or 3.3V (inactive)
- P-channel source at +12V
- When 74HC138 output = 3.3V: Vgs = 3.3V - 12V = **-8.7V**
- IRF4905 threshold Vgs(th) = -2V to -4V → **MOSFET STILL PARTIALLY ON!**

**Solution - 10kΩ Pull-up Resistor:**
- Pull-up resistor from gate to +12V
- When 74HC138 output = HIGH (3.3V):
  - Pull-up brings gate voltage toward +12V
  - Vgs ≈ 0V → **MOSFET FULLY OFF**
- When 74HC138 output = LOW (0V):
  - 1kΩ series resistor dominates voltage divider
  - Gate pulled to ~0V
  - Vgs = 0V - 12V = -12V → **MOSFET FULLY ON**

### Resistor Network Calculation

**When 74HC138 = LOW (0V) - MOSFET ON:**
```
Voltage divider: 1kΩ to 0V, 10kΩ to +12V
Vgate = 12V × (1kΩ / (1kΩ + 10kΩ)) = 1.09V
Vgs = 1.09V - 12V = -10.91V → Fully ON
```

**When 74HC138 = HIGH (3.3V) - MOSFET OFF:**
```
Gate pulled toward +12V through 10kΩ
3.3V output has weak drive (25mA max)
Vgate ≈ 11.5V to 12V
Vgs ≈ -0.5V to 0V → Fully OFF
```

## Hardware Implementation

### Wiring Per Channel (Repeat 8x for Y0-Y7):

1. **74HC138 Output (Y0-Y7)**
   - Connect to 1kΩ resistor

2. **1kΩ Series Resistor**
   - One end to 74HC138 output
   - Other end to MOSFET gate

3. **10kΩ Pull-up Resistor**
   - One end to MOSFET gate (same node as 1kΩ)
   - Other end to +12V rail

4. **IRF4905 P-channel MOSFET**
   - **Source** → +12V rail
   - **Drain** → Solenoid positive terminal
   - **Gate** → Junction of 1kΩ and 10kΩ resistors

5. **Solenoid**
   - Positive terminal → MOSFET drain
   - Negative terminal → GND

6. **1N4004 Flyback Diode**
   - **Cathode** (stripe) → +12V rail (or MOSFET drain)
   - **Anode** → GND (or solenoid negative)
   - Protects against inductive kickback

### 74HC138 Enable Pins (Boot Glitch Prevention):

**Option 1: RC Delay Circuit (Recommended)**
```
10kΩ resistor + 100µF tantalum capacitor on G1 pin
- G1 → 10kΩ → +3.3V
- G1 → 100µF → GND
- Time constant: τ = RC = 1 second
- Delays decoder enable until after ESP32 boot (300-500ms)
```

**Option 2: GPIO Enable Control**
```
- G1 → ESP32 GPIO (e.g., GPIO14)
- G2A → GND
- G2B → GND
- Software enables G1 HIGH after boot sequence complete
```

**Current Wiring (No Protection):**
```
- G1 → +3.3V (always enabled - BOOT GLITCH RISK)
- G2A → GND
- G2B → GND
```

## 74HC138 Address Decoding

### GPIO to Output Mapping:
```
A2 (GPIO12) | A1 (GPIO4) | A0 (GPIO2) | Active Output | Solenoid
------------|-----------|-----------|---------------|----------
    0       |     0     |     0     |      Y0       | Sol 0
    0       |     0     |     1     |      Y1       | Sol 1
    0       |     1     |     0     |      Y2       | Sol 2
    0       |     1     |     1     |      Y3       | Sol 3
    1       |     0     |     0     |      Y4       | Sol 4
    1       |     0     |     1     |      Y5       | Sol 5
    1       |     1     |     0     |      Y6       | Sol 6
    1       |     1     |     1     |      Y7       | Sol 7
```

### Important: Active LOW Outputs
- Selected output goes to **0V (LOW)**
- All other outputs stay at **3.3V (HIGH)**
- With P-channel: LOW = ON, HIGH = OFF (perfect match!)

## Software Control

### Current Code (combined_test_rtos.cpp):
```cpp
#define SOL_A0  2   // GPIO2 - Address bit 0 (LSB)
#define SOL_A1  4   // GPIO4 - Address bit 1
#define SOL_A2  12  // GPIO12 - Address bit 2 (MSB)
```

### Solenoid Activation Function:
```cpp
void activateSolenoid(uint8_t solenoidNum) {
    if (solenoidNum > 7) return;  // Only 0-7 valid
    
    // Set address lines to select solenoid
    digitalWrite(SOL_A0, (solenoidNum >> 0) & 1);
    digitalWrite(SOL_A1, (solenoidNum >> 1) & 1);
    digitalWrite(SOL_A2, (solenoidNum >> 2) & 1);
    
    // Selected output goes LOW → P-channel turns ON
    // Keep active for pulse duration (typically 20-50ms)
}

void deactivateSolenoid() {
    // Set to invalid address (all outputs HIGH)
    // Or use enable pin if boot protection implemented
    digitalWrite(SOL_A0, LOW);
    digitalWrite(SOL_A1, LOW);
    digitalWrite(SOL_A2, LOW);
    // Y0 goes LOW, but this can be designated as "no solenoid"
}
```

### Safe Boot Sequence (with GPIO enable):
```cpp
void setup() {
    // Configure address pins
    pinMode(SOL_A0, OUTPUT);
    pinMode(SOL_A1, OUTPUT);
    pinMode(SOL_A2, OUTPUT);
    
    // Set to safe state (address 0, but decoder disabled)
    digitalWrite(SOL_A0, LOW);
    digitalWrite(SOL_A1, LOW);
    digitalWrite(SOL_A2, LOW);
    
    // Configure enable pin (if using GPIO control)
    pinMode(DECODER_ENABLE, OUTPUT);
    digitalWrite(DECODER_ENABLE, LOW);  // Keep disabled during init
    
    // ... rest of initialization ...
    
    // Enable decoder after everything ready
    digitalWrite(DECODER_ENABLE, HIGH);
}
```

## Testing Procedure

### 1. Visual Inspection
- Verify all 10kΩ pull-ups installed (gate to +12V)
- Verify all 1kΩ series resistors installed (74HC138 to gate)
- Verify all flyback diodes installed (cathode to +12V)
- Check P-channel orientation: flat side consistent, pins correct

### 2. Power-Off Resistance Check
- Measure gate to source on each MOSFET: should see 10kΩ pull-up
- Measure 74HC138 output to gate: should see 1kΩ series resistor

### 3. Power-On No-Signal Test
- Apply +12V power
- ESP32 not programmed or in reset
- **Expected:** No solenoids fire (pull-ups keep gates HIGH)
- Measure gate voltages: should be near +12V

### 4. Individual Solenoid Test
```cpp
void testSingleSolenoid(uint8_t num) {
    Serial.printf("Testing Solenoid %d...\n", num);
    
    // Activate
    activateSolenoid(num);
    delay(50);  // 50ms pulse
    
    // Deactivate
    deactivateSolenoid();
    delay(500);  // 500ms between tests
}
```

### 5. Gate Voltage Verification
- Solenoid OFF: Vgate ≈ 11.5V to 12V, Vgs ≈ 0V
- Solenoid ON: Vgate ≈ 0V to 1V, Vgs ≈ -11V to -12V
- Measure with oscilloscope or multimeter

### 6. Current Draw Test
- Solenoid coil resistance: typically 10-50Ω
- At 12V with 20Ω coil: I = 12V / 20Ω = 0.6A
- Measure actual current per solenoid when activated
- Check for excessive heating after multiple activations

## Parts List (Complete Circuit)

### MOSFETs:
- 8× IRF4905 P-channel (TO-220 package)

### Resistors:
- 8× 10kΩ 1/4W (pull-up, gate to +12V)
- 8× 1kΩ 1/4W (series, 74HC138 to gate)

### Diodes:
- 8× 1N4004 (flyback protection)

### IC:
- 1× 74HC138 3-to-8 decoder (DIP-16 or SOIC-16)

### Boot Protection (Option 1 - RC Delay):
- 1× 10kΩ resistor (G1 pull-up)
- 1× 100µF tantalum capacitor (G1 delay)

### Boot Protection (Option 2 - GPIO):
- 1× 10kΩ resistor (G1 pull-up)
- Wire from G1 to ESP32 GPIO (e.g., GPIO14)

### Power:
- +12V supply (minimum 1A per solenoid, 8A total for simultaneous)
- Adequate GND return path
- Bypass capacitors: 100µF electrolytic near solenoid bank

## Known Issues & Solutions

### Issue: Cookie Board MOSFETs Soldered In
**Status:** Waiting for replacement IRF4905 parts
**Action:** Cannot test until new MOSFETs arrive and are installed

### Issue: 74HC138 Boot Glitch
**Problem:** Combinatorial logic always has one output active during boot
**Solution:** Implement RC delay on G1 enable (10kΩ + 100µF)
**Alternative:** Use G1 GPIO control, enable after boot

### Issue: Solenoid Doesn't Release
**Symptom:** Solenoid stays energized when it should be OFF
**Cause:** Missing 10kΩ pull-up resistor
**Fix:** Add pull-up from gate to +12V on affected channel

### Issue: Weak Solenoid Activation
**Symptom:** Solenoid clicks but doesn't fully engage
**Cause:** Gate not being pulled low enough (check 1kΩ series resistor)
**Fix:** Verify 1kΩ installed, check for bad solder joints

### Issue: Random Solenoid Fires on Power-Up
**Symptom:** Solenoid activates briefly when power applied
**Cause:** 74HC138 glitch during power ramp-up
**Fix:** Add RC delay circuit on G1 enable pin

## Future Enhancements

1. **PWM Control** - Reduce holding current (stronger pull-in, weaker hold)
2. **Fault Detection** - Monitor solenoid current draw
3. **Thermal Shutdown** - Disable if MOSFET temperature exceeds limit
4. **Diagnostic Mode** - Test each solenoid individually on command
5. **Shift Register Alternative** - Replace 74HC138 with 74HC595 for better boot behavior

## References

- IRF4905 Datasheet: Vgs(th) = -2V to -4V, Rds(on) = 20mΩ @ Vgs = -10V
- 74HC138 Datasheet: Active-LOW outputs, propagation delay ~15ns
- Lamp Matrix Driver: Uses same P-channel high-side topology
- LAMP_VOLTAGE_NOTES.md: PWM voltage regulation techniques

---
**Last Updated:** December 5, 2025
**Status:** Circuit designed, awaiting hardware implementation
