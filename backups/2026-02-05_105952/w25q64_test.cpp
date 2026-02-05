#include <Arduino.h>
#include "w25q64.h"

W25Q64 flash;

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n[W25Q64 Test] Booting...");
    flash.begin();
    uint8_t status = flash.readStatus();
    Serial.print("Status Register: 0x");
    Serial.println(status, HEX);

    // Write and read test
    uint32_t testAddr = 0x000100;
    uint8_t testValue = 0x5A;
    flash.writeByte(testAddr, testValue);
    uint8_t readValue = flash.readByte(testAddr);
    Serial.print("Wrote 0x");
    Serial.print(testValue, HEX);
    Serial.print(" Read back 0x");
    Serial.println(readValue, HEX);

    if (readValue == testValue) {
        Serial.println("[PASS] W25Q64 R/W test successful!");
    } else {
        Serial.println("[FAIL] W25Q64 R/W test failed!");
    }
}

void loop() {
    delay(1000);
}