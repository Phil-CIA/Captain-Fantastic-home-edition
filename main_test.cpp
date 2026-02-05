#include <Arduino.h>
#include <Wire.h>

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("*** ESP32 + I2C TEST ***");
  Serial.println("ESP32 is working! Now testing I2C...");
  
  // Initialize I2C
  Wire.begin(21, 22);  // SDA=21, SCL=22
  Wire.setClock(100000); // 100kHz
  
  Serial.println("I2C initialized on GPIO21(SDA) and GPIO22(SCL)");
}

void loop() {
  static int count = 0;
  static unsigned long lastTest = 0;
  
  count++;
  Serial.printf("Loop #%d - ESP32 running\\n", count);
  
  // Test I2C every 10 loops
  if (count % 10 == 0 || millis() - lastTest > 5000) {
    Serial.println("\\n=== I2C Diagnostic ===");
    
    // Check pin states
    Serial.printf("GPIO21 (SDA) level: %s\\n", digitalRead(21) ? "HIGH" : "LOW");
    Serial.printf("GPIO22 (SCL) level: %s\\n", digitalRead(22) ? "HIGH" : "LOW");
    
    // Scan I2C bus
    Serial.println("Scanning addresses 0x20-0x27 (MCP23017 range)...");
    int devices = 0;
    
    for (uint8_t addr = 0x20; addr <= 0x27; addr++) {
      Wire.beginTransmission(addr);
      uint8_t error = Wire.endTransmission();
      Serial.printf("Address 0x%02X: %s (error %d)\\n", addr, 
                    error == 0 ? "FOUND" : "no response", error);
      if (error == 0) devices++;
    }
    
    if (devices == 0) {
      Serial.println("\\n*** NO MCP23017 FOUND ***");
      Serial.println("Check:");
      Serial.println("- Power: VCC to +5V, GND connected");
      Serial.println("- Wiring: SDA to GPIO21, SCL to GPIO22"); 
      Serial.println("- Pull-ups: 4.7k SDA->+5V, SCL->+5V");
      Serial.println("- Address pins: A0,A1,A2 to GND for 0x20");
    } else {
      Serial.printf("\\n*** MCP23017 RESPONDS - Testing PIN CONTROL ***\\n", devices);
      
      // Configure ALL pins as outputs
      Wire.beginTransmission(0x20);
      Wire.write(0x00);  // IODIRA register
      Wire.write(0x00);  // All outputs
      Wire.endTransmission();
      
      Serial.println("Test 1: ALL pins LOW (0x00)");
      Wire.beginTransmission(0x20);
      Wire.write(0x12);  // GPIOA register
      Wire.write(0x00);  // All LOW
      Wire.endTransmission();
      delay(2000);
      
      Serial.println("Test 2: ALL pins HIGH (0xFF)");  
      Wire.beginTransmission(0x20);
      Wire.write(0x12);  // GPIOA register
      Wire.write(0xFF);  // All HIGH
      Wire.endTransmission();
      delay(2000);
      
      Serial.println("Test 3: Only A0 HIGH (0x01)");
      Wire.beginTransmission(0x20);
      Wire.write(0x12);  // GPIOA register
      Wire.write(0x01);  // Only A0 HIGH
      Wire.endTransmission();
      delay(2000);
      
      Serial.println("*** NO LEDs? CHECK: ***");
      Serial.println("1. LED power supply connected?");
      Serial.println("2. LEDs wired to MCP23017 pins A0-A7?");
      Serial.println("3. LED polarity correct?");
      Serial.println("4. Current limiting resistors present?");
      Serial.println("5. Common ground between ESP32, MCP23017, LEDs?");
      
      // Back to all LOW
      Wire.beginTransmission(0x20);
      Wire.write(0x12);  // GPIOA register
      Wire.write(0x00);  // All LOW
      Wire.endTransmission();
    }
    
    lastTest = millis();
    Serial.println("=== End Test ===\\n");
  }
  
  // Blink LED
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);
  delay(500);
  digitalWrite(2, LOW);
  delay(500);
}