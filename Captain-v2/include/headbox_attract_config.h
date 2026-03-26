#ifndef HEADBOX_ATTRACT_CONFIG_H
#define HEADBOX_ATTRACT_CONFIG_H

#include <Arduino.h>

// Runtime headbox attract routine configuration.
constexpr bool CAPTAIN_HEADBOX_ATTRACT_LOOP = true;
constexpr uint32_t CAPTAIN_HEADBOX_ATTRACT_LOOP_PERIOD_MS = 1000;
constexpr uint32_t CAPTAIN_HEADBOX_ATTRACT_MIN_STEP_MS = 20;
constexpr uint8_t CAPTAIN_HEADBOX_ATTRACT_BLINK_STEPS = 0;
constexpr uint8_t CAPTAIN_HEADBOX_ATTRACT_CHASE_LAPS = 1;

// Visual cadence used while OTA is actively writing new firmware.
constexpr uint32_t CAPTAIN_OTA_VISUAL_INTERVAL_MS = 120;

#endif
