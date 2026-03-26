#include <Arduino.h>
#include <Wire.h>
#include "../include/solenoid.h"

// MCP23017 Register definitions
#define MCP23017_IODIRA   0x00   // I/O Direction Register A
#define MCP23017_IODIRB   0x01   // I/O Direction Register B  
#define MCP23017_GPIOA    0x12   // GPIO Register A
#define MCP23017_GPIOB    0x13   // GPIO Register B

// Current state tracking
uint8_t mcpPortAState = 0x00;
uint8_t mcpPortBState = 0x00;

// Pulse lengths for Captain Fantastic Home Edition (milliseconds)
uint16_t solenoidPulseLengths[7] = {
  120,  // SOL_OUTHOLE - Ball kicker (longer pulse)
  60,   // SOL_THUMPER_LEFT - Left bumper (quick pop)
  60,   // SOL_THUMPER_RIGHT - Right bumper (quick pop)
  40,   // SOL_SLINGSHOT_LEFT - Left slingshot (fast response)
  40,   // SOL_SLINGSHOT_RIGHT - Right slingshot (fast response)
  0,    // LED_SYSTEM_STATUS - Continuous on/off
  0     // LED_GAME_STATUS - Continuous on/off
};

// Event trigger flags - set by other tasks
volatile uint8_t solenoidTriggers[7] = {0};
volatile bool systemStatus = false;

void writeMCPRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MCP23017_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

uint8_t readMCPRegister(uint8_t reg) {
  Wire.beginTransmission(MCP23017_ADDRESS);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom(MCP23017_ADDRESS, 1);
  return Wire.read();
}

bool testI2CConnection() {
  Serial.println("Testing I2C connection to MCP23017...");
  
  // Enable internal pull-ups on I2C pins
  pinMode(SDA_PIN, INPUT_PULLUP);
  pinMode(SCL_PIN, INPUT_PULLUP);
  Serial.println("Enabled internal pull-ups on SDA/SCL pins");
  
  Wire.beginTransmission(MCP23017_ADDRESS);
  uint8_t error = Wire.endTransmission();
  
  if (error == 0) {
    Serial.println("✓ MCP23017 found on I2C bus");
    return true;
  } else {
    Serial.printf("✗ MCP23017 NOT found! Error code: %d\\n", error);
    Serial.println("Check: SDA=GPIO21, SCL=GPIO22, +5V, GND, need 4.7k pull-ups");
    return false;
  }
}

void initMCP23017() {
  // Initialize I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000); // Slower 100kHz for testing
  
  Serial.printf("Initializing I2C on SDA=%d, SCL=%d\n", SDA_PIN, SCL_PIN);
  
  // Test connection first
  if (!testI2CConnection()) {
    Serial.println("I2C connection failed - check wiring!");
    return;
  }
  
  // Configure all pins as outputs (0 = output, 1 = input)
  writeMCPRegister(MCP23017_IODIRA, 0x00);  // Port A all outputs
  writeMCPRegister(MCP23017_IODIRB, 0x00);  // Port B all outputs
  
  // Initialize all outputs LOW
  writeMCPRegister(MCP23017_GPIOA, 0x00);
  writeMCPRegister(MCP23017_GPIOB, 0x00);
  
  mcpPortAState = 0x00;
  mcpPortBState = 0x00;
  
  Serial.println("✓ MCP23017 configured successfully");
  Serial.printf("I2C Address: 0x%02X\n", MCP23017_ADDRESS);
}

void initSolenoidSystem() {
  initMCP23017();
  
  Serial.println("Captain Fantastic Home Edition - MCP23017 initialized");
  Serial.println("Pin mapping:");
  Serial.printf("  Outhole Kicker: MCP pin A%d\n", SOL_OUTHOLE);
  Serial.printf("  Left Thumper: MCP pin A%d\n", SOL_THUMPER_LEFT);
  Serial.printf("  Right Thumper: MCP pin A%d\n", SOL_THUMPER_RIGHT);
  Serial.printf("  Left Slingshot: MCP pin A%d\n", SOL_SLINGSHOT_LEFT);
  Serial.printf("  Right Slingshot: MCP pin A%d\n", SOL_SLINGSHOT_RIGHT);
  Serial.printf("  System Status LED: MCP pin A%d\n", LED_SYSTEM_STATUS);
  Serial.printf("  Game Status LED: MCP pin A%d\n", LED_GAME_STATUS);
}

void setMCPOutput(uint8_t pin, bool state) {
  if (pin < 8) {
    // Port A (pins 0-7)
    if (state) {
      mcpPortAState |= (1 << pin);   // Set bit
    } else {
      mcpPortAState &= ~(1 << pin);  // Clear bit
    }
    writeMCPRegister(MCP23017_GPIOA, mcpPortAState);
  } else if (pin < 16) {
    // Port B (pins 8-15)
    uint8_t portBPin = pin - 8;
    if (state) {
      mcpPortBState |= (1 << portBPin);   // Set bit
    } else {
      mcpPortBState &= ~(1 << portBPin);  // Clear bit
    }
    writeMCPRegister(MCP23017_GPIOB, mcpPortBState);
  }
}

void triggerSolenoid(uint8_t solenoidId) {
  if (solenoidId < 7) {
    solenoidTriggers[solenoidId] = 1;
  }
}

void setSystemStatus(bool status) {
  systemStatus = status;
  // Status LED will be handled in main task loop
}

void TaskSolenoid(void *pvParameters) {
  initSolenoidSystem();
  
  // LED blink timing for status
  unsigned long lastStatusBlink = 0;
  bool statusLedState = false;
  
  for (;;) {
    // Handle solenoid triggers (0-4)
    for (int i = 0; i < 5; i++) {
      if (solenoidTriggers[i]) {
        solenoidTriggers[i] = 0;  // Clear flag
        
        // Activate solenoid
        setMCPOutput(i, true);
        Serial.printf("Solenoid %d fired (MCP pin A%d) - %dms pulse\n", i, i, solenoidPulseLengths[i]);
        
        // Hold pulse for specified time
        vTaskDelay(solenoidPulseLengths[i] / portTICK_PERIOD_MS);
        
        // Turn off solenoid
        setMCPOutput(i, false);
      }
    }
    
    // Force status LEDs ON (common cathode - LOW = ON)
    setMCPOutput(LED_SYSTEM_STATUS, false);  // LOW = ON for common cathode
    setMCPOutput(LED_GAME_STATUS, false);    // LOW = ON for common cathode
    
    // Debug: Show actual pin states
    Serial.printf("Status LEDs: A5=%s, A6=%s (LOW=ON for common cathode)\\n",
                  (mcpPortAState & (1 << LED_SYSTEM_STATUS)) ? "HIGH" : "LOW",
                  (mcpPortAState & (1 << LED_GAME_STATUS)) ? "HIGH" : "LOW");
    
    vTaskDelay(5 / portTICK_PERIOD_MS);  // Fast response time
  }
}

