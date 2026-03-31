#include <Arduino.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "captain_mapping.h"
#include "captain_protocol.h"
#include "matrix_test_support.h"

namespace {
constexpr uint32_t MATRIX_TEST_AUTOWALK_INTERVAL_MS = 180;
constexpr size_t MATRIX_TEST_BUFFER_SIZE = 80;

char commandBuffer[MATRIX_TEST_BUFFER_SIZE] = {};
size_t commandLength = 0;
bool autoWalkEnabled = false;
bool manualOverrideActive = false;
uint8_t autoWalkRow = 0;
uint8_t autoWalkCol = 0;
uint32_t lastAutoWalkMs = 0;

void matrixTestClearLampRows(const CaptainMatrixRuntimeView& runtime) {
    memset(runtime.lampRowRam, 0, runtime.lampRowCount);
}

void matrixTestPrintHelp() {
    Serial.println("Matrix test commands:");
    Serial.println("  HELP");
    Serial.println("  STATUS");
    Serial.println("  SYSTEM ON|OFF");
    Serial.println("  OUTPUT ON|OFF");
    Serial.println("  PULSE <0-15>");
    Serial.println("  ALL ON|OFF");
    Serial.println("  CLEAR");
    Serial.println("  ROW <0-7> <0-31>");
    Serial.println("  LAMP <row> <col> ON|OFF");
    Serial.println("  WALK ON|OFF");
    Serial.println("  SWITCHES");
}

void matrixTestPrintStatus(const CaptainMatrixRuntimeView& runtime) {
    Serial.printf("Matrix test status: system=%u output=%u pulseLevel=%u override=%u autowalk=%u\n",
                  *runtime.matrixSystemEnabled ? 1u : 0u,
                  *runtime.matrixOutputEnabled ? 1u : 0u,
                  static_cast<unsigned>(*runtime.lampPulseWidthLevel),
                  manualOverrideActive ? 1u : 0u,
                  autoWalkEnabled ? 1u : 0u);

    Serial.print("Lamp rows:");
    for (size_t index = 0; index < runtime.lampRowCount; index++) {
        Serial.printf(" [%u]=0x%02X", static_cast<unsigned>(index), runtime.lampRowRam[index]);
    }
    Serial.println();
}

void matrixTestDumpSwitches(const CaptainMatrixRuntimeView& runtime) {
    Serial.print("Switch bytes:");
    for (size_t index = 0; index < runtime.switchByteCount; index++) {
        Serial.printf(" [%u]=0x%02X", static_cast<unsigned>(index), runtime.switchStateBytes[index]);
    }
    Serial.println();

    for (uint8_t row = 0; row < CAPTAIN_SWITCH_ROWS; row++) {
        for (uint8_t col = 0; col < CAPTAIN_SWITCH_COLS; col++) {
            if (captainGetBit(runtime.switchStateBytes, captainSwitchBitIndex(row, col))) {
                Serial.printf("  CLOSED M%u/SW%u %s\n", row + 1, col + 1, captainSwitchName(row, col));
            }
        }
    }
}

bool parseUnsignedToken(const char* token, uint8_t* out) {
    if (token == nullptr || *token == '\0') {
        return false;
    }

    char* end = nullptr;
    const long value = strtol(token, &end, 0);
    if (end == token || *end != '\0' || value < 0 || value > 255) {
        return false;
    }

    *out = static_cast<uint8_t>(value);
    return true;
}

void matrixTestApplyAutoWalk(const CaptainMatrixRuntimeView& runtime) {
    matrixTestClearLampRows(runtime);
    runtime.lampRowRam[autoWalkRow] = captainMatrixLampRowMask(autoWalkCol);
    manualOverrideActive = true;

    autoWalkCol++;
    if (autoWalkCol >= CAPTAIN_LAMP_COLS) {
        autoWalkCol = 0;
        autoWalkRow = static_cast<uint8_t>((autoWalkRow + 1) % runtime.lampRowCount);
    }
}

void matrixTestProcessCommand(const CaptainMatrixRuntimeView& runtime, char* line) {
    char* context = nullptr;
    char* command = strtok_r(line, " \t", &context);
    if (command == nullptr) {
        return;
    }

    if (strcasecmp(command, "HELP") == 0) {
        matrixTestPrintHelp();
        return;
    }

    if (strcasecmp(command, "STATUS") == 0) {
        matrixTestPrintStatus(runtime);
        return;
    }

    if (strcasecmp(command, "SWITCHES") == 0) {
        matrixTestDumpSwitches(runtime);
        return;
    }

    if (strcasecmp(command, "SYSTEM") == 0) {
        char* state = strtok_r(nullptr, " \t", &context);
        if (state != nullptr && strcasecmp(state, "ON") == 0) {
            *runtime.matrixSystemEnabled = true;
            Serial.println("Matrix system enabled");
            return;
        }
        if (state != nullptr && strcasecmp(state, "OFF") == 0) {
            *runtime.matrixSystemEnabled = false;
            Serial.println("Matrix system disabled");
            return;
        }
    }

    if (strcasecmp(command, "OUTPUT") == 0) {
        char* state = strtok_r(nullptr, " \t", &context);
        if (state != nullptr && strcasecmp(state, "ON") == 0) {
            *runtime.matrixOutputEnabled = true;
            Serial.println("Matrix output enabled");
            return;
        }
        if (state != nullptr && strcasecmp(state, "OFF") == 0) {
            *runtime.matrixOutputEnabled = false;
            Serial.println("Matrix output disabled");
            return;
        }
    }

    if (strcasecmp(command, "PULSE") == 0) {
        uint8_t level = 0;
        if (parseUnsignedToken(strtok_r(nullptr, " \t", &context), &level) && level <= 15) {
            *runtime.lampPulseWidthLevel = level;
            Serial.printf("Lamp pulse width level set to %u\n", static_cast<unsigned>(level));
            return;
        }
    }

    if (strcasecmp(command, "ALL") == 0) {
        char* state = strtok_r(nullptr, " \t", &context);
        if (state != nullptr && strcasecmp(state, "ON") == 0) {
            for (size_t index = 0; index < runtime.lampRowCount; index++) {
                runtime.lampRowRam[index] = 0x1Fu;
            }
            manualOverrideActive = true;
            autoWalkEnabled = false;
            Serial.println("All lamps forced on");
            return;
        }
        if (state != nullptr && strcasecmp(state, "OFF") == 0) {
            matrixTestClearLampRows(runtime);
            manualOverrideActive = true;
            autoWalkEnabled = false;
            Serial.println("All lamps forced off");
            return;
        }
    }

    if (strcasecmp(command, "CLEAR") == 0) {
        matrixTestClearLampRows(runtime);
        manualOverrideActive = false;
        autoWalkEnabled = false;
        Serial.println("Matrix manual lamp override cleared");
        return;
    }

    if (strcasecmp(command, "ROW") == 0) {
        uint8_t row = 0;
        uint8_t mask = 0;
        if (parseUnsignedToken(strtok_r(nullptr, " \t", &context), &row) &&
            parseUnsignedToken(strtok_r(nullptr, " \t", &context), &mask) &&
            row < runtime.lampRowCount) {
            runtime.lampRowRam[row] = static_cast<uint8_t>(mask & 0x1Fu);
            manualOverrideActive = true;
            autoWalkEnabled = false;
            Serial.printf("Lamp row %u set to 0x%02X\n", static_cast<unsigned>(row), runtime.lampRowRam[row]);
            return;
        }
    }

    if (strcasecmp(command, "LAMP") == 0) {
        uint8_t row = 0;
        uint8_t col = 0;
        char* state = nullptr;
        if (parseUnsignedToken(strtok_r(nullptr, " \t", &context), &row) &&
            parseUnsignedToken(strtok_r(nullptr, " \t", &context), &col) &&
            (state = strtok_r(nullptr, " \t", &context)) != nullptr &&
            row < runtime.lampRowCount && col < CAPTAIN_LAMP_COLS) {
            const uint8_t mask = captainMatrixLampRowMask(col);
            if (strcasecmp(state, "ON") == 0) {
                runtime.lampRowRam[row] |= mask;
                manualOverrideActive = true;
                autoWalkEnabled = false;
                Serial.printf("Lamp row=%u col=%u forced on\n", static_cast<unsigned>(row), static_cast<unsigned>(col));
                return;
            }
            if (strcasecmp(state, "OFF") == 0) {
                runtime.lampRowRam[row] &= static_cast<uint8_t>(~mask);
                manualOverrideActive = true;
                autoWalkEnabled = false;
                Serial.printf("Lamp row=%u col=%u forced off\n", static_cast<unsigned>(row), static_cast<unsigned>(col));
                return;
            }
        }
    }

    if (strcasecmp(command, "WALK") == 0) {
        char* state = strtok_r(nullptr, " \t", &context);
        if (state != nullptr && strcasecmp(state, "ON") == 0) {
            autoWalkEnabled = true;
            manualOverrideActive = true;
            autoWalkRow = 0;
            autoWalkCol = 0;
            lastAutoWalkMs = 0;
            Serial.println("Matrix auto-walk enabled");
            return;
        }
        if (state != nullptr && strcasecmp(state, "OFF") == 0) {
            autoWalkEnabled = false;
            Serial.println("Matrix auto-walk disabled");
            return;
        }
    }

    Serial.println("Unknown matrix test command. Type HELP.");
}
}

