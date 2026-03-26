/**
 * combined_test.cpp
 * 
 * Combined Display + Solenoid Test
 * - Display cycles through digits 0-9
 * - Solenoids cycle through all 8 outputs
 * - Both running simultaneously
 * - Serial commands to control both
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ========== DISPLAY CONFIGURATION ==========
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SSD1306_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ========== MCP23017 CONFIGURATION ==========
#define MCP23017_ADDRESS 0x20

// MCP23017 Register addresses
#define IODIRA 0x00   // I/O Direction Register A
#define IODIRB 0x01   // I/O Direction Register B
#define GPIOA  0x12   // GPIO Register A
#define GPIOB  0x13   // GPIO Register B

// ========== TIMING CONFIGURATION ==========
unsigned long lastDisplayUpdate = 0;
unsigned long lastSolenoidUpdate = 0;
const unsigned long DISPLAY_INTERVAL = 1000;  // Update display every 1 second
const unsigned long SOLENOID_INTERVAL = 500;  // Pulse solenoids every 500ms

// ========== STATE VARIABLES ==========
uint8_t currentDigit = 0;
uint8_t currentSolenoid = 0;
bool solenoidEnabled = true;
bool displayEnabled = true;
bool solenoidState = false;

// ========== MCP23017 FUNCTIONS ==========

void writeMCP23017(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(MCP23017_ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
    delayMicroseconds(100);  // Give I2C bus time to settle
}

uint8_t readMCP23017(uint8_t reg) {
    Wire.beginTransmission(MCP23017_ADDRESS);
    Wire.write(reg);
    Wire.endTransmission();
    delayMicroseconds(100);  // Give I2C bus time to settle
    Wire.requestFrom((int)MCP23017_ADDRESS, (int)1);
    return Wire.read();
}

void initMCP23017() {
    Serial.println("Initializing MCP23017...");
    
    // Set all pins as outputs (0 = output, 1 = input)
    writeMCP23017(IODIRA, 0x00);  // Port A all outputs
    writeMCP23017(IODIRB, 0x00);  // Port B all outputs
    
    // Turn all outputs off initially
    writeMCP23017(GPIOA, 0x00);
    writeMCP23017(GPIOB, 0x00);
    
    Serial.println("MCP23017 initialized - all outputs OFF");
}

void fireSolenoid(uint8_t number) {
    if (number > 7) return;
    
    uint8_t port = (number < 4) ? GPIOA : GPIOB;
    uint8_t pin = (number < 4) ? number : (number - 4);
    uint8_t mask = 1 << pin;
    
    // Turn on
    writeMCP23017(port, mask);
    delayMicroseconds(100);  // Ensure write completes
    Serial.print("Solenoid ");
    Serial.print(number);
    Serial.println(" ON");
}

void allSolenoidsOff() {
    writeMCP23017(GPIOA, 0x00);
    writeMCP23017(GPIOB, 0x00);
    delayMicroseconds(100);  // Ensure writes complete
}

// ========== DISPLAY FUNCTIONS ==========

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
    
    // Add text label at bottom
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(40, 56);
    display.print("Digit ");
    display.print(digit);
    
    display.display();
}

void initDisplay() {
    Serial.println("Initializing Display...");
    
    if (!display.begin(SSD1306_SWITCHCAPVCC, SSD1306_ADDRESS)) {
        Serial.println("ERROR: Display initialization failed!");
        return;
    }
    
    Serial.println("Display initialized");
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 10);
    display.println("Captain");
    display.println("Fantastic");
    display.display();
    delay(1000);
}

// ========== I2C SCAN ==========

void scanI2C() {
    Serial.println("\n=== I2C Bus Scan ===");
    uint8_t deviceCount = 0;
    
    for (uint8_t address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        uint8_t error = Wire.endTransmission();
        
        if (error == 0) {
            Serial.print("Found device at 0x");
            if (address < 16) Serial.print("0");
            Serial.print(address, HEX);
            
            if (address == MCP23017_ADDRESS) {
                Serial.println(" (MCP23017 - Solenoids)");
            } else if (address == SSD1306_ADDRESS) {
                Serial.println(" (SSD1306 - Display)");
            } else {
                Serial.println();
            }
            deviceCount++;
        }
    }
    
    Serial.print("Total devices: ");
    Serial.println(deviceCount);
    Serial.println("====================\n");
}

// ========== SETUP ==========

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n\n========================================");
    Serial.println("   Combined Display + Solenoid Test");
    Serial.println("   Captain Fantastic Project");
    Serial.println("========================================\n");
    
    // Initialize I2C
    Serial.println("Initializing I2C at 100kHz...");
    Wire.begin(21, 22);
    Wire.setClock(100000);
    delay(100);
    
    // Scan I2C bus
    scanI2C();
    
    // Initialize devices
    initMCP23017();
    initDisplay();
    
    Serial.println("\n========================================");
    Serial.println("System Ready!");
    Serial.println("========================================\n");
    Serial.println("Auto-running:");
    Serial.println("  - Display cycles 0-9 (every 1 sec)");
    Serial.println("  - Solenoids pulse 0-7 (every 500ms)");
    Serial.println("\nCommands:");
    Serial.println("  s - Toggle solenoids ON/OFF");
    Serial.println("  d - Toggle display ON/OFF");
    Serial.println("  0-7 - Fire specific solenoid");
    Serial.println("  x - All solenoids OFF");
    Serial.println("  i - Re-scan I2C");
    Serial.println("  ? - Help");
    Serial.println();
}

// ========== MAIN LOOP ==========

void loop() {
    unsigned long currentTime = millis();
    
    // ===== UPDATE DISPLAY =====
    if (displayEnabled && (currentTime - lastDisplayUpdate >= DISPLAY_INTERVAL)) {
        lastDisplayUpdate = currentTime;
        
        // Ensure solenoids are off before display update
        allSolenoidsOff();
        delay(10);  // Let I2C bus settle
        
        displayDigit(currentDigit);
        Serial.print("Display: ");
        Serial.println(currentDigit);
        
        currentDigit++;
        if (currentDigit > 9) currentDigit = 0;
        
        delay(10);  // Let I2C bus settle after display
    }
    
    // ===== UPDATE SOLENOIDS =====
    if (solenoidEnabled && (currentTime - lastSolenoidUpdate >= SOLENOID_INTERVAL)) {
        lastSolenoidUpdate = currentTime;
        
        delay(5);  // Let I2C bus settle before MCP access
        
        if (solenoidState) {
            // Turn off current solenoid
            allSolenoidsOff();
            solenoidState = false;
            
            // Move to next solenoid
            currentSolenoid++;
            if (currentSolenoid > 7) currentSolenoid = 0;
            
        } else {
            // Turn on current solenoid
            fireSolenoid(currentSolenoid);
            solenoidState = true;
        }
        
        delay(5);  // Let I2C bus settle after MCP access
    }
    
    // ===== HANDLE SERIAL COMMANDS =====
    if (Serial.available()) {
        char cmd = Serial.read();
        
        switch (cmd) {
            case 's':
                solenoidEnabled = !solenoidEnabled;
                Serial.print("Solenoids: ");
                Serial.println(solenoidEnabled ? "ENABLED" : "DISABLED");
                if (!solenoidEnabled) {
                    allSolenoidsOff();
                }
                break;
                
            case 'd':
                displayEnabled = !displayEnabled;
                Serial.print("Display cycling: ");
                Serial.println(displayEnabled ? "ENABLED" : "DISABLED");
                break;
                
            case '0' ... '7':
                Serial.print("Manual fire solenoid ");
                Serial.println(cmd - '0');
                allSolenoidsOff();
                fireSolenoid(cmd - '0');
                delay(100);
                allSolenoidsOff();
                break;
                
            case 'x':
                Serial.println("All solenoids OFF");
                allSolenoidsOff();
                break;
                
            case 'i':
                scanI2C();
                break;
                
            case '?':
            case 'h':
                Serial.println("\n=== Commands ===");
                Serial.println("s   - Toggle solenoids");
                Serial.println("d   - Toggle display");
                Serial.println("0-7 - Fire solenoid");
                Serial.println("x   - All solenoids OFF");
                Serial.println("i   - Scan I2C");
                Serial.println("?/h - Help");
                Serial.println("================\n");
                break;
        }
    }
}
