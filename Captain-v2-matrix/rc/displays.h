#ifndef DISPLAYS_H
#define DISPLAYS_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_LEDBackpack.h>
#include "i2c_bus_config.h"

#define HT16K33_ADDRESS CAPTAIN_DISPLAY_I2C_ADDRESS
#define MAX_SCORE 999999

extern Adafruit_7segment ledDisplay;

void initDisplay();
void displayStartupTest();
void displayGoodMessage(uint16_t durationMs);
void updateLEDScore(uint32_t score);
void clearDisplay();
void setDisplayBrightness(uint8_t level);

#endif
