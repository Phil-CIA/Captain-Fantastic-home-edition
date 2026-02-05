#include <Arduino.h>
#include "../include/switches.h"
#include "../include/solenoid.h"

// 8x8 Matrix Configuration - Pin assignments from ESP32 Dev S2 schematic
const uint8_t rowPins[8] = {ROW_M1, ROW_M2, ROW_M3, ROW_M4, ROW_M5, ROW_M6, ROW_M7, ROW_M8};  // M1-M8 row address
const uint8_t switchCols[4] = {COL_SW1, COL_SW2, COL_SW3, COL_SW4};  // SW1-SW4 switch inputs
const uint8_t lampCols[5] = {COL_L3, COL_L4, COL_L5, COL_L6, COL_L7};        // L3-L7 lamp outputs

// Matrix state arrays
volatile uint8_t switchMatrix[8][4] = {0};   // Current switch states
volatile uint8_t lampMatrix[8][4] = {0};     // Current lamp states

void initMatrix() {
  Serial.println("Initializing 8x8 matrix...");
  
  // Setup row address lines as outputs (for selecting rows)
  for (int r = 0; r < 8; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], HIGH);  // Default high (inactive)
  }
  
  // Setup switch columns as inputs with pull-ups
  for (int c = 0; c < 4; c++) {
    pinMode(switchCols[c], INPUT_PULLUP);
  }
  
  // Setup lamp columns as outputs
  for (int c = 0; c < 4; c++) {
    pinMode(lampCols[c], OUTPUT);
    digitalWrite(lampCols[c], LOW);  // Default low (off)
  }
  
  Serial.printf("Row pins (M1-M8): GPIO %d,%d,%d,%d,%d,%d,%d,%d\n", 
    ROW_M1, ROW_M2, ROW_M3, ROW_M4, ROW_M5, ROW_M6, ROW_M7, ROW_M8);
  Serial.printf("Switch cols (SW1-SW4): GPIO %d,%d,%d,%d\n", 
    COL_SW1, COL_SW2, COL_SW3, COL_SW4);
  Serial.printf("Lamp cols (L3-L7): GPIO %d,%d,%d,%d,%d\n", 
    COL_L3, COL_L4, COL_L5, COL_L6, COL_L7);
}

void TaskSwitchScanner(void *pvParameters) {
  initMatrix();

  for (;;) {
    // Scan 8x8 matrix - one row at a time
    for (int r = 0; r < 8; r++) {
      // Activate current row (active low)
      digitalWrite(rowPins[r], LOW);
      delayMicroseconds(10);  // Allow line to settle

      // Read all 4 switch columns for this row
      for (int c = 0; c < 4; c++) {
        bool pressed = digitalRead(switchCols[c]) == LOW;
        
        // Check for state change
        if (pressed != switchMatrix[r][c]) {
          switchMatrix[r][c] = pressed;
          
          if (pressed) {
            Serial.printf("Switch M%d-SW%d pressed\n", r+1, c+1);
            
            // Trigger solenoids based on switch position
            switch (r * 4 + c) {
              case 0:  // M1-SW1: Left slingshot
                triggerSolenoid(SOL_SLINGSHOT_LEFT);
                break;
              case 1:  // M1-SW2: Right slingshot
                triggerSolenoid(SOL_SLINGSHOT_RIGHT);
                break;
              case 2:  // M1-SW3: Left thumper
                triggerSolenoid(SOL_THUMPER_LEFT);
                break;
              case 4:  // M2-SW1: Right thumper
                triggerSolenoid(SOL_THUMPER_RIGHT);
                break;
              case 5:  // M2-SW2: Outhole kicker
                triggerSolenoid(SOL_OUTHOLE);
                break;
              default:
                // Flash game status LED for other switches
                solenoidTriggers[LED_GAME_STATUS] = 1;
                // Light up corresponding lamp position
                setLamp(r, c % 4, true);
                vTaskDelay(200 / portTICK_PERIOD_MS);
                setLamp(r, c % 4, false);
                break;
            }
            
            // Brief delay to prevent multiple triggers
            vTaskDelay(50 / portTICK_PERIOD_MS);
          }
        }
      }

      // Deactivate current row
      digitalWrite(rowPins[r], HIGH);
    }

    vTaskDelay(20 / portTICK_PERIOD_MS);  // Scan cycle every 20ms
  }
}

void setLamp(uint8_t row, uint8_t col, bool state) {
  if (row < 8 && col < 4) {
    lampMatrix[row][col] = state ? 1 : 0;
  }
}

void TaskLampMatrix(void *pvParameters) {
  // Initialize lamp matrix task
  Serial.println("Lamp matrix task started");
  
  for (;;) {
    // Multiplex lamp matrix - one row at a time
    for (int r = 0; r < 8; r++) {
      // Activate current row
      digitalWrite(rowPins[r], LOW);
      
      // Set lamp column states for this row
      for (int c = 0; c < 4; c++) {
        digitalWrite(lampCols[c], lampMatrix[r][c] ? HIGH : LOW);
      }
      
      // Hold for persistence of vision
      vTaskDelay(2 / portTICK_PERIOD_MS);
      
      // Turn off all lamps before switching rows
      for (int c = 0; c < 4; c++) {
        digitalWrite(lampCols[c], LOW);
      }
      
      // Deactivate current row
      digitalWrite(rowPins[r], HIGH);
    }
    
    vTaskDelay(1 / portTICK_PERIOD_MS);  // Brief pause between cycles
  }
}