#ifndef I2C_BUS_CONFIG_H
#define I2C_BUS_CONFIG_H

#include <stdint.h>

constexpr uint8_t CAPTAIN_I2C_SDA_PIN = 21;
constexpr uint8_t CAPTAIN_I2C_SCL_PIN = 22;
constexpr uint32_t CAPTAIN_I2C_FREQUENCY_HZ = 100000;

constexpr uint8_t CAPTAIN_MATRIX_I2C_SDA_PIN = 16;
constexpr uint8_t CAPTAIN_MATRIX_I2C_SCL_PIN = 17;

constexpr uint8_t CAPTAIN_MATRIX_I2C_ADDRESS = 0x24;
constexpr uint8_t CAPTAIN_DISPLAY_I2C_ADDRESS = 0x70;

#endif