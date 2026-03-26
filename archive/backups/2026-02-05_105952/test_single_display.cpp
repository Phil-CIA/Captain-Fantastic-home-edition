/**
 * test_single_display.cpp
 * 
 * Simple test to get ONE SSD1306 OLED display working
 * Tests basic I2C communication and display initialization
 * 
 * Hardware setup:
 * - ESP32 GPIO21 (SDA), GPIO22 (SCL)
 * - TCA9548A multiplexer at 0x70
 * - SSD1306 OLED at 0x3C on channel 0
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Display configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SSD1306_ADDRESS 0x3C

// Multiplexer configuration
#define MUX_ADDRESS 0x70
#define TEST_CHANNEL 0    // Test with channel 0 first

// Display object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

/**
 * Select a channel on the TCA9548A multiplexer
 */
bool selectMuxChannel(uint8_t channel) {
    if (channel > 7) {
        Serial.println("ERROR: Invalid channel (must be 0-7)");
        return false;
    }
    
    Wire.beginTransmission(MUX_ADDRESS);
    Wire.write(1 << channel);  // Enable only the selected channel
    uint8_t error = Wire.endTransmission();
    
    if (error != 0) {
        Serial.print("ERROR: Mux channel select failed, error=");
        Serial.println(error);
        return false;
    }
    
    Serial.print("Selected mux channel ");
    Serial.println(channel);
    return true;
}

/**
 * Disable all multiplexer channels
 */
void disableMuxChannels() {
    Wire.beginTransmission(MUX_ADDRESS);
    Wire.write(0);  // Disable all channels
    Wire.endTransmission();
}

/**
 * Scan I2C bus for devices
 */
void scanI2C() {
    Serial.println("\n=== Scanning I2C Bus ===");
    
    uint8_t deviceCount = 0;
    
    for (uint8_t address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        uint8_t error = Wire.endTransmission();
        
        if (error == 0) {
            Serial.print("Found device at 0x");
            if (address < 16) Serial.print("0");
            Serial.print(address, HEX);
            
            if (address == MUX_ADDRESS) {
                Serial.print(" (TCA9548A Multiplexer)");
            } else if (address == SSD1306_ADDRESS) {
                Serial.print(" (SSD1306 Display)");
            }
            
            Serial.println();
            deviceCount++;
        }
    }
    
    Serial.print("Total devices found: ");
    Serial.println(deviceCount);
    Serial.println("========================\n");
}

/**
 * Test multiplexer by scanning all channels
 */
void testMultiplexer() {
    Serial.println("\n=== Testing Multiplexer Channels ===");
    
    for (uint8_t channel = 0; channel < 8; channel++) {
        Serial.print("Channel ");
        Serial.print(channel);
        Serial.print(": ");
        
        if (!selectMuxChannel(channel)) {
            Serial.println("FAILED to select");
            continue;
        }
        
        delay(10);
        
        // Scan for devices on this channel
        bool foundDisplay = false;
        for (uint8_t addr = 0x3C; addr <= 0x3D; addr++) {  // Common SSD1306 addresses
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) {
                Serial.print("Found display at 0x");
                Serial.println(addr, HEX);
                foundDisplay = true;
            }
        }
        
        if (!foundDisplay) {
            Serial.println("No display found");
        }
        
        disableMuxChannels();
        delay(10);
    }
    
    Serial.println("====================================\n");
}

/**
 * Initialize the display
 */
bool initDisplay() {
    Serial.println("\n=== Initializing Display ===");
    
    // Select the test channel
    if (!selectMuxChannel(TEST_CHANNEL)) {
        Serial.println("ERROR: Cannot select mux channel");
        return false;
    }
    
    delay(50);  // Give mux time to stabilize
    
    // Try to initialize the display
    Serial.print("Attempting to initialize SSD1306 at 0x");
    Serial.print(SSD1306_ADDRESS, HEX);
    Serial.print(" on channel ");
    Serial.println(TEST_CHANNEL);
    
    if (!display.begin(SSD1306_SWITCHCAPVCC, SSD1306_ADDRESS)) {
        Serial.println("ERROR: SSD1306 allocation/initialization failed!");
        Serial.println("Possible issues:");
        Serial.println("  - Display not connected to channel 0");
        Serial.println("  - Wrong I2C address (try 0x3D)");
        Serial.println("  - Faulty display or wiring");
        Serial.println("  - Insufficient power");
        disableMuxChannels();
        return false;
    }
    
    Serial.println("SUCCESS: Display initialized!");
    
    // Clear display
    display.clearDisplay();
    display.display();
    
    // Don't disable channels yet - keep display active
    
    return true;
}

/**
 * Test basic display rendering
 */
