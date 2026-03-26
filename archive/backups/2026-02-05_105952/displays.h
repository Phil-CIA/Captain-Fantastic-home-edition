/*
 * displays.h - Display Control for Captain Fantastic
 * 
 * HT16K33 LED 7-SEGMENT DISPLAY SYSTEM:
 * - Single HT16K33 backpack driving 6× LED 7-segment displays
 * - I2C Bus 0: GPIO 21 (SDA), GPIO 22 (SCL), 100kHz
 * - Address: 0x70
 * - Common cathode displays (LDS-C814RI style)
 * - Position mapping: 0,1,2,3,4,7 (positions 5 & 6 reserved/non-functional)
 * 
 * DISPLAY LAYOUT:
 * [Pos 0][Pos 1][Pos 2][Pos 3][Pos 4][Pos 7]
 * 100K   10K    1K     100    10     1     (score digits)
 */

#ifndef DISPLAYS_H
#define DISPLAYS_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_LEDBackpack.h>

// ============= CONFIGURATION =============
#define HT16K33_ADDRESS 0x70              // I2C address for HT16K33 backpack
#define NUM_DIGITS 6                      // 6 digits total

// Display positions (HT16K33 has 8 possible positions, but 5 & 6 don't work)
#define POS_HUNDRED_THOUSANDS 0           // 100K digit
#define POS_TEN_THOUSANDS 1               // 10K digit
#define POS_THOUSANDS 2                   // 1K digit
#define POS_HUNDREDS 3                    // 100 digit
#define POS_TENS 4                        // 10 digit
#define POS_ONES 5                        // 1 digit (using position 7, skip 5 & 6)

// Score limits
#define MAX_SCORE 999999                  // 6 digits max

// LED display object
extern Adafruit_7segment ledDisplay;

// Function prototypes
void initDisplay();                           // Initialize HT16K33 display
void updateLEDScore(uint32_t score);         // Update score on display
void clearDisplay();                          // Clear display
void setDisplayBrightness(uint8_t level);    // Set brightness (0-15)
void testDisplayPositions();                  // Test which positions work
void testSegmentMapping();                    // Test individual segment mapping
void displayStartupTest();                    // Flashy startup test routine

#endif // DISPLAYS_H
