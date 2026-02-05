/**
 * i2c_diagnostic.cpp
 * 
 * Diagnose I2C bus issues and verify system state
 * 
 * IMPORTANT SAFETY CHECKS:
 * - Verifies I2C bus is not locked
 * - Scans for all devices
 * - Checks for conflicts
 * - Tests bus recovery
 */

#include <Arduino.h>
#include <Wire.h>

#define I2C_SDA 21
#define I2C_SCL 22

// Known device addresses
#define MCP23017_ADDRESS 0x20  // Solenoid controller
#define TCA9548A_ADDRESS 0x70  // Multiplexer (when you get it)
#define SSD1306_ADDRESS_1 0x3C // Display option 1
#define SSD1306_ADDRESS_2 0x3D // Display option 2

bool i2cBusLocked = false;

/**
 * Check if I2C bus is locked (SDA stuck low)
 */
void checkBusLock() {
    Serial.println("\n=== I2C Bus Lock Check ===");
    
    // Read SDA and SCL pin states before initializing I2C
    pinMode(I2C_SDA, INPUT);
    pinMode(I2C_SCL, INPUT);
    
    delay(10);
    
    bool sdaState = digitalRead(I2C_SDA);
    bool sclState = digitalRead(I2C_SCL);
    
    Serial.print("SDA (GPIO21) state: ");
    Serial.println(sdaState ? "HIGH (OK)" : "LOW (PROBLEM!)");
    Serial.print("SCL (GPIO22) state: ");
    Serial.println(sclState ? "HIGH (OK)" : "LOW (PROBLEM!)");
    
    if (!sdaState || !sclState) {
        Serial.println("\n⚠️  WARNING: I2C bus may be locked!");
        Serial.println("One or both lines stuck LOW.");
        Serial.println("This can happen when:");
        Serial.println("  - Device connected during communication");
        Serial.println("  - Device lost power mid-transaction");
        Serial.println("  - Device voltage mismatch (3.3V vs 5V)");
        i2cBusLocked = true;
    } else {
        Serial.println("✓ I2C bus appears OK (both lines HIGH)");
    }
    
    Serial.println("==========================\n");
}

/**
 * Attempt to recover locked I2C bus
 * Sends clock pulses to release any device holding SDA low
 */
void recoverI2CBus() {
    Serial.println("\n=== Attempting I2C Bus Recovery ===");
    
    // Manually bit-bang clock pulses
    pinMode(I2C_SCL, OUTPUT);
    pinMode(I2C_SDA, INPUT_PULLUP);
    
    Serial.println("Sending clock pulses to release bus...");
    
    for (int i = 0; i < 16; i++) {
        digitalWrite(I2C_SCL, LOW);
        delayMicroseconds(5);
        digitalWrite(I2C_SCL, HIGH);
        delayMicroseconds(5);
        
        if (digitalRead(I2C_SDA)) {
            Serial.print("✓ SDA released after ");
            Serial.print(i + 1);
            Serial.println(" clock pulses");
            break;
        }
    }
    
    // Send STOP condition
    pinMode(I2C_SDA, OUTPUT);
    digitalWrite(I2C_SDA, LOW);
    delayMicroseconds(5);
    digitalWrite(I2C_SCL, HIGH);
    delayMicroseconds(5);
    digitalWrite(I2C_SDA, HIGH);
    delayMicroseconds(5);
    
    Serial.println("Recovery attempt complete");
    Serial.println("===================================\n");
    
    delay(100);
}

/**
 * Comprehensive I2C bus scan
 */
void scanI2CBus() {
    Serial.println("\n=== I2C Bus Scan ===");
    Serial.println("Scanning addresses 0x01 to 0x7F...\n");
    
    uint8_t deviceCount = 0;
    uint8_t errorCount = 0;
    
    for (uint8_t address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        uint8_t error = Wire.endTransmission();
        
        if (error == 0) {
            // Device found
            Serial.print("✓ Device found at 0x");
            if (address < 16) Serial.print("0");
            Serial.print(address, HEX);
            Serial.print(" - ");
            
            // Identify known devices
            if (address == MCP23017_ADDRESS) {
                Serial.println("MCP23017 (Solenoid Controller)");
            } else if (address == TCA9548A_ADDRESS) {
                Serial.println("TCA9548A (I2C Multiplexer)");
            } else if (address == SSD1306_ADDRESS_1) {
                Serial.println("SSD1306 Display (0x3C)");
            } else if (address == SSD1306_ADDRESS_2) {
                Serial.println("SSD1306 Display (0x3D)");
            } else {
                Serial.println("Unknown Device");
            }
            
            deviceCount++;
        } else if (error == 4) {
            // Unknown error
            errorCount++;
        }
        
        delay(2);  // Small delay between probes
    }
    
    Serial.println();
    Serial.print("Devices found: ");
    Serial.println(deviceCount);
    
    if (errorCount > 0) {
        Serial.print("⚠️  Errors encountered: ");
        Serial.println(errorCount);
        Serial.println("This may indicate bus problems");
    }
    
    Serial.println("====================\n");
}

/**
 * Test MCP23017 communication
 */
void testMCP23017() {
    Serial.println("\n=== Testing MCP23017 ===");
    
    Wire.beginTransmission(MCP23017_ADDRESS);
    uint8_t error = Wire.endTransmission();
    
    if (error != 0) {
        Serial.println("✗ MCP23017 not responding!");
        Serial.println("  Check if solenoid board is powered");
        return;
    }
    
    Serial.println("✓ MCP23017 responding");
    
    // Try to read IODIRA register (should be 0xFF on reset)
    Wire.beginTransmission(MCP23017_ADDRESS);
    Wire.write(0x00);  // IODIRA register
    error = Wire.endTransmission();
    
    if (error == 0) {
        Wire.requestFrom((int)MCP23017_ADDRESS, (int)1);
        if (Wire.available()) {
            uint8_t value = Wire.read();
            Serial.print("  IODIRA register: 0x");
            Serial.println(value, HEX);
        }
    }
    
    Serial.println("========================\n");
}