void testDisplayRendering() {
    Serial.println("\n=== Testing Display Rendering ===");
    
    // Make sure we're on the right channel
    selectMuxChannel(TEST_CHANNEL);
    
    // Test 1: Fill screen
    Serial.println("Test 1: Fill screen");
    display.clearDisplay();
    display.fillScreen(SSD1306_WHITE);
    display.display();
    delay(1000);
    
    // Test 2: Clear screen
    Serial.println("Test 2: Clear screen");
    display.clearDisplay();
    display.display();
    delay(1000);
    
    // Test 3: Draw text
    Serial.println("Test 3: Text rendering");
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Captain");
    display.println("Fantastic");
    display.setTextSize(1);
    display.println();
    display.println("Display Test");
    display.display();
    delay(2000);
    
    // Test 4: Draw shapes
    Serial.println("Test 4: Drawing shapes");
    display.clearDisplay();
    display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
    display.drawCircle(64, 32, 20, SSD1306_WHITE);
    display.drawLine(0, 0, 127, 63, SSD1306_WHITE);
    display.display();
    delay(2000);
    
    // Test 5: Pixels
    Serial.println("Test 5: Individual pixels");
    display.clearDisplay();
    for (uint8_t y = 0; y < 64; y += 4) {
        for (uint8_t x = 0; x < 128; x += 4) {
            display.drawPixel(x, y, SSD1306_WHITE);
        }
    }
    display.display();
    delay(2000);
    
    Serial.println("============================\n");
}

/**
 * Test simple 7-segment style digit
 */
void testSegmentDigit() {
    Serial.println("\n=== Testing 7-Segment Style Digit ===");
    
    selectMuxChannel(TEST_CHANNEL);
    
    display.clearDisplay();
    
    // Draw a simple "8" using rectangles (no slant yet)
    // Segment A (top)
    display.fillRect(20, 5, 40, 8, SSD1306_WHITE);
    // Segment B (top right)
    display.fillRect(60, 13, 8, 20, SSD1306_WHITE);
    // Segment C (bottom right)
    display.fillRect(60, 41, 8, 20, SSD1306_WHITE);
    // Segment D (bottom)
    display.fillRect(20, 56, 40, 8, SSD1306_WHITE);
    // Segment E (bottom left)
    display.fillRect(12, 41, 8, 20, SSD1306_WHITE);
    // Segment F (top left)
    display.fillRect(12, 13, 8, 20, SSD1306_WHITE);
    // Segment G (middle)
    display.fillRect(20, 30, 40, 8, SSD1306_WHITE);
    
    display.display();
    
    Serial.println("Displaying '8' in 7-segment style");
    Serial.println("=====================================\n");
    
    delay(3000);
}

void setup() {
    // Initialize serial
    Serial.begin(115200);
    delay(2000);  // Give serial time to connect
    
    Serial.println("\n\n========================================");
    Serial.println("   SSD1306 Single Display Test");
    Serial.println("   Captain Fantastic Project");
    Serial.println("========================================\n");
    
    // Initialize I2C
    Serial.println("Initializing I2C...");
    Wire.begin(21, 22);  // SDA=21, SCL=22 for ESP32
    Wire.setClock(100000);  // Start with 100kHz (standard mode)
    Serial.println("I2C initialized at 100kHz");
    
    delay(500);
    
    // Step 1: Scan I2C bus (should see multiplexer)
    scanI2C();
    
    // Step 2: Test multiplexer channels
    testMultiplexer();
    
    // Step 3: Initialize display
    if (!initDisplay()) {
        Serial.println("\n*** DISPLAY INITIALIZATION FAILED ***");
        Serial.println("Check wiring and connections.");
        Serial.println("Test stopped.");
        while (1) {
            delay(1000);
        }
    }
    
    // Step 4: Test rendering
    testDisplayRendering();
    
    // Step 5: Test 7-segment style
    testSegmentDigit();
    
    Serial.println("\n========================================");
    Serial.println("   All Tests Complete!");
    Serial.println("========================================\n");
    Serial.println("Enter commands:");
    Serial.println("  c - Clear display");
    Serial.println("  t - Test text");
    Serial.println("  s - Test shapes");
    Serial.println("  8 - Test segment digit");
    Serial.println("  i - Re-scan I2C");
    Serial.println("  m - Test multiplexer");
    Serial.println("  r - Re-initialize display");
}

void loop() {
    // Interactive commands
    if (Serial.available()) {
        char cmd = Serial.read();
        
        // Make sure we're on the test channel
        selectMuxChannel(TEST_CHANNEL);
        
        switch (cmd) {
            case 'c':
                Serial.println("Clearing display...");
                display.clearDisplay();
                display.display();
                break;
                
            case 't':
                Serial.println("Testing text...");
                display.clearDisplay();
                display.setTextSize(2);
                display.setTextColor(SSD1306_WHITE);
                display.setCursor(10, 20);
                display.println("HELLO!");
                display.display();
                break;
                
            case 's':
                Serial.println("Testing shapes...");
                display.clearDisplay();
                display.drawRect(10, 10, 108, 44, SSD1306_WHITE);
                display.fillCircle(64, 32, 15, SSD1306_WHITE);
                display.display();
                break;
                
            case '8':
                testSegmentDigit();
                break;
                
            case 'i':
                disableMuxChannels();
                delay(100);
                scanI2C();
                break;
                
            case 'm':
                disableMuxChannels();
                delay(100);
                testMultiplexer();
                selectMuxChannel(TEST_CHANNEL);
                break;
                
            case 'r':
                disableMuxChannels();
                delay(100);
                initDisplay();
                break;
                
            case '?':
            case 'h':
                Serial.println("\n=== Commands ===");
                Serial.println("c - Clear display");
                Serial.println("t - Test text");
                Serial.println("s - Test shapes");
                Serial.println("8 - Test segment digit");
                Serial.println("i - Re-scan I2C");
                Serial.println("m - Test multiplexer");
                Serial.println("r - Re-initialize display");
                Serial.println("================\n");
                break;
        }
    }
    
    delay(10);
}
