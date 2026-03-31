#include <Arduino.h>
#include <Wire.h>
#include "captain_protocol.h"
#include "captain_mapping.h"
#include "i2c_bus_config.h"
#include "matrix_lamp_driver_config.h"
#include "matrix_test_support.h"

namespace {
constexpr uint16_t MATRIX_LAMP_PULSE_MIN_US = 50;
constexpr uint16_t MATRIX_LAMP_PULSE_STEP_US = 50;

uint8_t lampRowRam[CAPTAIN_LAMP_ROWS] = {};
uint8_t switchStateBytes[CAPTAIN_SWITCH_BYTES] = {};
uint8_t registerPointer = CAPTAIN_MATRIX_REG_SWITCH_BASE;
uint8_t lampPulseWidthLevel = CAPTAIN_MATRIX_DEFAULT_PULSE_WIDTH_LEVEL;
bool matrixSystemEnabled = false;
bool matrixOutputEnabled = false;

CaptainMatrixRuntimeView runtimeView() {
    CaptainMatrixRuntimeView view = {};
    view.lampRowRam = lampRowRam;
    view.lampRowCount = CAPTAIN_LAMP_ROWS;
    view.switchStateBytes = switchStateBytes;
    view.switchByteCount = CAPTAIN_SWITCH_BYTES;
    view.lampPulseWidthLevel = &lampPulseWidthLevel;
    view.matrixSystemEnabled = &matrixSystemEnabled;
    view.matrixOutputEnabled = &matrixOutputEnabled;
    return view;
}

uint16_t appliedLampPulseWidthUs() {
    return static_cast<uint16_t>(MATRIX_LAMP_PULSE_MIN_US +
                                 static_cast<uint16_t>(lampPulseWidthLevel) * MATRIX_LAMP_PULSE_STEP_US);
}

void fillDiagnosticBytes(uint8_t* diag) {
    diag[0] = 0;
    if (matrixSystemEnabled) {
        diag[0] |= CAPTAIN_MATRIX_DIAG_FLAG_SYSTEM_ENABLED;
    }
    if (matrixOutputEnabled) {
        diag[0] |= CAPTAIN_MATRIX_DIAG_FLAG_OUTPUT_ENABLED;
    }
    diag[0] |= matrixTestStatusFlags();
    diag[1] = lampPulseWidthLevel;
    diag[2] = lampRowRam[0];
    diag[3] = switchStateBytes[0];
}

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
    const uint8_t rowValue = lampRowRam[row];
    const bool col0 = (rowValue & captainMatrixLampRowMask(0)) != 0;
    const bool col1 = (rowValue & captainMatrixLampRowMask(1)) != 0;
    const bool col2 = (rowValue & captainMatrixLampRowMask(2)) != 0;
    const bool col3 = (rowValue & captainMatrixLampRowMask(3)) != 0;
    const bool col4 = (rowValue & captainMatrixLampRowMask(4)) != 0;

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

    if (!matrixSystemEnabled || !matrixOutputEnabled) {
        writeShiftRegister16(0);
        row = static_cast<uint8_t>((row + 1) % CAPTAIN_SWITCH_ROWS);
        return;
    }

    const uint16_t shiftValue = composeLampColumnShiftValue(row);
    writeShiftRegister16(shiftValue);

    digitalWrite(CAPTAIN_MATRIX_ROW_PINS[row], LOW);
    delayMicroseconds(appliedLampPulseWidthUs());
    digitalWrite(CAPTAIN_MATRIX_ROW_PINS[row], HIGH);

    row = static_cast<uint8_t>((row + 1) % CAPTAIN_SWITCH_ROWS);
}

void onI2CReceive(int length) {
    if (length <= 0 || !Wire.available()) {
        return;
    }

    const uint8_t command = static_cast<uint8_t>(Wire.read());
    registerPointer = command;
    const int payloadLength = length - 1;

    if ((command & 0xFEu) == CAPTAIN_MATRIX_CMD_SYSTEM_SETUP && payloadLength == 0) {
        matrixSystemEnabled = (command & CAPTAIN_MATRIX_CMD_SYSTEM_ENABLE) != 0;
        if (!matrixSystemEnabled) {
            writeShiftRegister16(0);
        }
        return;
    }

    if ((command & 0xFEu) == CAPTAIN_MATRIX_CMD_OUTPUT_SETUP && payloadLength == 0) {
        matrixOutputEnabled = (command & CAPTAIN_MATRIX_CMD_OUTPUT_ENABLE) != 0;
        if (!matrixOutputEnabled) {
            writeShiftRegister16(0);
        }
        return;
    }

    if ((command & 0xF0u) == CAPTAIN_MATRIX_CMD_PULSE_WIDTH_BASE && payloadLength == 0) {
        lampPulseWidthLevel = static_cast<uint8_t>(command & CAPTAIN_MATRIX_CMD_PULSE_WIDTH_MASK);
        return;
    }

    if (captainMatrixLampRegister(command) && payloadLength > 0) {
        uint8_t target = command;
        while (Wire.available() && captainMatrixLampRegister(target)) {
            lampRowRam[target - CAPTAIN_MATRIX_REG_LAMP_BASE] = static_cast<uint8_t>(Wire.read()) & 0x1Fu;
            target++;
        }
        while (Wire.available()) {
            Wire.read();
        }
        return;
    }

    while (Wire.available()) {
        Wire.read();
    }
}

