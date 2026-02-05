/*
 * displays.cpp - HT16K33 LED 7-Segment Display Implementation
 * 
 * Manages single HT16K33 backpack with 6 LED 7-segment displays
 * VERIFIED CUSTOM SEGMENT MAPPING (non-standard wiring):
 * ROW0=RDP, ROW1=LDP, ROW2=G, ROW3=F, ROW4=E, ROW5=D, ROW6=C, ROW7=B, ROW8=A
 */

#include "displays.h"

// Global LED display object
Adafruit_7segment ledDisplay = Adafruit_7segment();

// Custom segment patterns for non-standard wiring
// Bit mapping: [8]=A [7]=B [6]=C [5]=D [4]=E [3]=F [2]=G [1]=LDP [0]=RDP
// Using 16-bit values for HT16K33 SOP-28 with ROW0-ROW15
const uint16_t segmentPatterns[16] = {
    0b0111111000,  // 0 = A,B,C,D,E,F (no G)
    0b0011000000,  // 1 = B,C
    0b0110110100,  // 2 = A,B,D,E,G
    0b0111100100,  // 3 = A,B,C,D,G
    0b0011001100,  // 4 = B,C,F,G
    0b0101101100,  // 5 = A,C,D,F,G
    0b0101111100,  // 6 = A,C,D,E,F,G
    0b0111000000,  // 7 = A,B,C
    0b0111111100,  // 8 = A,B,C,D,E,F,G
    0b0111101100,  // 9 = A,B,C,D,F,G
    0b0111011100,  // A = A,B,C,E,F,G
    0b0000111100,  // b = C,D,E,F,G
    0b0100110000,  // C = A,D,E,F
    0b0001110100,  // d = B,C,D,E,G
    0b0100111100,  // E = A,D,E,F,G
    0b0100011100   // F = A,E,F,G
};

/**
 * Initialize the HT16K33 LED display
 */
void initDisplay() {
    Serial.println("Initializing HT16K33 LED 7-Segment Display...");
    
    if (!ledDisplay.begin(HT16K33_ADDRESS)) {
        Serial.println("ERROR: HT16K33 not found at address 0x70!");
        return;
    }
    
    Serial.println("HT16K33 found at 0x70");
    
    // Set brightness (0-15)
    ledDisplay.setBrightness(10);
    
    // Display test pattern - all 8s on positions 0,1,2,3,4,7
    Serial.println("Showing test pattern on positions 0,1,2,3,4,7...");
    ledDisplay.clear();
    ledDisplay.writeDigitNum(0, 8, false);
    ledDisplay.writeDigitNum(1, 8, false);
    ledDisplay.writeDigitNum(2, 8, false);
    ledDisplay.writeDigitNum(3, 8, false);
    ledDisplay.writeDigitNum(4, 8, false);
    ledDisplay.writeDigitNum(5, 8, false);  // Skip positions 5 & 6
    ledDisplay.writeDisplay();
    
    Serial.println("Display initialized - 6 digits (trying position 7)...");
}

/**
 * Flashy startup test routine
 */
void displayStartupTest() {
    Serial.println("Running flashy display startup test...");
    
    // 1. Flash all segments 3 times
    for (uint8_t flash = 0; flash < 3; flash++) {
        // All 8s (all segments)
        Wire.beginTransmission(HT16K33_ADDRESS);
        Wire.write(0x00);
        for (uint8_t i = 0; i < 6; i++) {
            Wire.write(0b11111100);  // All segments except decimal points
            Wire.write(0b00000001);  // Bit 8 (A segment)
        }
        Wire.endTransmission();
        delay(150);
        
        // Blank
        Wire.beginTransmission(HT16K33_ADDRESS);
        Wire.write(0x00);
        for (uint8_t i = 0; i < 12; i++) Wire.write(0x00);
        Wire.endTransmission();
        delay(150);
    }
    
    // 2. Countdown from 9 to 0 on all displays
    for (int8_t num = 9; num >= 0; num--) {
        uint16_t pattern = segmentPatterns[num];
        Wire.beginTransmission(HT16K33_ADDRESS);
        Wire.write(0x00);
        for (uint8_t i = 0; i < 6; i++) {
            Wire.write(pattern & 0xFF);
            Wire.write((pattern >> 8) & 0xFF);
        }
        Wire.endTransmission();
        delay(80);
    }
    
    // 3. Sweep left to right with 8s
    for (uint8_t pos = 0; pos < 6; pos++) {
        Wire.beginTransmission(HT16K33_ADDRESS);
        Wire.write(0x00);
        for (uint8_t i = 0; i < 6; i++) {
            if (i == pos) {
                Wire.write(0b11111100);  // 8
                Wire.write(0b00000001);
            } else {
                Wire.write(0x00);
                Wire.write(0x00);
            }
        }
        Wire.endTransmission();
        delay(100);
    }
    
    // 4. Final flash and clear
    Wire.beginTransmission(HT16K33_ADDRESS);
    Wire.write(0x00);
    for (uint8_t i = 0; i < 6; i++) {
        Wire.write(0b11111100);
        Wire.write(0b00000001);
    }
    Wire.endTransmission();
    delay(200);
    
    Wire.beginTransmission(HT16K33_ADDRESS);
    Wire.write(0x00);
    for (uint8_t i = 0; i < 12; i++) Wire.write(0x00);
    Wire.endTransmission();
    
    Serial.println("Startup test complete!");
}