/**
 * Check display voltage and provide warnings
 */
void displayVoltageWarning() {
    Serial.println("\n⚠️  CRITICAL: DISPLAY VOLTAGE WARNING ⚠️");
    Serial.println("==========================================");
    Serial.println("SSD1306 OLED displays are 3.3V devices!");
    Serial.println();
    Serial.println("YOU MENTIONED DISPLAY IS ON 5V - THIS IS WRONG!");
    Serial.println();
    Serial.println("Correct connections:");
    Serial.println("  Display VCC → ESP32 3.3V (NOT 5V!)");
    Serial.println("  Display GND → ESP32 GND");
    Serial.println("  Display SDA → ESP32 GPIO21 (via 4.7k pullup to 3.3V)");
    Serial.println("  Display SCL → ESP32 GPIO22 (via 4.7k pullup to 3.3V)");
    Serial.println();
    Serial.println("The 4.7k pullups should go to 3.3V, NOT 5V!");
    Serial.println();
    Serial.println("If display was powered at 5V, it may be damaged.");
    Serial.println("==========================================\n");
}

/**
 * Provide troubleshooting guide
 */
void printTroubleshooting() {
    Serial.println("\n=== Troubleshooting Guide ===");
    Serial.println();
    Serial.println("ISSUE: Program stopped when SDA connected");
    Serial.println("CAUSE: Device on bus caused conflict/lock");
    Serial.println();
    Serial.println("SOLUTIONS:");
    Serial.println("1. NEVER hot-plug I2C devices while system running");
    Serial.println("2. Always power down before connecting/disconnecting");
    Serial.println("3. Verify ALL devices are 3.3V compatible");
    Serial.println("4. Check pullup resistors go to 3.3V, not 5V");
    Serial.println();
    Serial.println("CURRENT SITUATION:");
    Serial.println("- MCP23017 is on bus (for solenoids)");
    Serial.println("- MCP23017 uses 4.7k pullups to 3.3V");
    Serial.println("- SSD1306 display MUST use same 3.3V level");
    Serial.println("- Display VCC MUST be 3.3V (NOT 5V!)");
    Serial.println();
    Serial.println("SAFE PROCEDURE:");
    Serial.println("1. Power down ESP32 completely");
    Serial.println("2. Disconnect display VCC (it's on 5V - WRONG!)");
    Serial.println("3. Connect display VCC to 3.3V rail");
    Serial.println("4. Verify pullups are to 3.3V");
    Serial.println("5. Connect SDA and SCL");
    Serial.println("6. Power up ESP32");
    Serial.println("7. Run this diagnostic");
    Serial.println("=============================\n");
}

void setup() {
    Serial.begin(115200);
    delay(3000);  // Long delay to ensure serial is ready
    
    Serial.println("\n\n");
    Serial.println("================================================");
    Serial.println("    I2C Bus Diagnostic Tool");
    Serial.println("    Captain Fantastic Project");
    Serial.println("================================================\n");
    
    // CRITICAL: Display voltage warning
    displayVoltageWarning();
    
    delay(2000);
    
    // Step 1: Check if bus is locked BEFORE initializing I2C
    checkBusLock();
    
    // Step 2: Attempt recovery if needed
    if (i2cBusLocked) {
        Serial.println("⚠️  Bus appears locked - attempting recovery...\n");
        recoverI2CBus();
        
        // Re-check
        checkBusLock();
    }
    
    // Step 3: Initialize I2C
    Serial.println("Initializing I2C...");
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);  // Conservative 100kHz
    Serial.println("I2C initialized at 100kHz\n");
    
    delay(100);
    
    // Step 4: Scan bus
    scanI2CBus();
    
    // Step 5: Test MCP23017 specifically
    testMCP23017();
    
    // Step 6: Provide troubleshooting
    printTroubleshooting();
    
    Serial.println("\n================================================");
    Serial.println("Diagnostic complete.");
    Serial.println("================================================\n");
    Serial.println("Commands:");
    Serial.println("  s - Scan I2C bus again");
    Serial.println("  r - Attempt bus recovery");
    Serial.println("  m - Test MCP23017");
    Serial.println("  c - Check bus lock status");
    Serial.println("  ? - Help");
    Serial.println();
}

void loop() {
    if (Serial.available()) {
        char cmd = Serial.read();
        
        switch (cmd) {
            case 's':
                Serial.println("\nRe-scanning I2C bus...");
                scanI2CBus();
                break;
                
            case 'r':
                Serial.println("\nAttempting bus recovery...");
                Wire.end();
                delay(100);
                recoverI2CBus();
                delay(100);
                Wire.begin(I2C_SDA, I2C_SCL);
                Wire.setClock(100000);
                delay(100);
                scanI2CBus();
                break;
                
            case 'm':
                testMCP23017();
                break;
                
            case 'c':
                Wire.end();
                delay(100);
                checkBusLock();
                Wire.begin(I2C_SDA, I2C_SCL);
                Wire.setClock(100000);
                break;
                
            case '?':
            case 'h':
                printTroubleshooting();
                break;
                
            default:
                break;
        }
    }
    
    delay(10);
}
