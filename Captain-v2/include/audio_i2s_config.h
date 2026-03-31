#ifndef AUDIO_I2S_CONFIG_H
#define AUDIO_I2S_CONFIG_H

#include <Arduino.h>
#include "driver/i2s.h"

constexpr i2s_port_t CAPTAIN_AUDIO_I2S_PORT = I2S_NUM_0;
constexpr uint8_t CAPTAIN_AUDIO_DIN_PIN = 25;
constexpr uint8_t CAPTAIN_AUDIO_BCLK_PIN = 14;
constexpr uint8_t CAPTAIN_AUDIO_LRCK_PIN = 13;

// Keep default OFF until hardware routing is physically confirmed.
#ifndef CAPTAIN_AUDIO_SWAP_BCLK_LRCK_FOR_TEST
#define CAPTAIN_AUDIO_SWAP_BCLK_LRCK_FOR_TEST 0
#endif

constexpr uint32_t CAPTAIN_AUDIO_SAMPLE_RATE = 22050;
constexpr uint16_t CAPTAIN_AUDIO_TEST_AMPLITUDE = 5000;

// Bring-up test pattern played on boot to validate MAX98357 wiring/audio path.
constexpr bool CAPTAIN_AUDIO_STARTUP_TEST_ENABLED = true;
constexpr uint8_t CAPTAIN_AUDIO_STARTUP_TEST_REPEATS = 2;
constexpr uint16_t CAPTAIN_AUDIO_STARTUP_TONE_MS = 100;
constexpr uint16_t CAPTAIN_AUDIO_STARTUP_GAP_MS = 35;

// Keeps I2S clocks/data active in short bursts for easier logic-analyzer probing.
constexpr bool CAPTAIN_AUDIO_CONTINUOUS_DIAGNOSTIC = true;
constexpr uint32_t CAPTAIN_AUDIO_CONTINUOUS_INTERVAL_MS = 90;
constexpr uint16_t CAPTAIN_AUDIO_CONTINUOUS_TONE_MS = 45;

// Direct GPIO pulse sanity test before I2S init.
constexpr bool CAPTAIN_AUDIO_PIN_SANITY_TEST = false;
constexpr uint16_t CAPTAIN_AUDIO_PIN_SANITY_PULSE_MS = 2;
constexpr uint16_t CAPTAIN_AUDIO_PIN_SANITY_PULSES = 200;

// Pure GPIO pulse test mode: bypasses all I2S and repeatedly toggles one pin.
constexpr bool CAPTAIN_AUDIO_GPIO_ONLY_TEST_MODE = false;
constexpr uint8_t CAPTAIN_AUDIO_GPIO_ONLY_TEST_PIN = 13;
constexpr uint16_t CAPTAIN_AUDIO_GPIO_ONLY_HALF_PERIOD_MS = 5;

#endif