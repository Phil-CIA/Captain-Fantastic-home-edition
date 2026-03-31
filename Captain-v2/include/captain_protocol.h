#ifndef CAPTAIN_PROTOCOL_H
#define CAPTAIN_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include "i2c_bus_config.h"

constexpr uint8_t CAPTAIN_SWITCH_ROWS = 8;
constexpr uint8_t CAPTAIN_SWITCH_COLS = 4;
constexpr uint8_t CAPTAIN_LAMP_ROWS = 8;
constexpr uint8_t CAPTAIN_LAMP_COLS = 5;
constexpr size_t CAPTAIN_SWITCH_BYTES = (CAPTAIN_SWITCH_ROWS * CAPTAIN_SWITCH_COLS + 7) / 8;
constexpr size_t CAPTAIN_LAMP_BYTES = (CAPTAIN_LAMP_ROWS * CAPTAIN_LAMP_COLS + 7) / 8;

constexpr uint8_t CAPTAIN_MATRIX_REG_LAMP_BASE = 0x00;
constexpr uint8_t CAPTAIN_MATRIX_REG_LAMP_END = CAPTAIN_MATRIX_REG_LAMP_BASE + CAPTAIN_LAMP_ROWS - 1;
constexpr uint8_t CAPTAIN_MATRIX_REG_SWITCH_BASE = 0x40;
constexpr uint8_t CAPTAIN_MATRIX_REG_SWITCH_END = CAPTAIN_MATRIX_REG_SWITCH_BASE + CAPTAIN_SWITCH_BYTES - 1;
constexpr uint8_t CAPTAIN_MATRIX_REG_DIAG_BASE = 0xF0;
constexpr uint8_t CAPTAIN_MATRIX_REG_DIAG_END = 0xF3;

constexpr uint8_t CAPTAIN_MATRIX_CMD_SYSTEM_SETUP = 0x20;
constexpr uint8_t CAPTAIN_MATRIX_CMD_SYSTEM_ENABLE = 0x01;
constexpr uint8_t CAPTAIN_MATRIX_CMD_OUTPUT_SETUP = 0x80;
constexpr uint8_t CAPTAIN_MATRIX_CMD_OUTPUT_ENABLE = 0x01;
constexpr uint8_t CAPTAIN_MATRIX_CMD_PULSE_WIDTH_BASE = 0xE0;
constexpr uint8_t CAPTAIN_MATRIX_CMD_PULSE_WIDTH_MASK = 0x0F;

constexpr uint8_t CAPTAIN_MATRIX_DEFAULT_PULSE_WIDTH_LEVEL = 4;

#ifndef CAPTAIN_MATRIX_TEST_SUPPORT
#define CAPTAIN_MATRIX_TEST_SUPPORT 0
#endif

constexpr uint8_t CAPTAIN_MATRIX_DIAG_FLAG_SYSTEM_ENABLED = 0x01;
constexpr uint8_t CAPTAIN_MATRIX_DIAG_FLAG_OUTPUT_ENABLED = 0x02;
constexpr uint8_t CAPTAIN_MATRIX_DIAG_FLAG_TEST_OVERRIDE = 0x04;
constexpr uint8_t CAPTAIN_MATRIX_DIAG_FLAG_TEST_AUTOWALK = 0x08;

inline bool captainMatrixLampRegister(uint8_t reg) {
    return reg >= CAPTAIN_MATRIX_REG_LAMP_BASE && reg <= CAPTAIN_MATRIX_REG_LAMP_END;
}

inline bool captainMatrixSwitchRegister(uint8_t reg) {
    return reg >= CAPTAIN_MATRIX_REG_SWITCH_BASE && reg <= CAPTAIN_MATRIX_REG_SWITCH_END;
}

inline bool captainMatrixDiagnosticRegister(uint8_t reg) {
    return reg >= CAPTAIN_MATRIX_REG_DIAG_BASE && reg <= CAPTAIN_MATRIX_REG_DIAG_END;
}

inline uint8_t captainMatrixLampRowMask(uint8_t col) {
    if (col >= CAPTAIN_LAMP_COLS) {
        return 0;
    }
    return static_cast<uint8_t>(1u << col);
}

#endif
