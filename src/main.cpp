#include <Arduino.h>
#include <Wire.h>

#define MCP23017_ADDRESS 0x20

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("*** MINIMAL SOLENOID TEST ***");
  
  Wire.begin(21, 22);
  Wire.setClock(100000);
  
  // Configure MCP23017 as outputs
  Wire.beginTransmission(MCP23017_ADDRESS);
  Wire.write(0x00);  // IODIRA
  Wire.write(0x00);  // All outputs
  Wire.endTransmission();
  
  Serial.println("MCP23017 configured. Testing pin A0 repeatedly...");
}

void loop() {
  static int cycle = 0;
  cycle++;
  
  Serial.printf("\n=== Cycle #%d - Testing all 5 solenoids ===\n", cycle);
  
  // Test each solenoid pin A0-A4
  for (int pin = 0; pin < 5; pin++) {
    Serial.printf("Solenoid %d (pin A%d): ON\n", pin, pin);
    
    Wire.beginTransmission(MCP23017_ADDRESS);
    Wire.write(0x12);  // GPIOA
    Wire.write(1 << pin);  // Turn on this pin only
    Wire.endTransmission();
    
    delay(500);  // Hold for 0.5 seconds
    
    Serial.printf("Solenoid %d: OFF\n", pin);
    
    Wire.beginTransmission(MCP23017_ADDRESS);
    Wire.write(0x12);  // GPIOA
    Wire.write(0x00);  // All OFF
    Wire.endTransmission();
    
    delay(300);  // Brief pause between solenoids
  }
  
  Serial.println("All solenoids tested. Waiting 2 seconds...\n");
  delay(2000);
}