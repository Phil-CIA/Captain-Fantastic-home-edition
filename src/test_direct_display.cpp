/**
 * test_direct_display.cpp
 * 
 * Ultra-simple test - ONE SSD1306 display connected DIRECTLY to ESP32
 * No multiplexer needed - just ESP32 + Display
 * 
 * Hardware setup:
 * ESP32          SSD1306 Display
 * GPIO21 (SDA) → SDA
 * GPIO22 (SCL) → SCL
 * 3.3V         → VCC
 * GND          → GND
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Display configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SSD1306_ADDRESS 0x3C  // Try 0x3D if 0x3C doesn't work

// Display object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

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
            
            if (address == 0x3C || address == 0x3D) {
                Serial.print(" (Likely SSD1306 Display)");
            }
            
            Serial.println();
            deviceCount++;
        }
    }
    
    if (deviceCount == 0) {
        Serial.println("ERROR: No I2C devices found!");
        Serial.println("Check connections:");
        Serial.println("  - SDA connected to GPIO21");
        Serial.println("  - SCL connected to GPIO22");
        Serial.println("  - VCC connected to 3.3V");
        Serial.println("  - GND connected to GND");
    } else {
        Serial.print("Total devices found: ");
        Serial.println(deviceCount);
    }
    
    Serial.println("========================\n");
}

/**
 * Initialize the display
 */
bool initDisplay() {
    Serial.println("\n=== Initializing Display ===");
    
    Serial.print("Attempting to initialize SSD1306 at 0x");
    Serial.println(SSD1306_ADDRESS, HEX);
    
    if (!display.begin(SSD1306_SWITCHCAPVCC, SSD1306_ADDRESS)) {
        Serial.println("\nERROR: Display initialization failed!");
        Serial.println("\nTroubleshooting:");
        Serial.println("1. Check if device found at 0x3C in I2C scan above");
        Serial.println("2. If found at 0x3D, change SSD1306_ADDRESS to 0x3D");
        Serial.println("3. Verify wiring:");
        Serial.println("   ESP32 GPIO21 → Display SDA");
        Serial.println("   ESP32 GPIO22 → Display SCL");
        Serial.println("   ESP32 3.3V   → Display VCC");
        Serial.println("   ESP32 GND    → Display GND");
        Serial.println("4. Try a different display if available");
        return false;
    }
    
    Serial.println("SUCCESS: Display initialized!");
    
    // Clear display
    display.clearDisplay();
    display.display();
    
    return true;
}

/**
 * Draw a simple 7-segment style digit "8"
 */
void drawSegment8() {
    display.clearDisplay();
    
    // Draw "8" using filled rectangles (simple segments, no slant yet)
    int x = 30;  // Center offset
    int y = 8;   // Top offset
    
    // Segment A (top horizontal)
    display.fillRect(x + 10, y, 30, 6, SSD1306_WHITE);
    
    // Segment B (top right vertical)
    display.fillRect(x + 40, y + 6, 6, 18, SSD1306_WHITE);
    
    // Segment C (bottom right vertical)
    display.fillRect(x + 40, y + 30, 6, 18, SSD1306_WHITE);
    
    // Segment D (bottom horizontal)
    display.fillRect(x + 10, y + 48, 30, 6, SSD1306_WHITE);
    
    // Segment E (bottom left vertical)
    display.fillRect(x + 4, y + 30, 6, 18, SSD1306_WHITE);
    
    // Segment F (top left vertical)
    display.fillRect(x + 4, y + 6, 6, 18, SSD1306_WHITE);
    
    // Segment G (middle horizontal)
    display.fillRect(x + 10, y + 24, 30, 6, SSD1306_WHITE);
    
    display.display();
}

/**
 * Display all digits 0-9 in 7-segment style
 */
void displayDigit(uint8_t digit) {
    // Segment patterns for digits 0-9
    // Pattern: A B C D E F G
    bool segments[10][7] = {
        {1,1,1,1,1,1,0}, // 0
        {0,1,1,0,0,0,0}, // 1
        {1,1,0,1,1,0,1}, // 2
        {1,1,1,1,0,0,1}, // 3
        {0,1,1,0,0,1,1}, // 4
        {1,0,1,1,0,1,1}, // 5
        {1,0,1,1,1,1,1}, // 6
        {1,1,1,0,0,0,0}, // 7
        {1,1,1,1,1,1,1}, // 8
        {1,1,1,1,0,1,1}  // 9
    };
    
    if (digit > 9) digit = 0;
    
    display.clearDisplay();
    
    int x = 30;  // Center offset
    int y = 8;   // Top offset
    
    // Segment A (top)
    if (segments[digit][0]) 
        display.fillRect(x + 10, y, 30, 6, SSD1306_WHITE);
    
    // Segment B (top right)
    if (segments[digit][1]) 
        display.fillRect(x + 40, y + 6, 6, 18, SSD1306_WHITE);
    
    // Segment C (bottom right)
    if (segments[digit][2]) 
        display.fillRect(x + 40, y + 30, 6, 18, SSD1306_WHITE);
    
    // Segment D (bottom)
    if (segments[digit][3]) 
        display.fillRect(x + 10, y + 48, 30, 6, SSD1306_WHITE);
    
    // Segment E (bottom left)
    if (segments[digit][4]) 
        display.fillRect(x + 4, y + 30, 6, 18, SSD1306_WHITE);
    
    // Segment F (top left)
    if (segments[digit][5]) 
        display.fillRect(x + 4, y + 6, 6, 18, SSD1306_WHITE);
    
    // Segment G (middle)
    if (segments[digit][6]) 
        display.fillRect(x + 10, y + 24, 30, 6, SSD1306_WHITE);
    
    display.display();
}

