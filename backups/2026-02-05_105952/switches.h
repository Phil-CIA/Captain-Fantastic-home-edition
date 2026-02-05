#ifndef SWITCHES_H
#define SWITCHES_H

// ESP32 Pin Assignments for 8x8 Matrix (from schematic)
// 8x8 Matrix: Switches (SW1-SW4) + Lamps (L4-L7) vs Rows (M1-M8)

// ROW ADDRESS LINES (M1-M8) - 8 rows for full matrix
// From ESP32 Dev S2 schematic pinout
#define ROW_M1       23    // ESP32 GPIO23 (pin 30) - M1
#define ROW_M2       19    // ESP32 GPIO19 (pin 25) - M2  
#define ROW_M3       18    // ESP32 GPIO18 (pin 24) - M3
#define ROW_M4        5    // ESP32 GPIO5  (pin 23) - M4
#define ROW_M5       17    // ESP32 GPIO17 (pin 22) - M5
#define ROW_M6       16    // ESP32 GPIO16 (pin 21) - M6
#define ROW_M7       15    // ESP32 GPIO15 (pin 18) - M7
#define ROW_M8       13    // ESP32 GPIO13 (pin 3) - M8

// COLUMN LINES - Switch inputs (SW0-SW3) + Lamp outputs (L3-L6)
// From KiCad schematic: J4 (left side connector) controls switch and lamp columns
// Switch columns (inputs with pull-ups) - From schematic
#define COL_SW1      35    // ESP32 GPIO35 (pin 11) - SW1
#define COL_SW2      34    // ESP32 GPIO34 (pin 12) - SW2
#define COL_SW3      39    // ESP32 GPIO39 (pin 13) - SW3
#define COL_SW4      36    // ESP32 GPIO36 (pin 14) - SW4

// Lamp columns (outputs for LED matrix) - From schematic
#define COL_L3       14    // ESP32 GPIO14 (pin 5) - L3
#define COL_L4       27    // ESP32 GPIO27 (pin 6) - L4
#define COL_L5       26    // ESP32 GPIO26 (pin 7) - L5
#define COL_L6       33    // ESP32 GPIO33 (pin 9) - L6
#define COL_L7       32    // ESP32 GPIO32 (pin 10) - L7

// Additional assignments from schematic
#define SOUND_PIN    25    // ESP32 GPIO25 (pin 8) - Sound
#define A0_PIN        2    // ESP32 GPIO2  (pin 19) - a0
#define A1_PIN        4    // ESP32 GPIO4  (pin 20) - a1
#define A2_PIN       12    // ESP32 GPIO12 (pin 4) - a2

// Matrix configuration arrays (matching Captain Fantastic schematic)
extern const uint8_t rowPins[8];        // M1-M8 row drivers 
extern const uint8_t switchCols[4];     // SW1-SW4 switch input columns  
extern const uint8_t lampCols[5];       // L3-L7 lamp output columns

// Matrix state tracking
extern volatile uint8_t switchMatrix[8][4];   // 8 rows x 4 switch columns
extern volatile uint8_t lampMatrix[8][4];     // 8 rows x 4 lamp columns

// Function declarations
void TaskSwitchScanner(void *pvParameters);
void TaskLampMatrix(void *pvParameters);
void setLamp(uint8_t row, uint8_t col, bool state);
void initMatrix();

#endif

