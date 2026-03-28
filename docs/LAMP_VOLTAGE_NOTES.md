# Lamp Matrix Voltage Regulation - TODO

## Current Status
- **Power Supply:** 18V
- **Lamp Rating:** 6.3V (#44/#47 bulbs)
- **Problem:** Bulb burned out after 24 hours runtime
- **Hardware Issue:** P-channel MOSFETs soldered into cookie board, need replacement

## Lamp Life Calculation
Lamp life ∝ (V_rated / V_actual)^13

Running 6.3V bulbs at 18V:
- Life ratio = (6.3/18)^13 = **0.0002%** of rated life
- Normal 1000 hour bulb → **2 hours** at 18V!

## Solutions to Implement (when resuming lamp testing)

### Option 1: PWM Dimming (RECOMMENDED)
- Duty cycle: ~35% to achieve 6.3V effective
- Frequency: 200-500Hz (standard for pinball)
- **Pros:** No extra hardware, adjustable brightness, ESP32 has hardware PWM
- **Cons:** Need to verify no visible flicker
- **Implementation:** Use ESP32 LEDC peripheral on column drivers (L4-L7)

### Option 2: Buck Converter
- Step down 18V → 6.3V DC
- **Pros:** Clean DC voltage, proper lamp operation
- **Cons:** Extra component cost (~$3-5), generates heat
- **Part suggestions:** LM2596, XL4015, or similar

### Option 3: Series Resistor (NOT RECOMMENDED)
- Drop 11.7V across resistor
- **Cons:** Wastes 65% of power as heat, voltage varies with bulb current

### Option 4: Replace with 12V Bulbs
- Still overvoltage but better than 6.3V
- Life at 18V = (12/18)^13 = **0.5%** → 5 hours instead of 2 hours
- Not a real solution

## Next Steps When Resuming
1. Order new P-channel MOSFETs (IRF4905 or similar)
2. Implement PWM on column drivers (GPIO26, 27, 32, 33)
3. Test duty cycle settings for proper brightness
4. Measure actual lamp voltage with scope/multimeter
5. Do long-term burn test (24+ hours)

## Code Changes Needed
- Add LEDC PWM setup in `initLampMatrix()`
- Modify `lampMatrixTask()` to use `ledcWrite()` instead of `digitalWrite()` for columns
- Add adjustable brightness control (PWM duty cycle 0-100%)
- Keep ~200Hz refresh rate for matrix scanning

## Current Test Program
Located in: `src/combined_test_rtos.cpp`
- Function: `lampMatrixTask()`
- Currently tests Lamp 1 only (M1=GPIO23, L4=GPIO27)
- Cycles through all 4 polarity combinations