/**
 * Test individual segments on all 6 digits to verify mapping
 */
void testSegmentMapping() {
    Serial.println("\n=== SEGMENT MAPPING TEST ===");
    Serial.println("Testing each segment on all 6 digits...\n");
    
    const char* segNames[] = {"A", "B", "C", "D", "E", "F", "G"};
    const uint16_t segBits[] = {
        0b0100000000,  // ROW8 = A
        0b0010000000,  // ROW7 = B
        0b0001000000,  // ROW6 = C
        0b0000100000,  // ROW5 = D
        0b0000010000,  // ROW4 = E
        0b0000001000,  // ROW3 = F
        0b0000000100   // ROW2 = G
    };
    
    for (uint8_t seg = 0; seg < 7; seg++) {
        Serial.print("Lighting segment ");
        Serial.print(segNames[seg]);
        Serial.print(" (ROW");
        Serial.print(8 - seg);
        Serial.println(") on all 6 digits");
        
        // Write to all 6 digit positions
        Wire.beginTransmission(HT16K33_ADDRESS);
        Wire.write(0x00); // Start at address 0
        
        for (uint8_t pos = 0; pos < 6; pos++) {
            Wire.write(segBits[seg] & 0xFF);         // Lower 8 bits
            Wire.write((segBits[seg] >> 8) & 0xFF);  // Upper 8 bits
        }
        Wire.endTransmission();
        
        delay(2000);  // 2 seconds per segment
    }
    
    Serial.println("\nSegment test complete!\n");
    
    // Clear all displays
    Wire.beginTransmission(HT16K33_ADDRESS);
    Wire.write(0x00);
    for (uint8_t i = 0; i < 12; i++) {
        Wire.write(0x00);
    }
    Wire.endTransmission();
}

/**
 * Update the LED display with a 6-digit score
 * 
 * @param score Score value (0-999999)
 */
void updateLEDScore(uint32_t score) {
    if (score > MAX_SCORE) {
        score = MAX_SCORE;
    }
    
    ledDisplay.clear();
    
    // Extract digits
    uint8_t digits[6] = {
        (uint8_t)((score / 100000) % 10),  // Hundred thousands
        (uint8_t)((score / 10000) % 10),   // Ten thousands  
        (uint8_t)((score / 1000) % 10),    // Thousands
        (uint8_t)((score / 100) % 10),     // Hundreds
        (uint8_t)((score / 10) % 10),      // Tens
        (uint8_t)(score % 10)              // Ones
    };
    
    // Write 16-bit segment patterns directly to I2C
    // HT16K33 display RAM: address 0x00, then pairs of bytes for each position
    Wire.beginTransmission(HT16K33_ADDRESS);
    Wire.write(0x00); // Start at address 0
    
    for (uint8_t i = 0; i < 6; i++) {
        uint16_t pattern = segmentPatterns[digits[i]];
        Wire.write(pattern & 0xFF);         // Lower 8 bits (ROW0-7)
        Wire.write((pattern >> 8) & 0xFF);  // Upper 8 bits (ROW8-15)
    }
    
    Wire.endTransmission();
}

/**
 * Clear the display
 */
void clearDisplay() {
    ledDisplay.clear();
    ledDisplay.writeDisplay();
}

/**
 * Set display brightness
 * 
 * @param level Brightness level (0-15)
 */
void setDisplayBrightness(uint8_t level) {
    if (level > 15) {
        level = 15;
    }
    ledDisplay.setBrightness(level);
}

/**
 * Test all positions to see which ones work
 * Useful for debugging position mapping
 */
void testDisplayPositions() {
    Serial.println("\n=== Testing HT16K33 Positions ===");
    
    for (uint8_t pos = 0; pos < 8; pos++) {
        ledDisplay.clear();
        ledDisplay.writeDigitNum(pos, 8, false);  // Show '8' on this position
        ledDisplay.writeDisplay();
        
        Serial.print("Position ");
        Serial.print(pos);
        Serial.println(" - showing '8'");
        
        delay(1000);
    }
    
    ledDisplay.clear();
    ledDisplay.writeDisplay();
    
    Serial.println("=================================\n");
}
