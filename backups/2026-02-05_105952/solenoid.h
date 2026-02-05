#ifndef SOLENOID_H
#define SOLENOID_H

#include <Arduino.h>

// MCP23017 I2C GPIO Expander Configuration
// From ESP32 Dev S2 schematic pinout
#define MCP23017_ADDRESS  0x20   // Default I2C address (A0=A1=A2=0)
#define SDA_PIN          21      // ESP32 GPIO21 (pin 26) - I2C SDA
#define SCL_PIN          22      // ESP32 GPIO22 (pin 29) - I2C SCL

// Captain Fantastic HOME EDITION - 5 Solenoids Only
#define SOL_OUTHOLE        0    // MCP23017 pin A0 -> Driver Board 1, Input 1 - Ball outhole kicker
#define SOL_THUMPER_LEFT   1    // MCP23017 pin A1 -> Driver Board 1, Input 2 - Left thumper bumper
#define SOL_THUMPER_RIGHT  2    // MCP23017 pin A2 -> Driver Board 1, Input 3 - Right thumper bumper  
#define SOL_SLINGSHOT_LEFT 3    // MCP23017 pin A3 -> Driver Board 1, Input 4 - Left slingshot
#define SOL_SLINGSHOT_RIGHT 4   // MCP23017 pin A4 -> Driver Board 1, Input 5 - Right slingshot

// Status LEDs (using remaining MCP23017 outputs)
#define LED_SYSTEM_STATUS  5    // MCP23017 pin A5 -> Driver Board 1, Input 6 (Status LED)
#define LED_GAME_STATUS    6    // MCP23017 pin A6 -> Driver Board 1, Input 7 (Game LED)

// Port B available for future expansion
#define SPARE_OUTPUT_1     8    // MCP23017 pin B0 -> Driver Board 2, Input 1
#define SPARE_OUTPUT_2     9    // MCP23017 pin B1 -> Driver Board 2, Input 2

// Pulse lengths in milliseconds (for 5 solenoids + 2 LEDs)
extern uint16_t solenoidPulseLengths[7];
extern volatile uint8_t solenoidTriggers[7];  // Event flags for 5 solenoids + 2 LEDs
extern volatile bool systemStatus;

// Function declarations
void TaskSolenoid(void *pvParameters);
void initSolenoidSystem();
void triggerSolenoid(uint8_t solenoidId);
void setMCPOutput(uint8_t pin, bool state);
void initMCP23017();
void setSystemStatus(bool status);

#endif

