c# ESP32 Power Notes

## Current Situation

### Issue Observed
- ESP32 board doesn't start when powered via VIN pin alone
- Requires USB to be connected to run
- This is a common issue with many ESP32 dev boards

## ESP32 Power Options

### 1. VIN Pin (5V Input)
**What it is**: External power input, goes through onboard regulator to 3.3V

**Problems**:
- Some boards have a diode → 0.7V drop (need 5.7V input!)
- Protection circuits may block power
- USB power may have priority in switching logic
- Quality varies by manufacturer

**Your observation**: Board doesn't run on VIN alone ✗

### 2. 5V Pin (Direct 5V)
**What it is**: Direct connection to USB 5V rail (bypasses VIN circuit)

**Pros**: 
- Usually more reliable than VIN
- No diode drop

**Cons**:
- Not all boards have this pin
- Bypasses protection circuits
- Must be clean 5V

### 3. 3.3V Pin (Direct 3.3V)
**What it is**: Direct connection to 3.3V regulator output

**Pros**:
- Most reliable if you have regulated 3.3V supply
- No regulator inefficiency

**Cons**:
- Requires external 3.3V regulator
- Must handle full ESP32 current (up to 500mA peak)

### 4. USB (Development Standard)
**What it is**: USB 5V through proper power management

**Pros**:
- Most reliable ✓
- Built-in protection
- Easy for development

**Cons**:
- Requires USB cable
- Not ideal for final installation

## Recommendation for Testing

**Right now**: Use USB power
- Most reliable
- You need serial monitor anyway
- Keeps things simple while debugging displays

## Future: Final Installation Power

When ready for permanent installation, options:

### Option A: Dedicated 5V Regulator
```
12V → Buck Converter → 5V → ESP32 "5V" pin
                      ↓
                    3.3V (onboard reg) → ESP32, Display, MCP23017
```

### Option B: Dual Regulators
```
12V → Buck #1 → 5V → Solenoid/Lamp logic
    → Buck #2 → 3.3V → ESP32 3.3V pin, Display, MCP23017
```

### Option C: USB Power Supply
```
12V → USB Power Supply (5V 2A) → ESP32 USB port
```

## Current Consumption Budget

| Device | Current | Notes |
|--------|---------|-------|
| ESP32 | 80-250mA | Active, WiFi on |
| MCP23017 | 1mA | I/O expander |
| SSD1306 (each) | 10-20mA | Per display |
| 5 displays | 50-100mA | Total |
| **Total 3.3V** | **~150-350mA** | |

## Action Items

**For now (testing)**:
- [x] Power ESP32 via USB
- [ ] Verify display gets 3.3V (NOT 5V!)
- [ ] Test display with USB power
- [ ] Confirm MCP23017 still works

**For later (production)**:
- [ ] Choose power architecture
- [ ] Test ESP32 with chosen power method
- [ ] Add bulk capacitors for stability
- [ ] Test under load (all solenoids + displays)

## Notes
- Your ESP32 board may have a faulty VIN circuit
- This is common and not a critical issue
- USB power is fine for all development/testing
- Can solve permanent power later when system is working
