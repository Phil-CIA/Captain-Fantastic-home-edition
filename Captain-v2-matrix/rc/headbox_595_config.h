#ifndef HEADBOX_595_CONFIG_H
#define HEADBOX_595_CONFIG_H

#include <Arduino.h>

constexpr int8_t CAPTAIN_HEADBOX_595_DATA_PIN = 2;
constexpr int8_t CAPTAIN_HEADBOX_595_CLOCK_PIN = 12;
constexpr int8_t CAPTAIN_HEADBOX_595_LATCH_PIN = 4;
constexpr bool CAPTAIN_HEADBOX_595_MSB_FIRST = true;

constexpr uint8_t CAPTAIN_SR_BIT_U3_Q0 = 0;
constexpr uint8_t CAPTAIN_SR_BIT_U3_Q1 = 1;
constexpr uint8_t CAPTAIN_SR_BIT_U3_Q2 = 2;
constexpr uint8_t CAPTAIN_SR_BIT_U3_Q3 = 3;
constexpr uint8_t CAPTAIN_SR_BIT_U3_Q4 = 4;
constexpr uint8_t CAPTAIN_SR_BIT_U3_Q5 = 5;
constexpr uint8_t CAPTAIN_SR_BIT_U3_Q6 = 6;
constexpr uint8_t CAPTAIN_SR_BIT_U3_Q7 = 7;
constexpr uint8_t CAPTAIN_SR_BIT_U4_Q0 = 8;
constexpr uint8_t CAPTAIN_SR_BIT_U4_Q1 = 9;
constexpr uint8_t CAPTAIN_SR_BIT_U4_Q2 = 10;
constexpr uint8_t CAPTAIN_SR_BIT_U4_Q3 = 11;
constexpr uint8_t CAPTAIN_SR_BIT_U4_Q4 = 12;
constexpr uint8_t CAPTAIN_SR_BIT_U4_Q5 = 13;
constexpr uint8_t CAPTAIN_SR_BIT_U4_Q6 = 14;
constexpr uint8_t CAPTAIN_SR_BIT_U4_Q7 = 15;

enum CaptainHeadboxLampId : uint8_t {
    HEADBOX_BALL_5 = 0,
    HEADBOX_BALL_4,
    HEADBOX_BALL_3,
    HEADBOX_BALL_2,
    HEADBOX_BALL_1,
    HEADBOX_TILT,
    HEADBOX_PLAYER_4,
    HEADBOX_PLAYER_3,
    HEADBOX_PLAYER_2,
    HEADBOX_PLAYER_1,
    HEADBOX_GAME_OVER,
    HEADBOX_SPARE_11,
    HEADBOX_SPARE_12,
    HEADBOX_SPARE_13,
    HEADBOX_SPARE_14,
    HEADBOX_SPARE_15,
    HEADBOX_LAMP_COUNT
};

constexpr const char* CAPTAIN_HEADBOX_LAMP_NAMES[HEADBOX_LAMP_COUNT] = {
    "Ball 5",
    "Ball 4",
    "Ball 3",
    "Ball 2",
    "Ball 1",
    "Tilt",
    "Player 4",
    "Player 3",
    "Player 2",
    "Player 1",
    "Game Over",
    "Spare 11",
    "Spare 12",
    "Spare 13",
    "Spare 14",
    "Spare 15"
};

constexpr uint8_t CAPTAIN_HEADBOX_LAMP_TO_SR_BIT[HEADBOX_LAMP_COUNT] = {
    CAPTAIN_SR_BIT_U4_Q1,
    CAPTAIN_SR_BIT_U4_Q0,
    CAPTAIN_SR_BIT_U3_Q7,
    CAPTAIN_SR_BIT_U3_Q6,
    CAPTAIN_SR_BIT_U3_Q5,
    CAPTAIN_SR_BIT_U3_Q4,
    CAPTAIN_SR_BIT_U3_Q3,
    CAPTAIN_SR_BIT_U3_Q2,
    CAPTAIN_SR_BIT_U3_Q1,
    CAPTAIN_SR_BIT_U3_Q0,
    CAPTAIN_SR_BIT_U4_Q2,
    CAPTAIN_SR_BIT_U4_Q3,
    CAPTAIN_SR_BIT_U4_Q4,
    CAPTAIN_SR_BIT_U4_Q5,
    CAPTAIN_SR_BIT_U4_Q6,
    CAPTAIN_SR_BIT_U4_Q7
};

#endif