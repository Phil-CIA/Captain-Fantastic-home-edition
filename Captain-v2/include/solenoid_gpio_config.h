#ifndef SOLENOID_GPIO_CONFIG_H
#define SOLENOID_GPIO_CONFIG_H

#include <Arduino.h>

enum CaptainSolenoidId : uint8_t {
    SOLENOID_S2 = 0,
    SOLENOID_S3,
    SOLENOID_S4,
    SOLENOID_S5,
    SOLENOID_S6,
    SOLENOID_COUNT
};

constexpr uint8_t CAPTAIN_SOLENOID_PINS[SOLENOID_COUNT] = {
    23,
    19,
    18,
    5,
    17
};

constexpr const char* CAPTAIN_SOLENOID_NAMES[SOLENOID_COUNT] = {
    "S2 / SD2",
    "S3 / SD3",
    "S4 / SD4",
    "S5 / SD5",
    "S6 / SD6"
};

constexpr uint16_t CAPTAIN_SOLENOID_PULSE_MS[SOLENOID_COUNT] = {
    45,
    45,
    45,
    40,
    40
};

#endif