void setup() {
    // Initialize serial
    Serial.begin(115200);
    delay(2000);  // Wait for serial to connect
    
    Serial.println("\n\n========================================");
    Serial.println("  Direct SSD1306 Display Test");
    Serial.println("  No Multiplexer Required");
    Serial.println("========================================\n");
    
    Serial.println("⚠️  CRITICAL: Verify display voltage FIRST!");
    Serial.println("SSD1306 displays are 3.3V devices - NOT 5V!\n");
    
    Serial.println("Correct Wiring:");
    Serial.println("  ESP32 GPIO21 → Display SDA");
    Serial.println("  ESP32 GPIO22 → Display SCL");
    Serial.println("  ESP32 3.3V   → Display VCC (NOT 5V!)");
    Serial.println("  ESP32 GND    → Display GND");
    Serial.println();
    Serial.println("Shared I2C with MCP23017:");
    Serial.println("  - Both use same SDA/SCL lines");
    Serial.println("  - Both use 4.7k pullups to 3.3V");
    Serial.println("  - MCP23017 should still work\n");
    
    // Initialize I2C
    Serial.println("Initializing I2C...");
    Wire.begin(21, 22);  // SDA=21, SCL=22
    Wire.setClock(100000);  // 100kHz (conservative speed)
    Serial.println("I2C initialized at 100kHz\n");
    
    delay(500);
    
    // Scan I2C bus
    scanI2C();
    
    // Initialize display
    if (!initDisplay()) {
        Serial.println("\n*** FAILED TO INITIALIZE DISPLAY ***");
        Serial.println("Review troubleshooting steps above.");
        while (1) {
            delay(1000);
        }
    }
    
    // Success - show welcome message
    Serial.println("\n=== Display Working! ===\n");
    
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Captain");
    display.println("Fantastic");
    display.setTextSize(1);
    display.setCursor(0, 40);
    display.println("Display OK!");
    display.display();
    
    delay(2000);
    
    // Test: Count 0-9 in 7-segment style
    Serial.println("Displaying digits 0-9 in 7-segment style...");
    for (uint8_t i = 0; i <= 9; i++) {
        Serial.print("Digit: ");
        Serial.println(i);
        displayDigit(i);
        delay(800);
    }
    
    Serial.println("\n========================================");
    Serial.println("  Test Complete!");
    Serial.println("========================================\n");
    Serial.println("Commands (send via Serial Monitor):");
    Serial.println("  0-9 : Display digit");
    Serial.println("  c   : Clear screen");
    Serial.println("  t   : Test text");
    Serial.println("  f   : Fill screen");
    Serial.println("  8   : Show segment 8");
    Serial.println("  i   : Re-scan I2C");
    Serial.println("  ?   : Help");
    Serial.println();
}

void loop() {
    if (Serial.available()) {
        char cmd = Serial.read();
        
        switch (cmd) {
            case '0' ... '9':
                Serial.print("Displaying digit: ");
                Serial.println(cmd - '0');
                displayDigit(cmd - '0');
                break;
                
            case 'c':
                Serial.println("Clearing display...");
                display.clearDisplay();
                display.display();
                break;
                
            case 't':
                Serial.println("Test text...");
                display.clearDisplay();
                display.setTextSize(2);
                display.setCursor(10, 10);
                display.println("HELLO!");
                display.setTextSize(1);
                display.setCursor(10, 40);
                display.println("Test 123");
                display.display();
                break;
                
            case 'f':
                Serial.println("Fill screen...");
                display.fillScreen(SSD1306_WHITE);
                display.display();
                delay(1000);
                display.clearDisplay();
                display.display();
                break;
                
            case 'i':
                Serial.println("Scanning I2C...");
                scanI2C();
                break;
                
            case 'a':
                Serial.println("Auto-count 0-9...");
                for (uint8_t i = 0; i <= 9; i++) {
                    displayDigit(i);
                    delay(500);
                }
                break;
                
            case '?':
            case 'h':
                Serial.println("\n=== Commands ===");
                Serial.println("0-9 : Display digit");
                Serial.println("c   : Clear screen");
                Serial.println("t   : Test text");
                Serial.println("f   : Fill screen");
                Serial.println("8   : Show segment 8");
                Serial.println("i   : Re-scan I2C");
                Serial.println("a   : Auto count 0-9");
                Serial.println("?/h : This help");
                Serial.println("================\n");
                break;
        }
    }
    
    delay(10);
}
