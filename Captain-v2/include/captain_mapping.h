#ifndef CAPTAIN_MAPPING_H
#define CAPTAIN_MAPPING_H

#include <stddef.h>
#include <stdint.h>
#include "captain_protocol.h"

constexpr uint8_t CAPTAIN_MATRIX_ROW_PINS[CAPTAIN_SWITCH_ROWS] = {3, 2, 11, 10, 8, 7, 6, 4};
constexpr uint8_t CAPTAIN_MATRIX_SWITCH_COL_PINS[CAPTAIN_SWITCH_COLS] = {20, 21, 22, 23};

constexpr const char* CAPTAIN_SWITCH_NAMES[CAPTAIN_SWITCH_ROWS][CAPTAIN_SWITCH_COLS] = {
    {"S20 Outhole", "Tilt (moved to direct input)", "S11 Spinner R", "S22 Return Lane R"},
    {"S1 Lane A", "Start (moved to direct input)", "S6 Target 1", "S14 Slingshot L"},
    {"S2 Lane B", "S7 Bumper L", "S12 Side Switch", "S21 Return Lane L"},
    {"S3 Lane C", "S8 Bumper R", "S10 Spinner L", "S18 Bonus Lane L"},
    {"S4 Lane D", "Easy", "S9 Target 2", "S16 Slingshot R"},
    {"S5 Target 3", "Test", "S13 Side Switch", "S19 Bonus Lane R"},
    {"MX7 SW1", "MX7 SW2", "MX7 SW3", "MX7 SW4"},
    {"MX8 SW1", "MX8 SW2", "MX8 SW3", "MX8 SW4"}
};

constexpr const char* CAPTAIN_LAMP_NAMES[CAPTAIN_LAMP_ROWS][CAPTAIN_LAMP_COLS] = {
    {"Game Over", "Unused", "L12 8K Bonus", "L11 9K Bonus", "L22 Return Lane R"},
    {"Unused", "L1 A", "L14 6K Bonus", "L6 Target 1", "L19 1K Bonus"},
    {"Unused", "L2 B", "L15 5K Bonus", "L7 Double Bonus", "L21 Return Lane L"},
    {"Unused", "L3 C", "L13 7K Bonus", "L10 10K Bonus", "L18 2K Bonus"},
    {"Unused", "L4 D", "L17 3K Bonus", "L9 Target 2", "L20 Same Player"},
    {"Unused", "L5 Target 3", "L16 4K Bonus", "L8 Triple Bonus", "B5 Ball 5"},
    {"Unused", "B1 Ball 1", "B2 Ball 2", "B3 Ball 3", "B4 Ball 4"},
    {"Unused", "P1 Player 1", "P2 Player 2", "P3 Player 3", "P4 Player 4"}
};

inline size_t captainSwitchBitIndex(uint8_t row, uint8_t col) {
    return static_cast<size_t>(row) * CAPTAIN_SWITCH_COLS + col;
}

inline size_t captainLampBitIndex(uint8_t row, uint8_t col) {
    return static_cast<size_t>(row) * CAPTAIN_LAMP_COLS + col;
}

inline bool captainGetBit(const uint8_t* buffer, size_t bitIndex) {
    const size_t byteIndex = bitIndex / 8;
    const uint8_t mask = static_cast<uint8_t>(1u << (bitIndex % 8));
    return (buffer[byteIndex] & mask) != 0;
}

inline void captainSetBit(uint8_t* buffer, size_t bitIndex, bool state) {
    const size_t byteIndex = bitIndex / 8;
    const uint8_t mask = static_cast<uint8_t>(1u << (bitIndex % 8));
    if (state) {
        buffer[byteIndex] |= mask;
    } else {
        buffer[byteIndex] &= static_cast<uint8_t>(~mask);
    }
}

inline const char* captainSwitchName(uint8_t row, uint8_t col) {
    if (row >= CAPTAIN_SWITCH_ROWS || col >= CAPTAIN_SWITCH_COLS) {
        return "Unknown Switch";
    }
    return CAPTAIN_SWITCH_NAMES[row][col];
}

inline const char* captainLampName(uint8_t row, uint8_t col) {
    if (row >= CAPTAIN_LAMP_ROWS || col >= CAPTAIN_LAMP_COLS) {
        return "Unknown Lamp";
    }
    return CAPTAIN_LAMP_NAMES[row][col];
}

#endif