void onI2CRequest() {
    if (captainMatrixSwitchRegister(registerPointer)) {
        const uint8_t offset = static_cast<uint8_t>(registerPointer - CAPTAIN_MATRIX_REG_SWITCH_BASE);
        const uint8_t count = static_cast<uint8_t>(CAPTAIN_SWITCH_BYTES - offset);
        Wire.write(switchStateBytes + offset, count);
        registerPointer = CAPTAIN_MATRIX_REG_SWITCH_END;
        return;
    }

    if (captainMatrixLampRegister(registerPointer)) {
        const uint8_t offset = static_cast<uint8_t>(registerPointer - CAPTAIN_MATRIX_REG_LAMP_BASE);
        const uint8_t count = static_cast<uint8_t>(CAPTAIN_LAMP_ROWS - offset);
        Wire.write(lampRowRam + offset, count);
        registerPointer = CAPTAIN_MATRIX_REG_LAMP_END;
        return;
    }

    if (captainMatrixDiagnosticRegister(registerPointer)) {
        uint8_t diagnosticBytes[CAPTAIN_MATRIX_REG_DIAG_END - CAPTAIN_MATRIX_REG_DIAG_BASE + 1] = {};
        fillDiagnosticBytes(diagnosticBytes);
        const uint8_t offset = static_cast<uint8_t>(registerPointer - CAPTAIN_MATRIX_REG_DIAG_BASE);
        const uint8_t count = static_cast<uint8_t>(sizeof(diagnosticBytes) - offset);
        Wire.write(diagnosticBytes + offset, count);
        registerPointer = CAPTAIN_MATRIX_REG_DIAG_END;
        return;
    }

    const uint8_t zero = 0;
    Wire.write(&zero, 1);
}

void scanSwitchMatrix() {
    memset(switchStateBytes, 0, sizeof(switchStateBytes));

    if (!matrixSystemEnabled) {
        return;
    }

    for (uint8_t row = 0; row < CAPTAIN_SWITCH_ROWS; row++) {
        for (uint8_t r = 0; r < CAPTAIN_SWITCH_ROWS; r++) {
            digitalWrite(CAPTAIN_MATRIX_ROW_PINS[r], HIGH);
        }
        digitalWrite(CAPTAIN_MATRIX_ROW_PINS[row], LOW);
        delayMicroseconds(10);

        for (uint8_t col = 0; col < CAPTAIN_SWITCH_COLS; col++) {
            const bool closed = digitalRead(CAPTAIN_MATRIX_SWITCH_COL_PINS[col]) == LOW;
            captainSetBit(switchStateBytes, captainSwitchBitIndex(row, col), closed);
        }
    }
}
}

void setup() {
    Serial.begin(115200);
    matrixTestSetup();

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

    Serial.printf("Captain v2 matrix board started: I2C=0x%02X lampPulse=%uus registers lamp[0x%02X-0x%02X] switch[0x%02X-0x%02X]\n",
                  CAPTAIN_MATRIX_I2C_ADDRESS,
                  static_cast<unsigned>(appliedLampPulseWidthUs()),
                  CAPTAIN_MATRIX_REG_LAMP_BASE,
                  CAPTAIN_MATRIX_REG_LAMP_END,
                  CAPTAIN_MATRIX_REG_SWITCH_BASE,
                  CAPTAIN_MATRIX_REG_SWITCH_END);
    Serial.printf("Matrix diagnostics registers: [0x%02X-0x%02X]\n",
                  CAPTAIN_MATRIX_REG_DIAG_BASE,
                  CAPTAIN_MATRIX_REG_DIAG_END);
}

void loop() {
    const uint32_t now = millis();
    matrixTestLoop(runtimeView(), now);
    scanSwitchMatrix();
    refreshLampMatrixStep();

    if ((now % 1000) < 10) {
        Serial.printf("Matrix alive system=%u output=%u pulse=%uus lamp0=0x%02X sw0=0x%02X\n",
                      matrixSystemEnabled ? 1u : 0u,
                      matrixOutputEnabled ? 1u : 0u,
                      static_cast<unsigned>(appliedLampPulseWidthUs()),
                      lampRowRam[0],
                      switchStateBytes[0]);
    }

    delay(5);
}
