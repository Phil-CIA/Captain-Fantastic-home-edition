# Single Display Test - Troubleshooting Guide

## Purpose
This test verifies ONE SSD1306 OLED display on ONE multiplexer channel before attempting to work with all 5 displays. This isolates hardware issues and confirms the I2C setup.

## Hardware Setup

### Connections
```
ESP32          TCA9548A Multiplexer
GPIO21 (SDA) → SDA
GPIO22 (SCL) → SCL
3.3V         → VCC
GND          → GND

TCA9548A       SSD1306 Display (Channel 0)
SC0/SD0      → SCL/SDA of OLED
```

### Power Requirements
- **ESP32**: 5V via USB or 3.3V regulated
- **TCA9548A**: 3.3V (from ESP32)
- **SSD1306 OLED**: 3.3V (from ESP32 or multiplexer)
- **Important**: Ensure adequate current capacity (~40-50mA per display)

## Running the Test

### Build and Upload
```bash
# Option 1: Using PlatformIO CLI
pio run -e test_single_display -t upload -t monitor

# Option 2: Using VS Code
# Select environment: "test_single_display" in status bar
# Click Upload button
# Click Monitor button
```

### Expected Serial Output
```
========================================
   SSD1306 Single Display Test
   Captain Fantastic Project
========================================

Initializing I2C...
I2C initialized at 100kHz

=== Scanning I2C Bus ===
Found device at 0x70 (TCA9548A Multiplexer)
Total devices found: 1
========================

=== Testing Multiplexer Channels ===
Channel 0: Found display at 0x3C
Channel 1: No display found
Channel 2: No display found
...
====================================

=== Initializing Display ===
Attempting to initialize SSD1306 at 0x3C on channel 0
SUCCESS: Display initialized!
...
```

## Troubleshooting

### Problem: "No devices found" on I2C scan

**Possible Causes**:
1. Incorrect wiring (SDA/SCL swapped or not connected)
2. Wrong I2C pins (ESP32 uses GPIO21=SDA, GPIO22=SCL by default)
3. No pull-up resistors on I2C bus (most boards have them built-in)
4. Power issue (check 3.3V is present)

**Solutions**:
- Double-check all connections with multimeter
- Verify GPIO21/22 are not used by other peripherals
- Try adding external 4.7kΩ pull-up resistors on SDA and SCL to 3.3V
- Measure voltage at multiplexer VCC pin (should be 3.3V)

### Problem: "Multiplexer found but no display on any channel"

**Possible Causes**:
1. Display not connected to multiplexer
2. Display connected to wrong channel
3. Faulty display
4. Wrong I2C address (some SSD1306 use 0x3D instead of 0x3C)

**Solutions**:
- Verify display is connected to SD0/SC0 on multiplexer (Channel 0)
- Try connecting display to different channels
- Test display standalone (bypass multiplexer temporarily)
- Check solder joints on display I2C pads
- Modify code to scan 0x3D: change `#define SSD1306_ADDRESS 0x3D`

### Problem: "Display found but initialization failed"

**Possible Causes**:
1. Insufficient power supply current
2. I2C bus speed too fast
3. Poor connections (intermittent)
4. Defective display

**Solutions**:
- Reduce I2C speed to 50kHz: `Wire.setClock(50000);`
- Add 100µF capacitor near display VCC/GND
- Use shorter I2C wires (< 6 inches)
- Try different power source
- Replace display

### Problem: "Display initializes but shows nothing"

**Possible Causes**:
1. Wrong display type (128x32 vs 128x64)
2. Display in sleep mode
3. Contrast set too low
4. Multiplexer channel not selected

**Solutions**:
- Verify display is actually 128x64 (measure physical size)
- Send wake command: `display.ssd1306_command(SSD1306_DISPLAYON);`
- Increase contrast: `display.setContrast(255);`
- Confirm `selectMuxChannel(0)` is called before display commands

### Problem: "Display works but characters are garbled/distorted"

**Possible Causes**:
1. I2C communication errors
2. Bus speed too high
3. Electrical noise
4. Buffer corruption

**Solutions**:
- Lower I2C speed to 100kHz (already default in test)
- Add 0.1µF ceramic caps between VCC/GND on all devices
- Keep I2C wires away from power wires
- Add ferrite beads on I2C lines
- Check for ground loops

## Understanding the Test Sequence

### Step 1: I2C Bus Scan
Scans addresses 0x01 to 0x7F looking for devices.
- **Expected**: Find TCA9548A at 0x70
- **Note**: Display won't appear here (it's behind the mux)

### Step 2: Multiplexer Channel Scan  
Enables each mux channel (0-7) and scans for displays.
- **Expected**: Find SSD1306 at 0x3C on channel 0
- **Channels 1-7**: Should show "No display found" (unless you have more connected)

### Step 3: Display Initialization
Selects channel 0 and initializes the SSD1306.
- Allocates frame buffer
- Configures display parameters
- Clears screen

### Step 4: Rendering Tests
1. **Fill screen white** - Verifies all pixels work
2. **Clear screen** - Verifies clearing works
3. **Text rendering** - Tests built-in fonts
4. **Shape drawing** - Tests graphics primitives
5. **Pixel grid** - Tests individual pixel control

### Step 5: 7-Segment Simulation
Draws a simple "8" using rectangles to test the concept of rendering vintage-style segments.

## Interactive Commands

Once the test is running, you can send commands via Serial Monitor (115200 baud):

| Command | Action |
|---------|--------|
| `c` | Clear display |
| `t` | Show test text |
| `s` | Draw test shapes |
| `8` | Show 7-segment "8" |
| `i` | Re-scan I2C bus |
| `m` | Re-test multiplexer |
| `r` | Re-initialize display |
| `?` or `h` | Show help |

## Success Criteria

✅ **I2C scan finds TCA9548A at 0x70**  
✅ **Mux channel 0 finds SSD1306 at 0x3C**  
✅ **Display initializes successfully**  
✅ **All rendering tests display correctly**  
✅ **7-segment "8" is visible and clear**  

If all these pass, you're ready to:
1. Connect displays to channels 1-4
2. Run the full 5-display test (`test_oled_full` environment)
3. Implement the complete display system

## Next Steps After Success

### Adding More Displays
1. Connect second display to Channel 1 (SD1/SC1)
2. Modify `TEST_CHANNEL` to test channel 1
3. Repeat until all 5 displays work
4. Update to `test_oled_full` environment

### Common Display Address Configuration
If you have multiple displays with the same address (0x3C), the multiplexer solves this by isolating them on separate channels. Each display can use 0x3C because only one channel is active at a time.

### Performance Optimization
Once working:
- Increase I2C speed to 400kHz: `Wire.setClock(400000);`
- Test refresh rate
- Optimize rendering code

## Measuring Success

### Visual Inspection
- Display should show bright, crisp pixels
- No flickering
- No partial images
- Quick response to commands

### Serial Output
- No I2C errors
- All tests pass
- Consistent response times

### Electrical Check
- 3.3V stable at display VCC
- Current draw reasonable (10-40mA per display)
- No voltage drops during operation
