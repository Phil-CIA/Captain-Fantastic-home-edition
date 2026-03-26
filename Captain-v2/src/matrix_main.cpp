#include <Arduino.h>
#include <Wire.h>
#include "captain_protocol.h"
#include "captain_mapping.h"
#include "i2c_bus_config.h"
#include "matrix_lamp_driver_config.h"

namespace {
MatrixToControlFrame outbound = {};
ControlToMatrixCommand inbound = {};
volatile bool commandAvailable = false;
uint32_t appliedLampCommandSequence = 0;
uint8_t commandedLampBits[CAPTAIN_LAMP_BYTES] = {};

void writeShiftRegister16(uint16_t value) {
    digitalWrite(CAPTAIN_MATRIX_SR_LATCH_PIN, LOW);
    for (int8_t bit = 15; bit >= 0; bit--) {
        digitalWrite(CAPTAIN_MATRIX_SR_CLOCK_PIN, LOW);
        const bool state = (value & (1u << bit)) != 0;
        digitalWrite(CAPTAIN_MATRIX_SR_DATA_PIN, state ? HIGH : LOW);
        digitalWrite(CAPTAIN_MATRIX_SR_CLOCK_PIN, HIGH);
    }
    digitalWrite(CAPTAIN_MATRIX_SR_LATCH_PIN, HIGH);
}

uint16_t composeLampColumnShiftValue(uint8_t row) {
    uint16_t value = 0;
    const bool col0 = captainGetBit(commandedLampBits, captainLampBitIndex(row, 0));
    const bool col1 = captainGetBit(commandedLampBits, captainLampBitIndex(row, 1));
    const bool col2 = captainGetBit(commandedLampBits, captainLampBitIndex(row, 2));
    const bool col3 = captainGetBit(commandedLampBits, captainLampBitIndex(row, 3));
    const bool col4 = captainGetBit(commandedLampBits, captainLampBitIndex(row, 4));

    if (col0) value |= static_cast<uint16_t>(1u << CAPTAIN_MATRIX_SR_BIT_LAMP_COL0);
    if (col1) value |= static_cast<uint16_t>(1u << CAPTAIN_MATRIX_SR_BIT_LAMP_COL1);
    if (col2) value |= static_cast<uint16_t>(1u << CAPTAIN_MATRIX_SR_BIT_LAMP_COL2);
    if (col3) value |= static_cast<uint16_t>(1u << CAPTAIN_MATRIX_SR_BIT_LAMP_COL3);
    if (col4) value |= static_cast<uint16_t>(1u << CAPTAIN_MATRIX_SR_BIT_LAMP_COL4);

    return value;
}

void refreshLampMatrixStep() {
    static uint8_t row = 0;

    for (uint8_t r = 0; r < CAPTAIN_SWITCH_ROWS; r++) {
        digitalWrite(CAPTAIN_MATRIX_ROW_PINS[r], HIGH);
    }

    const uint16_t shiftValue = composeLampColumnShiftValue(row);
    writeShiftRegister16(shiftValue);

    digitalWrite(CAPTAIN_MATRIX_ROW_PINS[row], LOW);
    delayMicroseconds(250);
    digitalWrite(CAPTAIN_MATRIX_ROW_PINS[row], HIGH);

    row = static_cast<uint8_t>((row + 1) % CAPTAIN_SWITCH_ROWS);
}

void onI2CReceive(int length) {
    if (length != sizeof(ControlToMatrixCommand)) {
        while (Wire.available()) {
            Wire.read();
        }
        return;
    }

    uint8_t raw[sizeof(ControlToMatrixCommand)] = {};
    for (size_t i = 0; i < sizeof(raw); i++) {
        if (Wire.available()) {
            raw[i] = static_cast<uint8_t>(Wire.read());
        }
    }

    memcpy(&inbound, raw, sizeof(inbound));
    const uint8_t expected = captainChecksum(raw, sizeof(ControlToMatrixCommand) - 1);
    commandAvailable = (expected == inbound.checksum);
}

void onI2CRequest() {
    uint8_t* raw = reinterpret_cast<uint8_t*>(&outbound);
    outbound.checksum = captainChecksum(raw, sizeof(MatrixToControlFrame) - 1);
    Wire.write(raw, sizeof(MatrixToControlFrame));
}

void scanSwitchMatrix() {
    memset(outbound.switchBits, 0, sizeof(outbound.switchBits));

    for (uint8_t row = 0; row < CAPTAIN_SWITCH_ROWS; row++) {
        for (uint8_t r = 0; r < CAPTAIN_SWITCH_ROWS; r++) {
            digitalWrite(CAPTAIN_MATRIX_ROW_PINS[r], HIGH);
        }
        digitalWrite(CAPTAIN_MATRIX_ROW_PINS[row], LOW);
        delayMicroseconds(10);

        for (uint8_t col = 0; col < CAPTAIN_SWITCH_COLS; col++) {
            const bool closed = digitalRead(CAPTAIN_MATRIX_SWITCH_COL_PINS[col]) == LOW;
            captainSetBit(outbound.switchBits, captainSwitchBitIndex(row, col), closed);
        }
    }
}

void applyLampCommand() {
    if (!commandAvailable) {
        return;
    }

    memcpy(commandedLampBits, inbound.lampBits, sizeof(commandedLampBits));
    commandAvailable = false;
    appliedLampCommandSequence = inbound.sequence;
}
}

void setup() {
    Serial.begin(115200);

    for (uint8_t row = 0; row < CAPTAIN_SWITCH_ROWS; row++) {
        pinMode(CAPTAIN_MATRIX_ROW_PINS[row], OUTPUT);
        digitalWrite(CAPTAIN_MATRIX_ROW_PINS[row], HIGH);
    }

    for (uint8_t col = 0; col < CAPTAIN_SWITCH_COLS; col++) {
        pinMode(CAPTAIN_MATRIX_SWITCH_COL_PINS[col], INPUT_PULLUP);
    }

    pinMode(CAPTAIN_MATRIX_SR_DATA_PIN, OUTPUT);
    pinMode(CAPTAIN_MATRIX_SR_CLOCK_PIN, OUTPUT);
    pinMode(CAPTAIN_MATRIX_SR_LATCH_PIN, OUTPUT);
    writeShiftRegister16(0);

    Wire.begin(static_cast<int>(CAPTAIN_MATRIX_I2C_ADDRESS), CAPTAIN_I2C_SDA_PIN, CAPTAIN_I2C_SCL_PIN, CAPTAIN_I2C_FREQUENCY_HZ);
    Wire.onReceive(onI2CReceive);
    Wire.onRequest(onI2CRequest);

    Serial.println("Captain v2 matrix board started");
}

void loop() {
    outbound.sequence++;
    outbound.uptimeMs = millis();
    scanSwitchMatrix();
    applyLampCommand();
    refreshLampMatrixStep();

    if ((millis() % 1000) < 10) {
        Serial.printf("Matrix alive seq=%lu cmd=%lu\n", static_cast<unsigned long>(outbound.sequence), static_cast<unsigned long>(appliedLampCommandSequence));
    }

    delay(5);
}
