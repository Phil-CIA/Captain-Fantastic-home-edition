#ifndef DIRECT_INPUT_CONFIG_H
#define DIRECT_INPUT_CONFIG_H

#include <Arduino.h>

enum CaptainDirectInputId : uint8_t {
    DIRECT_INPUT_SW1 = 0,
    DIRECT_INPUT_SW2,
    DIRECT_INPUT_TILT,
    DIRECT_INPUT_START,
    DIRECT_INPUT_COUNT
};

constexpr uint8_t CAPTAIN_DIRECT_INPUT_PINS[DIRECT_INPUT_COUNT] = {
    36,
    39,
    34,
    35
};

constexpr const char* CAPTAIN_DIRECT_INPUT_NAMES[DIRECT_INPUT_COUNT] = {
    "SW1",
    "SW2",
    "Tilt",
    "Start"
};

constexpr bool CAPTAIN_DIRECT_INPUT_ACTIVE_LOW = true;

#endif