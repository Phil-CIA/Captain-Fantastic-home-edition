// metering_supply.cpp - Initial test for ESP32 onboard LED blink
// Confirms board control and basic PlatformIO setup

#include <Arduino.h>

#define ONBOARD_LED 2 // GPIO2 is onboard LED for ESP32 DevKit

void setup() {
    pinMode(ONBOARD_LED, OUTPUT);
}

void loop() {
    digitalWrite(ONBOARD_LED, HIGH);
    delay(500);
    digitalWrite(ONBOARD_LED, LOW);
    delay(500);
}
