// Minimal ESP32 pin toggle test for oscilloscope troubleshooting
// Change PIN_TO_TEST to any GPIO you want to verify

#include <Arduino.h>

#define PIN_TO_TEST 2   // Change this to test a different GPIO
#define DELAY_MS   20  // 20ms high, 20ms low (25Hz square wave)

void setup() {
    pinMode(PIN_TO_TEST, OUTPUT);
}

void loop() {
    digitalWrite(PIN_TO_TEST, HIGH);
    delay(DELAY_MS);
    digitalWrite(PIN_TO_TEST, LOW);
    delay(DELAY_MS);
}
