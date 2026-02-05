// main_firmware.cpp - Clean production firmware for Captain Fantastic
// Migrated from test code, starting with matrix scan task

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Hardware pin definitions for production matrix
#define NUM_LAMP_ROWS 8
#define NUM_LAMP_COLS 5
const uint8_t rowPins[NUM_LAMP_ROWS] = {23, 19, 18, 5, 17, 16, 15, 13}; // ROW_M1 to ROW_M8

// Shift register pins (74HC595)
#define SR_DATA   2   // GPIO2 - Serial data
#define SR_CLOCK  12  // GPIO12 - Shift clock
#define SR_LATCH  4   // GPIO4 - Storage register clock

// Shift register bit mapping for columns
#define SR_BIT_L3   0  // Bit 0: Lamp column L3 (TILT lamp)
#define SR_BIT_L4   1  // Bit 1: Lamp column L4
#define SR_BIT_L5   2  // Bit 2: Lamp column L5
#define SR_BIT_L6   3  // Bit 3: Lamp column L6
#define SR_BIT_L7   4  // Bit 4: Lamp column L7

// Helper: column bit array for easy addressing
const uint8_t colBits[NUM_LAMP_COLS] = {SR_BIT_L3, SR_BIT_L4, SR_BIT_L5, SR_BIT_L6, SR_BIT_L7};

// Shared state arrays
volatile bool lampMatrix[NUM_LAMP_ROWS][NUM_LAMP_COLS] = {0};
volatile uint16_t shiftRegisterState = 0;

// Forward declarations
void clearColumns();

// Shift register output function: sets column bit, latches, pulses the row, then blanks columns
void updateShiftRegister(uint8_t row, uint8_t col, uint16_t onTimeUs) {
    // Program desired column
    shiftRegisterState &= ~((1 << SR_BIT_L3) | (1 << SR_BIT_L4) | (1 << SR_BIT_L5) | (1 << SR_BIT_L6) | (1 << SR_BIT_L7));
    shiftRegisterState |= (1 << colBits[col]);

    // Latch out shift register state
    digitalWrite(SR_LATCH, LOW);
    for (int i = 15; i >= 0; i--) {
        digitalWrite(SR_CLOCK, LOW);
        digitalWrite(SR_DATA, (shiftRegisterState >> i) & 0x01);
        digitalWrite(SR_CLOCK, HIGH);
    }
    digitalWrite(SR_LATCH, HIGH);

    // Drive the selected row
    digitalWrite(rowPins[row], HIGH);
    delayMicroseconds(onTimeUs);
    digitalWrite(rowPins[row], LOW);

    // Blank columns after pulse
    clearColumns();
}

// Clear all column outputs (no column selected)
void clearColumns() {
    shiftRegisterState &= ~((1 << SR_BIT_L3) | (1 << SR_BIT_L4) | (1 << SR_BIT_L5) | (1 << SR_BIT_L6) | (1 << SR_BIT_L7));
    digitalWrite(SR_LATCH, LOW);
    for (int i = 15; i >= 0; i--) {
        digitalWrite(SR_CLOCK, LOW);
        digitalWrite(SR_DATA, (shiftRegisterState >> i) & 0x01);
        digitalWrite(SR_CLOCK, HIGH);
    }
    digitalWrite(SR_LATCH, HIGH);
}

// Pulse a single lamp: sets column, enables row, holds, then clears
void pulseLamp(uint8_t row, uint8_t col, uint16_t onTimeUs) {
    if (row < NUM_LAMP_ROWS && col < NUM_LAMP_COLS) {
        updateShiftRegister(row, col, onTimeUs);
    }
}

// Matrix scan task - self-timed using micros() for precise period
void matrixTask(void* parameter) {
    const uint32_t scanPeriodUs = 2500; // 2.5ms per full scan (~400Hz)
    const uint16_t pulseWidthUs = 50;    // 50us lamp pulse

    while (true) {
        uint32_t startUs = micros();

        for (uint8_t row = 0; row < NUM_LAMP_ROWS; row++) {
            for (uint8_t col = 0; col < NUM_LAMP_COLS; col++) {
                if (lampMatrix[row][col]) {
                    // Pulse selected lamp for precise duration
                    pulseLamp(row, col, pulseWidthUs);
                }
            }
        }

        // Idle for remainder of the scan period
        uint32_t elapsedUs = micros() - startUs;
        if (elapsedUs < scanPeriodUs) {
            delayMicroseconds(scanPeriodUs - elapsedUs);
        }
        // else: overran the period; continue immediately
    }
}

// Static lamp display task - lights specific lamps
void staticLampTask(void* parameter) {
    // Clear all lamps first
    for (uint8_t r = 0; r < NUM_LAMP_ROWS; r++) {
        for (uint8_t c = 0; c < NUM_LAMP_COLS; c++) {
            lampMatrix[r][c] = false;
        }
    }
    
    // Light Player 3 lamp (adjust row/col to match your board layout)
    // Example: Row 0, Col 2 - change these to actual positions
    lampMatrix[0][2] = true;
    
    // Light Ball 2 lamp (adjust row/col to match your board layout)
    // Example: Row 1, Col 1 - change these to actual positions
    lampMatrix[1][1] = true;
    
    // Task just sleeps - matrixTask will handle the scanning
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Captain Fantastic - Static Lamp Display ===");
    
    // Initialize row pins
    for (uint8_t i = 0; i < NUM_LAMP_ROWS; i++) {
        pinMode(rowPins[i], OUTPUT);
        digitalWrite(rowPins[i], LOW);
    }
    // Initialize shift register pins
    pinMode(SR_DATA, OUTPUT);
    pinMode(SR_CLOCK, OUTPUT);
    pinMode(SR_LATCH, OUTPUT);
    
    Serial.println("Starting tasks...");
    // Start matrix scan task (handles the actual multiplexing)
    xTaskCreatePinnedToCore(matrixTask, "MatrixScan", 2048, NULL, 2, NULL, 1);
    
    // Start static lamp task (sets which lamps are on)
    xTaskCreatePinnedToCore(staticLampTask, "StaticLamps", 2048, NULL, 1, NULL, 1);
    
    Serial.println("Lighting Player 3 and Ball 2 lamps");
}

void loop() {
    // Empty - all work done in tasks
}