void matrixTestSetup() {
#if CAPTAIN_MATRIX_TEST_SUPPORT
    Serial.println("Matrix test support enabled. Type HELP in serial monitor.");
#endif
}

void matrixTestLoop(const CaptainMatrixRuntimeView& runtime, uint32_t now) {
#if CAPTAIN_MATRIX_TEST_SUPPORT
    while (Serial.available() > 0) {
        const int raw = Serial.read();
        if (raw < 0) {
            break;
        }

        const char ch = static_cast<char>(raw);
        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            commandBuffer[commandLength] = '\0';
            matrixTestProcessCommand(runtime, commandBuffer);
            commandLength = 0;
            commandBuffer[0] = '\0';
            continue;
        }

        if (commandLength + 1 < MATRIX_TEST_BUFFER_SIZE) {
            commandBuffer[commandLength++] = ch;
        }
    }

    if (autoWalkEnabled && now - lastAutoWalkMs >= MATRIX_TEST_AUTOWALK_INTERVAL_MS) {
        lastAutoWalkMs = now;
        matrixTestApplyAutoWalk(runtime);
    }
#else
    (void)runtime;
    (void)now;
#endif
}

uint8_t matrixTestStatusFlags() {
#if CAPTAIN_MATRIX_TEST_SUPPORT
    uint8_t flags = 0;
    if (manualOverrideActive) {
        flags |= CAPTAIN_MATRIX_DIAG_FLAG_TEST_OVERRIDE;
    }
    if (autoWalkEnabled) {
        flags |= CAPTAIN_MATRIX_DIAG_FLAG_TEST_AUTOWALK;
    }
    return flags;
#else
    return 0;
#endif
}