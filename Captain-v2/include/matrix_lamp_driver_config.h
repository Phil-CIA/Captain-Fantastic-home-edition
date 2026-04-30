#ifndef MATRIX_LAMP_DRIVER_CONFIG_H
#define MATRIX_LAMP_DRIVER_CONFIG_H

#include <stdint.h>

constexpr int8_t CAPTAIN_MATRIX_SR_DATA_PIN = 15;
constexpr int8_t CAPTAIN_MATRIX_SR_CLOCK_PIN = 22;
constexpr int8_t CAPTAIN_MATRIX_SR_LATCH_PIN = 23;
constexpr int8_t CAPTAIN_MATRIX_SR_OE_N_PIN = 10;

constexpr uint8_t CAPTAIN_MATRIX_SR_BIT_LAMP_COL0 = 0;
constexpr uint8_t CAPTAIN_MATRIX_SR_BIT_LAMP_COL1 = 1;
constexpr uint8_t CAPTAIN_MATRIX_SR_BIT_LAMP_COL2 = 2;
constexpr uint8_t CAPTAIN_MATRIX_SR_BIT_LAMP_COL3 = 3;
constexpr uint8_t CAPTAIN_MATRIX_SR_BIT_LAMP_COL4 = 4;

#endif