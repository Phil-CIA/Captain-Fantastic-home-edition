#ifndef CAPTAIN_PROTOCOL_H
#define CAPTAIN_PROTOCOL_H

#include <Arduino.h>
#include "i2c_bus_config.h"

constexpr uint8_t CAPTAIN_SWITCH_ROWS = 8;
constexpr uint8_t CAPTAIN_SWITCH_COLS = 4;
constexpr uint8_t CAPTAIN_LAMP_ROWS = 8;
constexpr uint8_t CAPTAIN_LAMP_COLS = 5;
constexpr size_t CAPTAIN_SWITCH_BYTES = (CAPTAIN_SWITCH_ROWS * CAPTAIN_SWITCH_COLS + 7) / 8;
constexpr size_t CAPTAIN_LAMP_BYTES = (CAPTAIN_LAMP_ROWS * CAPTAIN_LAMP_COLS + 7) / 8;

struct MatrixToControlFrame {
    uint32_t sequence;
    uint32_t uptimeMs;
    uint8_t switchBits[CAPTAIN_SWITCH_BYTES];
    uint8_t checksum;
} __attribute__((packed));

struct ControlToMatrixCommand {
    uint32_t sequence;
    uint8_t lampBits[CAPTAIN_LAMP_BYTES];
    uint8_t checksum;
} __attribute__((packed));

inline uint8_t captainChecksum(const uint8_t* data, size_t lenWithoutChecksum) {
    uint8_t value = 0;
    for (size_t i = 0; i < lenWithoutChecksum; i++) {
        value ^= data[i];
    }
    return value;
}

#endif
