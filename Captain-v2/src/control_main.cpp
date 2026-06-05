#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>
#include <driver/i2s.h>
#include "captain_protocol.h"
#include "captain_mapping.h"
#include "solenoid_gpio_config.h"
#include "direct_input_config.h"
#include "audio_i2s_config.h"
#include "i2c_bus_config.h"
#include "external_flash_config.h"
#include "ota_config.h"
#include "headbox_attract_config.h"
#include "headbox_595_config.h"
#include "displays.h"

#ifndef CAPTAIN_BUILD_GIT_HASH
#define CAPTAIN_BUILD_GIT_HASH "unknown"
#endif

#ifndef CAPTAIN_BUILD_GIT_STATE
#define CAPTAIN_BUILD_GIT_STATE "unknown"
#endif

namespace {
constexpr uint32_t POLL_MS = 30;
constexpr uint32_t DIRECT_INPUT_POLL_MS = 5;
constexpr uint8_t DIRECT_INPUT_DEBOUNCE_TICKS = 3;
constexpr uint8_t HEARTBEAT_PIN = 15;
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 500;
constexpr bool HEARTBEAT_IS_WS2812 = true;
constexpr uint16_t GPIO15_WS2812_CHAIN_PIXELS = 301;  // D1 + external strip chain
struct GameplayLedSegment {
    uint16_t startPixel;
    uint16_t pixelCount;
};
struct GameplayLedRange {
    uint16_t startPixel;
    uint16_t endPixelInclusive;
};
struct GameplayGraphicZone {
    const GameplayLedRange* ranges;
    uint8_t rangeCount;
};
constexpr uint8_t GAMEPLAY_LED_SEGMENT_COUNT = 15;
// Physical strip map (pixel 0 reserved for onboard first pixel, strip starts at pixel 1).
constexpr GameplayLedSegment GAMEPLAY_LED_SEGMENTS[GAMEPLAY_LED_SEGMENT_COUNT] = {
    {1, 11},
    {12, 11},
    {23, 30},
    {53, 32},
    {85, 30},
    {115, 23},
    {138, 9},
    {147, 22},
    {169, 26},
    {195, 18},
    {213, 18},
    {231, 15},
    {246, 19},
    {265, 8},
    {273, 28},
};
// Graphic zones from playfield artwork mapping.
constexpr GameplayLedRange GAMEPLAY_GRAPHIC_ZONE_1[] = {
    {175, 179},
    {245, 249},
};
constexpr GameplayLedRange GAMEPLAY_GRAPHIC_ZONE_2[] = {
    {175, 179},
    {230, 249},
    {1, 8},
};
constexpr GameplayLedRange GAMEPLAY_GRAPHIC_ZONE_3[] = {
    {14, 30},
};
constexpr GameplayLedRange GAMEPLAY_GRAPHIC_ZONE_4[] = {
    {40, 60},
};
constexpr GameplayLedRange GAMEPLAY_GRAPHIC_ZONE_5[] = {
    {184, 199},
    {256, 264},
};
constexpr GameplayLedRange GAMEPLAY_GRAPHIC_ZONE_6[] = {
    {272, 293},
    {212, 230},
    {1, 4},
};
constexpr GameplayLedRange GAMEPLAY_GRAPHIC_ZONE_6A[] = {
    {275, 277},
    {215, 217},
};
constexpr GameplayLedRange GAMEPLAY_GRAPHIC_ZONE_6B[] = {
    {277, 280},
    {218, 221},
};
constexpr GameplayLedRange GAMEPLAY_GRAPHIC_ZONE_6C[] = {
    {281, 282},
    {222, 224},
};
constexpr GameplayLedRange GAMEPLAY_GRAPHIC_ZONE_6D[] = {
    {282, 286},
    {225, 227},
};
constexpr GameplayGraphicZone GAMEPLAY_GRAPHIC_ZONES[] = {
    {GAMEPLAY_GRAPHIC_ZONE_1, static_cast<uint8_t>(sizeof(GAMEPLAY_GRAPHIC_ZONE_1) / sizeof(GAMEPLAY_GRAPHIC_ZONE_1[0]))},
    {GAMEPLAY_GRAPHIC_ZONE_2, static_cast<uint8_t>(sizeof(GAMEPLAY_GRAPHIC_ZONE_2) / sizeof(GAMEPLAY_GRAPHIC_ZONE_2[0]))},
    {GAMEPLAY_GRAPHIC_ZONE_3, static_cast<uint8_t>(sizeof(GAMEPLAY_GRAPHIC_ZONE_3) / sizeof(GAMEPLAY_GRAPHIC_ZONE_3[0]))},
    {GAMEPLAY_GRAPHIC_ZONE_4, static_cast<uint8_t>(sizeof(GAMEPLAY_GRAPHIC_ZONE_4) / sizeof(GAMEPLAY_GRAPHIC_ZONE_4[0]))},
    {GAMEPLAY_GRAPHIC_ZONE_5, static_cast<uint8_t>(sizeof(GAMEPLAY_GRAPHIC_ZONE_5) / sizeof(GAMEPLAY_GRAPHIC_ZONE_5[0]))},
    {GAMEPLAY_GRAPHIC_ZONE_6, static_cast<uint8_t>(sizeof(GAMEPLAY_GRAPHIC_ZONE_6) / sizeof(GAMEPLAY_GRAPHIC_ZONE_6[0]))},
    {GAMEPLAY_GRAPHIC_ZONE_6A, static_cast<uint8_t>(sizeof(GAMEPLAY_GRAPHIC_ZONE_6A) / sizeof(GAMEPLAY_GRAPHIC_ZONE_6A[0]))},
    {GAMEPLAY_GRAPHIC_ZONE_6B, static_cast<uint8_t>(sizeof(GAMEPLAY_GRAPHIC_ZONE_6B) / sizeof(GAMEPLAY_GRAPHIC_ZONE_6B[0]))},
    {GAMEPLAY_GRAPHIC_ZONE_6C, static_cast<uint8_t>(sizeof(GAMEPLAY_GRAPHIC_ZONE_6C) / sizeof(GAMEPLAY_GRAPHIC_ZONE_6C[0]))},
    {GAMEPLAY_GRAPHIC_ZONE_6D, static_cast<uint8_t>(sizeof(GAMEPLAY_GRAPHIC_ZONE_6D) / sizeof(GAMEPLAY_GRAPHIC_ZONE_6D[0]))},
};
constexpr uint8_t GAMEPLAY_GRAPHIC_ZONE_COUNT = static_cast<uint8_t>(sizeof(GAMEPLAY_GRAPHIC_ZONES) / sizeof(GAMEPLAY_GRAPHIC_ZONES[0]));
constexpr uint8_t GAMEPLAY_LED_MAX_CHANNEL = 96;
constexpr uint16_t GAMEPLAY_LED_CURRENT_LIMIT_MA = 1200;
constexpr bool HEARTBEAT_ENABLE_IN_ATTRACT = false;
constexpr bool HEARTBEAT_ENABLE_IN_SERVE_BALL = false;
constexpr bool HEARTBEAT_ENABLE_IN_BALL_IN_PLAY = false;
constexpr bool HEARTBEAT_ENABLE_IN_BONUS_COUNTDOWN = false;
constexpr bool HEARTBEAT_ENABLE_IN_GAME_OVER = false;
constexpr bool HEARTBEAT_FEATURE_ENABLED =
    HEARTBEAT_ENABLE_IN_ATTRACT ||
    HEARTBEAT_ENABLE_IN_SERVE_BALL ||
    HEARTBEAT_ENABLE_IN_BALL_IN_PLAY ||
    HEARTBEAT_ENABLE_IN_BONUS_COUNTDOWN ||
    HEARTBEAT_ENABLE_IN_GAME_OVER;
constexpr bool GAMEPLAY_LED_FEEDBACK_ENABLED = true;
constexpr uint32_t GAMEPLAY_LED_PULSE_MS = 80;
constexpr uint32_t GAMEPLAY_LED_ANIM_STEP_MS = 120;
constexpr uint32_t MATRIX_DIAG_POLL_MS = 250;
constexpr uint32_t MATRIX_LINK_TIMEOUT_MS = 1000;
constexpr uint32_t MATRIX_LINK_SUMMARY_MS = 1000;
constexpr uint32_t MATRIX_INIT_RETRY_MS = 1000;
constexpr uint32_t MATRIX_LAMP_KEEPALIVE_MS = 250;
constexpr bool MATRIX_TRACE_ENABLED = false;
constexpr uint32_t MATRIX_TRACE_WINDOW_MS = 15000;
constexpr uint32_t MATRIX_SWITCH_LOG_DEBOUNCE_MS = 250;
constexpr uint32_t MATRIX_SWITCH_LOG_REPORT_MS = 1000;
constexpr uint16_t MATRIX_SWITCH_LOG_MAX_PER_REPORT = 12;
constexpr uint8_t MATRIX_SWITCH_CONFIRM_POLLS = 5;
constexpr bool MATRIX_SWITCH_BITS_ACTIVE_HIGH = false;
// Diagnostic mode: force matrix lamps off to isolate switch mapping from lamp-scan coupling.
// Disabled for normal solenoid bring-up/gameplay.
constexpr bool MATRIX_SWITCH_MAPPING_MODE = false;
constexpr bool START_BUTTON_SOLENOID_TEST_ENABLED = false;
constexpr bool MATRIX_SWITCH_SOLENOIDS_ENABLED = true;
constexpr uint8_t S20_OUTHOLE_SWITCH_ROW = 0;
constexpr uint8_t S20_OUTHOLE_SWITCH_COL = 0;
constexpr uint32_t S20_OUTHOLE_RETRIGGER_COOLDOWN_MS = 50;
// Temporary S2 safety limiter: prevent outhole retrigger loops while eject force is weak.
constexpr uint32_t S2_RETRIGGER_COOLDOWN_MS = 1500;
constexpr uint32_t S2_WINDOW_MS = 10000;
constexpr uint8_t S2_MAX_FIRES_PER_WINDOW = 3;
constexpr uint8_t S2_MAX_FIRES_PER_BALL = 5;
// Real gameplay: no more than 2-3 switches can close simultaneously (ball hits multiple targets).
// Scan transients appear as 5-28 edges per poll. Threshold of 4 passes genuine multi-switch
// events while blocking all observed scan-induced bursts.
constexpr uint8_t MATRIX_MAX_RISING_EDGES_PER_POLL = 4;
constexpr BaseType_t CONTROL_TASK_CORE = 1;
constexpr BaseType_t AUDIO_TASK_CORE = 0;
constexpr UBaseType_t CONTROL_TASK_PRIORITY = 3;
constexpr UBaseType_t AUDIO_TASK_PRIORITY = 2;
constexpr uint32_t CONTROL_TASK_STACK_BYTES = 8192;
constexpr uint32_t AUDIO_TASK_STACK_BYTES = 6144;
constexpr uint8_t AUDIO_QUEUE_LENGTH = 16;

struct AudioToneEvent {
    uint16_t frequencyHz;
    uint16_t durationMs;
};

enum CaptainGameMode : uint8_t {
    GAME_MODE_ATTRACT = 0,
    GAME_MODE_SERVE_BALL,
    GAME_MODE_BALL_IN_PLAY,
    GAME_MODE_BONUS_COUNTDOWN,
    GAME_MODE_GAME_OVER
};

struct CaptainGameplayState {
    CaptainGameMode mode = GAME_MODE_ATTRACT;
    uint32_t score = 0;
    uint16_t bonus = 1000;
    uint8_t bonusMultiplier = 1;
    uint8_t currentBall = 0;
    bool ballInPlay = false;
    bool laneAComplete = false;
    bool laneBComplete = false;
    bool laneCComplete = false;
    bool laneDComplete = false;
    bool target1Complete = false;
    bool target2Complete = false;
    bool target3Complete = false;
    bool samePlayerLit = false;
    uint32_t lastBonusStepMs = 0;
};

constexpr uint32_t BONUS_COUNTDOWN_STEP_MS = 200;
constexpr uint16_t GAMEPLAY_BONUS_START = 1000;
constexpr uint16_t GAMEPLAY_BONUS_MAX = 10000;

uint32_t displayScore = 0;
uint32_t lastDisplayedScore = UINT32_MAX;
uint32_t lastDisplayUpdate = 0;
uint8_t previousSwitchBits[CAPTAIN_SWITCH_BYTES] = {};
uint8_t stableMatrixSwitchBits[CAPTAIN_SWITCH_BYTES] = {};
uint8_t candidateMatrixSwitchBits[CAPTAIN_SWITCH_BYTES] = {};
uint8_t matrixSwitchConfirmTicks[CAPTAIN_SWITCH_ROWS * CAPTAIN_SWITCH_COLS] = {};
uint8_t lastMatrixLampRows[CAPTAIN_LAMP_ROWS] = {};
bool matrixLampFramePrimed = false;
uint32_t lastMatrixLampWriteMs = 0;
uint16_t headboxPattern = 0;
bool solenoidActive[SOLENOID_COUNT] = {false};
uint32_t solenoidStartedAtMs[SOLENOID_COUNT] = {0};
bool directInputStable[DIRECT_INPUT_COUNT] = {false};
uint8_t directInputDebounceCounter[DIRECT_INPUT_COUNT] = {0};
uint32_t lastDirectInputPollMs = 0;
bool tiltLatched = false;
bool i2sAudioReady = false;
uint32_t i2sWriteCalls = 0;
uint32_t i2sWriteErrors = 0;
uint32_t i2sBytesWrittenTotal = 0;
uint8_t audioBclkPinEffective = CAPTAIN_AUDIO_BCLK_PIN;
uint8_t audioLrckPinEffective = CAPTAIN_AUDIO_LRCK_PIN;
bool heartbeatEnabled = false;
bool heartbeatState = false;
uint32_t lastHeartbeatToggleMs = 0;
Adafruit_NeoPixel heartbeatPixel(GPIO15_WS2812_CHAIN_PIXELS, HEARTBEAT_PIN, NEO_GRB + NEO_KHZ800);
bool gameplayLedPulseActive = false;
uint8_t gameplayLedPulseSegment = 0;
uint32_t gameplayLedPulseUntilMs = 0;
uint32_t gameplayLedLastAnimStepMs = 0;
uint8_t gameplayLedAnimStep = 0;
bool otaReady = false;
bool otaInProgress = false;
uint32_t lastOtaVisualToggleMs = 0;
bool otaVisualState = false;
uint32_t lastHeadboxAttractStepMs = 0;
uint8_t headboxAttractStep = 0;
bool matrixDeviceReady = false;
uint32_t lastMatrixGoodTransactionMs = 0;
uint32_t lastMatrixInitAttemptMs = 0;
uint32_t lastMatrixDiagPollMs = 0;
bool matrixLinkFaulted = false;
uint32_t matrixWriteOkCount = 0;
uint32_t matrixWriteFailCount = 0;
uint32_t matrixReadOkCount = 0;
uint32_t matrixReadFailCount = 0;
uint32_t matrixDiagReadFailCount = 0;
uint32_t matrixDiagWarnCount = 0;
uint32_t lastMatrixLinkSummaryMs = 0;
uint8_t lastRawMatrixSwitch0 = 0;
uint8_t lastRawMatrixSwitch1 = 0;
uint8_t lastRawMatrixSwitch2 = 0;
uint8_t lastRawMatrixSwitch3 = 0;
uint8_t lastMatrixSwitch0 = 0;
uint8_t lastMatrixSwitch1 = 0;
uint8_t lastMatrixSwitch2 = 0;
uint8_t lastMatrixSwitch3 = 0;
bool matrixSwitch0Seen = false;
bool matrixDiagFaulted = false;
uint32_t matrixSwitchLogSuppressedDebounce = 0;
uint32_t matrixSwitchLogSuppressedRate = 0;
uint32_t matrixSwitchSuppressedBurst = 0;
uint32_t matrixSwitchSuppressedLampEcho = 0;
uint32_t matrixTraceSeq = 0;
uint32_t lastMatrixSwitchEdgeLogMs = 0;
uint32_t s20OutholeLastRiseMs = 0;
uint32_t s20OutholeSuppressedSticky = 0;
uint32_t s2SuppressedCooldown = 0;
uint32_t s2SuppressedWindow = 0;
uint32_t s2SuppressedBall = 0;
bool currentSW1Mode = false;  // false = Easy, true = Hard
bool currentSW2Mode = false;  // false = Game, true = Test
uint16_t matrixSwitchLoggedThisWindow = 0;
uint16_t matrixSwitchEdgesThisWindow = 0;
uint32_t matrixSwitchLogWindowStartMs = 0;
uint32_t s2WindowStartMs = 0;
uint32_t s2LastFireMs = 0;
uint8_t s2FiresInWindow = 0;
uint8_t s2FiresThisBall = 0;
CaptainGameplayState gameplayState = {};
QueueHandle_t audioToneQueue = nullptr;
TaskHandle_t controlTaskHandle = nullptr;
TaskHandle_t audioTaskHandle = nullptr;

void resetGameplayStateForNewBall();
void startNewGame();
void startBonusCountdown(uint32_t nowMs);
void updateGameplayState(uint32_t nowMs);
void handleGameplaySwitchHit(uint8_t row, uint8_t col, uint32_t nowMs);
void triggerGameplayLedPulse(uint8_t segment, uint8_t red, uint8_t green, uint8_t blue, uint32_t nowMs);
void updateGameplayLedPulse(uint32_t nowMs);
void updateGameplayLedPattern(uint32_t nowMs);
void setGameplayLedZone(uint8_t segment, uint8_t red, uint8_t green, uint8_t blue);
void setGameplayLedZoneNoShow(uint8_t segment, uint8_t red, uint8_t green, uint8_t blue);
void setGameplayGraphicZoneNoShow(uint8_t graphicZone, uint8_t red, uint8_t green, uint8_t blue);
void clearGameplayLedStripBuffer();
void showGameplayLedStripBuffer();
void applyGameplayLedCurrentLimit(uint16_t pixelCount, uint8_t& red, uint8_t& green, uint8_t& blue);
uint8_t mapSwitchToGameplayLedZone(uint8_t row, uint8_t col);
bool queueTone(uint16_t frequencyHz, uint16_t durationMs) {
    if (!i2sAudioReady || CAPTAIN_AUDIO_GPIO_ONLY_TEST_MODE || audioToneQueue == nullptr) {
        return false;
    }

    AudioToneEvent event = {frequencyHz, durationMs};
    if (xQueueSend(audioToneQueue, &event
        , 0) == pdPASS) {
        return true;
    }
    return false;
}

void updateHeadboxLamps(uint16_t pattern);

bool matrixTraceEnabledNow() {
    return MATRIX_TRACE_ENABLED && millis() <= MATRIX_TRACE_WINDOW_MS;
}

void logMatrixTrace(const char* event,
                    uint8_t reg,
                    const uint8_t* bytes,
                    size_t length) {
    if (!matrixTraceEnabledNow()) {
        return;
    }

    const uint8_t b0 = (length > 0 && bytes != nullptr) ? bytes[0] : 0;
    const uint8_t b1 = (length > 1 && bytes != nullptr) ? bytes[1] : 0;
    const uint8_t b2 = (length > 2 && bytes != nullptr) ? bytes[2] : 0;
    const uint8_t b3 = (length > 3 && bytes != nullptr) ? bytes[3] : 0;
    const uint8_t b4 = (length > 4 && bytes != nullptr) ? bytes[4] : 0;

    Serial.printf("[mx %lu] %s reg=0x%02X len=%u b=[%02X,%02X,%02X,%02X,%02X]\n",
                  static_cast<unsigned long>(matrixTraceSeq++),
                  event,
                  static_cast<unsigned>(reg),
                  static_cast<unsigned>(length),
                  static_cast<unsigned>(b0),
                  static_cast<unsigned>(b1),
                  static_cast<unsigned>(b2),
                  static_cast<unsigned>(b3),
                  static_cast<unsigned>(b4));
}

void recordMatrixTransactionResult(bool ok) {
    const uint32_t now = millis();
    if (ok) {
        lastMatrixGoodTransactionMs = now;
        if (matrixLinkFaulted) {
            matrixLinkFaulted = false;
            Serial.println("Matrix link recovered");
        }
        return;
    }

    if (!matrixLinkFaulted && lastMatrixGoodTransactionMs != 0 && (now - lastMatrixGoodTransactionMs) >= MATRIX_LINK_TIMEOUT_MS) {
        matrixLinkFaulted = true;
        Serial.println("Matrix link fault: transaction timeout exceeded 1000 ms safe window");
    }
}

bool matrixWriteCommandByte(uint8_t value) {
    Wire.beginTransmission(CAPTAIN_MATRIX_I2C_ADDRESS);
    Wire.write(value);
    const bool ok = Wire.endTransmission() == 0;
    recordMatrixTransactionResult(ok);
    return ok;
}

bool matrixWriteRegisters(uint8_t startRegister, const uint8_t* data, size_t len) {
    Wire.beginTransmission(CAPTAIN_MATRIX_I2C_ADDRESS);
    Wire.write(startRegister);
    Wire.write(data, len);
    const bool ok = Wire.endTransmission() == 0;
    logMatrixTrace(ok ? "wr" : "wr_fail", startRegister, data, len);
    recordMatrixTransactionResult(ok);
    return ok;
}

bool matrixReadRegisters(uint8_t startRegister, uint8_t* out, size_t len) {
    Wire.beginTransmission(CAPTAIN_MATRIX_I2C_ADDRESS);
    Wire.write(startRegister);
    if (Wire.endTransmission(false) != 0) {
        logMatrixTrace("rd_ptr_fail", startRegister, nullptr, 0);
        recordMatrixTransactionResult(false);
        return false;
    }

    const size_t received = Wire.requestFrom(static_cast<int>(CAPTAIN_MATRIX_I2C_ADDRESS), static_cast<int>(len));
    if (received != len) {
        logMatrixTrace("rd_len_fail", startRegister, nullptr, received);
        while (Wire.available()) {
            Wire.read();
        }
        recordMatrixTransactionResult(false);
        return false;
    }

    for (size_t index = 0; index < len; index++) {
        out[index] = static_cast<uint8_t>(Wire.read());
    }
    logMatrixTrace("rd", startRegister, out, len);
    recordMatrixTransactionResult(true);
    return true;
}

void initMatrixDevice() {
    const uint32_t now = millis();
    lastMatrixInitAttemptMs = now;

    Wire.beginTransmission(CAPTAIN_MATRIX_I2C_ADDRESS);
    const uint8_t probeError = Wire.endTransmission();

    if (probeError == 0) {
        matrixDeviceReady = true;
        lastMatrixGoodTransactionMs = now;
        matrixLinkFaulted = false;
        Serial.printf("Matrix device ready at 0x%02X (simplified bridge mode, no HT16K33 emulation)\n",
                      CAPTAIN_MATRIX_I2C_ADDRESS);
        return;
    }

    matrixDeviceReady = false;
    Serial.printf("Matrix device missing at 0x%02X (err=%u), retrying...\n",
                  CAPTAIN_MATRIX_I2C_ADDRESS,
                  static_cast<unsigned>(probeError));
}

void logMatrixLinkSummary(uint32_t now) {
    if ((now - lastMatrixLinkSummaryMs) < MATRIX_LINK_SUMMARY_MS) {
        return;
    }

    lastMatrixLinkSummaryMs = now;
    Serial.printf("Matrix link: ready=%u fault=%u wr_ok=%lu wr_fail=%lu rd_ok=%lu rd_fail=%lu diag_warn=%lu raw0=0x%02X raw1=0x%02X raw2=0x%02X raw3=0x%02X sw0=0x%02X sw1=0x%02X sw2=0x%02X sw3=0x%02X sw_edges=%u sw_log=%u sup_db=%lu sup_rate=%lu sup_le=%lu s20_sticky=%lu s2_fire=%u s2_cd=%lu s2_win=%lu s2_ball=%lu\n",
                  matrixDeviceReady ? 1u : 0u,
                  matrixLinkFaulted ? 1u : 0u,
                  static_cast<unsigned long>(matrixWriteOkCount),
                  static_cast<unsigned long>(matrixWriteFailCount),
                  static_cast<unsigned long>(matrixReadOkCount),
                  static_cast<unsigned long>(matrixReadFailCount),
                  static_cast<unsigned long>(matrixDiagWarnCount),
                  static_cast<unsigned>(lastRawMatrixSwitch0),
                  static_cast<unsigned>(lastRawMatrixSwitch1),
                  static_cast<unsigned>(lastRawMatrixSwitch2),
                  static_cast<unsigned>(lastRawMatrixSwitch3),
                  static_cast<unsigned>(lastMatrixSwitch0),
                  static_cast<unsigned>(lastMatrixSwitch1),
                  static_cast<unsigned>(lastMatrixSwitch2),
                  static_cast<unsigned>(lastMatrixSwitch3),
                  static_cast<unsigned>(matrixSwitchEdgesThisWindow),
                  static_cast<unsigned>(matrixSwitchLoggedThisWindow),
                  static_cast<unsigned long>(matrixSwitchLogSuppressedDebounce),
                  static_cast<unsigned long>(matrixSwitchLogSuppressedRate),
                  static_cast<unsigned long>(matrixSwitchSuppressedLampEcho),
                  static_cast<unsigned long>(s20OutholeSuppressedSticky),
                  static_cast<unsigned>(s2FiresThisBall),
                  static_cast<unsigned long>(s2SuppressedCooldown),
                  static_cast<unsigned long>(s2SuppressedWindow),
                  static_cast<unsigned long>(s2SuppressedBall));

    if (matrixSwitchSuppressedBurst > 0) {
        Serial.printf("Matrix switch burst filter: dropped=%lu (max rising edges per poll=%u)\n",
                      static_cast<unsigned long>(matrixSwitchSuppressedBurst),
                      static_cast<unsigned>(MATRIX_MAX_RISING_EDGES_PER_POLL));
    }

    matrixSwitchEdgesThisWindow = 0;
    matrixSwitchLoggedThisWindow = 0;
    matrixSwitchLogSuppressedDebounce = 0;
    matrixSwitchLogSuppressedRate = 0;
    matrixSwitchSuppressedBurst = 0;
    matrixSwitchSuppressedLampEcho = 0;
    s20OutholeSuppressedSticky = 0;
    s2SuppressedCooldown = 0;
    s2SuppressedWindow = 0;
    s2SuppressedBall = 0;
    matrixSwitchLogWindowStartMs = now;
}

void updateOtaVisual(uint32_t now) {
    if (now - lastOtaVisualToggleMs < CAPTAIN_OTA_VISUAL_INTERVAL_MS) {
        return;
    }

    lastOtaVisualToggleMs = now;
    otaVisualState = !otaVisualState;
    updateHeadboxLamps(otaVisualState ? 0xFFFF : 0x0000);
    updateLEDScore(otaVisualState ? 888888 : 0);
}

void initWifiAndOta() {
    if (!CAPTAIN_OTA_ENABLED) {
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(CAPTAIN_WIFI_SSID, CAPTAIN_WIFI_PASSWORD);

    const uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < CAPTAIN_WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("OTA/WiFi: connect timeout, OTA disabled");
        return;
    }

    ArduinoOTA.setHostname(CAPTAIN_OTA_HOSTNAME);
    ArduinoOTA.setPassword(CAPTAIN_OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
        otaInProgress = true;
        lastOtaVisualToggleMs = millis();
        otaVisualState = false;
        Serial.println("OTA start");
    });
    ArduinoOTA.onEnd([]() {
        otaInProgress = false;
        updateHeadboxLamps(0x0000);
        Serial.println("OTA complete");
    });
    ArduinoOTA.onError([](ota_error_t error) {
        otaInProgress = false;
        updateHeadboxLamps(0x0000);
        Serial.printf("OTA error: %u\n", static_cast<unsigned>(error));
    });

    ArduinoOTA.begin();
    otaReady = true;
    Serial.printf("OTA ready: host=%s ip=%s\n", CAPTAIN_OTA_HOSTNAME, WiFi.localIP().toString().c_str());
}

void initHeartbeat() {
    if (!HEARTBEAT_FEATURE_ENABLED) {
        if (HEARTBEAT_IS_WS2812) {
            heartbeatPixel.begin();
            heartbeatPixel.setPixelColor(0, heartbeatPixel.Color(0, 0, 0));
            heartbeatPixel.show();
        } else {
            pinMode(HEARTBEAT_PIN, OUTPUT);
            digitalWrite(HEARTBEAT_PIN, LOW);
        }
        heartbeatEnabled = false;
        heartbeatState = false;
        Serial.printf("Heartbeat disabled by policy on GPIO%u\n", HEARTBEAT_PIN);
        return;
    }

    const bool conflictsHeadboxShiftRegister =
        (HEARTBEAT_PIN == static_cast<uint8_t>(CAPTAIN_HEADBOX_595_DATA_PIN)) ||
        (HEARTBEAT_PIN == static_cast<uint8_t>(CAPTAIN_HEADBOX_595_CLOCK_PIN)) ||
        (HEARTBEAT_PIN == static_cast<uint8_t>(CAPTAIN_HEADBOX_595_LATCH_PIN));

    if (conflictsHeadboxShiftRegister) {
        heartbeatEnabled = false;
        Serial.printf("Heartbeat disabled: GPIO%u shared with headbox 74HC595\n", HEARTBEAT_PIN);
        return;
    }

    if (HEARTBEAT_IS_WS2812) {
        heartbeatPixel.begin();
        heartbeatPixel.setPixelColor(0, heartbeatPixel.Color(0, 0, 0));
        heartbeatPixel.show();
    } else {
        pinMode(HEARTBEAT_PIN, OUTPUT);
        digitalWrite(HEARTBEAT_PIN, LOW);
    }

    heartbeatEnabled = true;
    heartbeatState = false;
    lastHeartbeatToggleMs = millis();
    Serial.printf("Heartbeat enabled on GPIO%u (%s)\n", HEARTBEAT_PIN, HEARTBEAT_IS_WS2812 ? "WS2812" : "GPIO");
}

void updateHeartbeat(uint32_t now) {
    if (!heartbeatEnabled) {
        return;
    }

    bool modeAllowsHeartbeat = true;
    switch (gameplayState.mode) {
        case GAME_MODE_ATTRACT:
            modeAllowsHeartbeat = HEARTBEAT_ENABLE_IN_ATTRACT;
            break;
        case GAME_MODE_SERVE_BALL:
            modeAllowsHeartbeat = HEARTBEAT_ENABLE_IN_SERVE_BALL;
            break;
        case GAME_MODE_BALL_IN_PLAY:
            modeAllowsHeartbeat = HEARTBEAT_ENABLE_IN_BALL_IN_PLAY;
            break;
        case GAME_MODE_BONUS_COUNTDOWN:
            modeAllowsHeartbeat = HEARTBEAT_ENABLE_IN_BONUS_COUNTDOWN;
            break;
        case GAME_MODE_GAME_OVER:
            modeAllowsHeartbeat = HEARTBEAT_ENABLE_IN_GAME_OVER;
            break;
        default:
            break;
    }

    if (!modeAllowsHeartbeat) {
        if (heartbeatState) {
            heartbeatState = false;
            if (HEARTBEAT_IS_WS2812) {
                heartbeatPixel.setPixelColor(0, heartbeatPixel.Color(0, 0, 0));
                heartbeatPixel.show();
            } else {
                digitalWrite(HEARTBEAT_PIN, LOW);
            }
        }
        return;
    }

    if (now - lastHeartbeatToggleMs >= HEARTBEAT_INTERVAL_MS) {
        lastHeartbeatToggleMs = now;
        heartbeatState = !heartbeatState;
        if (HEARTBEAT_IS_WS2812) {
            heartbeatPixel.setPixelColor(0, heartbeatState ? heartbeatPixel.Color(0, 24, 0) : heartbeatPixel.Color(0, 0, 0));
            heartbeatPixel.show();
        } else {
            digitalWrite(HEARTBEAT_PIN, heartbeatState ? HIGH : LOW);
        }
    }
}

void triggerGameplayLedPulse(uint8_t segment, uint8_t red, uint8_t green, uint8_t blue, uint32_t nowMs) {
    if (!GAMEPLAY_LED_FEEDBACK_ENABLED || !HEARTBEAT_IS_WS2812) {
        return;
    }

    setGameplayLedZone(segment, red, green, blue);
    gameplayLedPulseActive = true;
    gameplayLedPulseSegment = segment;
    gameplayLedPulseUntilMs = nowMs + GAMEPLAY_LED_PULSE_MS;
}

void updateGameplayLedPulse(uint32_t nowMs) {
    if (!GAMEPLAY_LED_FEEDBACK_ENABLED || !HEARTBEAT_IS_WS2812 || !gameplayLedPulseActive) {
        return;
    }

    if (static_cast<int32_t>(nowMs - gameplayLedPulseUntilMs) >= 0) {
        setGameplayLedZone(gameplayLedPulseSegment, 0, 0, 0);
        gameplayLedPulseActive = false;
    }
}

void applyGameplayLedCurrentLimit(uint16_t pixelCount, uint8_t& red, uint8_t& green, uint8_t& blue) {
    if (red > GAMEPLAY_LED_MAX_CHANNEL) red = GAMEPLAY_LED_MAX_CHANNEL;
    if (green > GAMEPLAY_LED_MAX_CHANNEL) green = GAMEPLAY_LED_MAX_CHANNEL;
    if (blue > GAMEPLAY_LED_MAX_CHANNEL) blue = GAMEPLAY_LED_MAX_CHANNEL;

    const uint16_t channelSum = static_cast<uint16_t>(red) + static_cast<uint16_t>(green) + static_cast<uint16_t>(blue);
    if (channelSum == 0 || pixelCount == 0) {
        return;
    }

    const uint32_t estimatedMa = (static_cast<uint32_t>(pixelCount) * static_cast<uint32_t>(channelSum) * 20u) / 255u;
    if (estimatedMa <= GAMEPLAY_LED_CURRENT_LIMIT_MA) {
        return;
    }

    red = static_cast<uint8_t>((static_cast<uint32_t>(red) * GAMEPLAY_LED_CURRENT_LIMIT_MA) / estimatedMa);
    green = static_cast<uint8_t>((static_cast<uint32_t>(green) * GAMEPLAY_LED_CURRENT_LIMIT_MA) / estimatedMa);
    blue = static_cast<uint8_t>((static_cast<uint32_t>(blue) * GAMEPLAY_LED_CURRENT_LIMIT_MA) / estimatedMa);
}

void clearGameplayLedStripBuffer() {
    const uint16_t total = static_cast<uint16_t>(heartbeatPixel.numPixels());
    for (uint16_t i = 0; i < total; i++) {
        heartbeatPixel.setPixelColor(i, 0);
    }
}

void showGameplayLedStripBuffer() {
    heartbeatPixel.show();
}

void setGameplayLedZoneNoShow(uint8_t segment, uint8_t red, uint8_t green, uint8_t blue) {
    const uint16_t total = static_cast<uint16_t>(heartbeatPixel.numPixels());
    const uint8_t zoneIndex = static_cast<uint8_t>(segment % GAMEPLAY_LED_SEGMENT_COUNT);
    const GameplayLedSegment zoneDef = GAMEPLAY_LED_SEGMENTS[zoneIndex];
    const uint16_t zoneStart = zoneDef.startPixel < total ? zoneDef.startPixel : total;
    const uint16_t zoneEnd = static_cast<uint16_t>((zoneStart + zoneDef.pixelCount) < total ? (zoneStart + zoneDef.pixelCount) : total);
    const uint16_t zonePixels = zoneEnd > zoneStart ? static_cast<uint16_t>(zoneEnd - zoneStart) : 0;
    applyGameplayLedCurrentLimit(zonePixels, red, green, blue);
    const uint32_t color = heartbeatPixel.Color(red, green, blue);

    for (uint16_t i = zoneStart; i < zoneEnd; i++) {
        heartbeatPixel.setPixelColor(i, color);
    }
}

void setGameplayLedZone(uint8_t segment, uint8_t red, uint8_t green, uint8_t blue) {
    clearGameplayLedStripBuffer();
    setGameplayLedZoneNoShow(segment, red, green, blue);
    showGameplayLedStripBuffer();
}

void setGameplayGraphicZoneNoShow(uint8_t graphicZone, uint8_t red, uint8_t green, uint8_t blue) {
    const uint16_t total = static_cast<uint16_t>(heartbeatPixel.numPixels());
    const uint8_t zoneIndex = static_cast<uint8_t>(graphicZone % GAMEPLAY_GRAPHIC_ZONE_COUNT);
    const GameplayGraphicZone& zone = GAMEPLAY_GRAPHIC_ZONES[zoneIndex];

    uint16_t zonePixelCount = 0;
    for (uint8_t r = 0; r < zone.rangeCount; r++) {
        const GameplayLedRange& span = zone.ranges[r];
        if (span.endPixelInclusive < span.startPixel) {
            continue;
        }
        const uint16_t clampedStart = span.startPixel < total ? span.startPixel : total;
        const uint16_t clampedEnd = span.endPixelInclusive < total ? span.endPixelInclusive : static_cast<uint16_t>(total > 0 ? total - 1 : 0);
        if (clampedEnd >= clampedStart) {
            zonePixelCount = static_cast<uint16_t>(zonePixelCount + (clampedEnd - clampedStart + 1));
        }
    }

    applyGameplayLedCurrentLimit(zonePixelCount, red, green, blue);
    const uint32_t color = heartbeatPixel.Color(red, green, blue);

    for (uint8_t r = 0; r < zone.rangeCount; r++) {
        const GameplayLedRange& span = zone.ranges[r];
        if (span.endPixelInclusive < span.startPixel) {
            continue;
        }
        const uint16_t clampedStart = span.startPixel < total ? span.startPixel : total;
        const uint16_t clampedEnd = span.endPixelInclusive < total ? span.endPixelInclusive : static_cast<uint16_t>(total > 0 ? total - 1 : 0);
        if (clampedEnd < clampedStart) {
            continue;
        }
        for (uint16_t i = clampedStart; i <= clampedEnd; i++) {
            heartbeatPixel.setPixelColor(i, color);
        }
    }
}

void updateGameplayLedPattern(uint32_t nowMs) {
    if (!GAMEPLAY_LED_FEEDBACK_ENABLED || !HEARTBEAT_IS_WS2812 || gameplayLedPulseActive) {
        return;
    }

    if ((nowMs - gameplayLedLastAnimStepMs) < GAMEPLAY_LED_ANIM_STEP_MS) {
        return;
    }
    gameplayLedLastAnimStepMs = nowMs;
    gameplayLedAnimStep++;

    clearGameplayLedStripBuffer();

    switch (gameplayState.mode) {
        case GAME_MODE_ATTRACT: {
            const uint8_t lead = static_cast<uint8_t>(gameplayLedAnimStep % GAMEPLAY_GRAPHIC_ZONE_COUNT);
            const uint8_t tail = static_cast<uint8_t>((lead + GAMEPLAY_GRAPHIC_ZONE_COUNT - 1) % GAMEPLAY_GRAPHIC_ZONE_COUNT);
            setGameplayGraphicZoneNoShow(lead, 96, 170, 210);
            setGameplayGraphicZoneNoShow(tail, 110, 50, 90);
            break;
        }
        case GAME_MODE_SERVE_BALL: {
            const uint8_t serveSegment = static_cast<uint8_t>((gameplayState.currentBall == 0 ? 0 : gameplayState.currentBall - 1) % GAMEPLAY_GRAPHIC_ZONE_COUNT);
            const bool on = ((gameplayLedAnimStep % 4u) < 2u);
            if (on) {
                setGameplayGraphicZoneNoShow(serveSegment, 170, 160, 40);
            }
            break;
        }
        case GAME_MODE_BALL_IN_PLAY: {
            const uint8_t litSegments = static_cast<uint8_t>(min<uint32_t>(GAMEPLAY_GRAPHIC_ZONE_COUNT, (gameplayState.score / 3000u) + 1u));
            uint8_t r = 48;
            uint8_t g = 130;
            uint8_t b = 80;
            if (gameplayState.bonusMultiplier == 2) {
                r = 80; g = 120; b = 180;
            } else if (gameplayState.bonusMultiplier >= 3) {
                r = 180; g = 90; b = 190;
            }
            for (uint8_t i = 0; i < litSegments; i++) {
                setGameplayGraphicZoneNoShow(i, r, g, b);
            }
            break;
        }
        case GAME_MODE_BONUS_COUNTDOWN: {
            const bool on = ((gameplayLedAnimStep % 2u) == 0u);
            if (on) {
                for (uint8_t i = 0; i < GAMEPLAY_GRAPHIC_ZONE_COUNT; i++) {
                    setGameplayGraphicZoneNoShow(i, 180, 95, 20);
                }
            }
            break;
        }
        case GAME_MODE_GAME_OVER: {
            const uint8_t idx = static_cast<uint8_t>(gameplayLedAnimStep % GAMEPLAY_GRAPHIC_ZONE_COUNT);
            setGameplayGraphicZoneNoShow(idx, 220, 24, 24);
            break;
        }
        default:
            break;
    }

    showGameplayLedStripBuffer();
}

uint8_t mapSwitchToGameplayLedZone(uint8_t row, uint8_t col) {
    const uint8_t switchIndex = static_cast<uint8_t>((row * CAPTAIN_SWITCH_COLS) + col);
    return static_cast<uint8_t>(switchIndex % GAMEPLAY_LED_SEGMENT_COUNT);
}

void flashWriteEnable() {
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(CAPTAIN_FLASH_CS_PIN, LOW);
    SPI.transfer(0x06);
    digitalWrite(CAPTAIN_FLASH_CS_PIN, HIGH);
    SPI.endTransaction();
}

uint8_t flashReadStatus1() {
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(CAPTAIN_FLASH_CS_PIN, LOW);
    SPI.transfer(0x05);
    const uint8_t status = SPI.transfer(0x00);
    digitalWrite(CAPTAIN_FLASH_CS_PIN, HIGH);
    SPI.endTransaction();
    return status;
}

bool flashWaitBusy(uint32_t timeoutMs) {
    const uint32_t started = millis();
    while ((flashReadStatus1() & 0x01) != 0) {
        if (millis() - started > timeoutMs) {
            return false;
        }
        delay(1);
    }
    return true;
}

void flashReadBytes(uint32_t address, uint8_t* out, size_t len) {
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(CAPTAIN_FLASH_CS_PIN, LOW);
    SPI.transfer(0x03);
    SPI.transfer(static_cast<uint8_t>((address >> 16) & 0xFF));
    SPI.transfer(static_cast<uint8_t>((address >> 8) & 0xFF));
    SPI.transfer(static_cast<uint8_t>(address & 0xFF));
    for (size_t index = 0; index < len; index++) {
        out[index] = SPI.transfer(0x00);
    }
    digitalWrite(CAPTAIN_FLASH_CS_PIN, HIGH);
    SPI.endTransaction();
}

bool flashEraseSector4K(uint32_t address) {
    flashWriteEnable();
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(CAPTAIN_FLASH_CS_PIN, LOW);
    SPI.transfer(0x20);
    SPI.transfer(static_cast<uint8_t>((address >> 16) & 0xFF));
    SPI.transfer(static_cast<uint8_t>((address >> 8) & 0xFF));
    SPI.transfer(static_cast<uint8_t>(address & 0xFF));
    digitalWrite(CAPTAIN_FLASH_CS_PIN, HIGH);
    SPI.endTransaction();
    return flashWaitBusy(6000);
}

bool flashProgramPage(uint32_t address, const uint8_t* data, size_t len) {
    if (len > CAPTAIN_FLASH_PAGE_SIZE) {
        return false;
    }

    flashWriteEnable();
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(CAPTAIN_FLASH_CS_PIN, LOW);
    SPI.transfer(0x02);
    SPI.transfer(static_cast<uint8_t>((address >> 16) & 0xFF));
    SPI.transfer(static_cast<uint8_t>((address >> 8) & 0xFF));
    SPI.transfer(static_cast<uint8_t>(address & 0xFF));
    for (size_t index = 0; index < len; index++) {
        SPI.transfer(data[index]);
    }
    digitalWrite(CAPTAIN_FLASH_CS_PIN, HIGH);
    SPI.endTransaction();
    return flashWaitBusy(1000);
}

void initExternalFlashProbe() {
    pinMode(CAPTAIN_FLASH_CS_PIN, OUTPUT);
    digitalWrite(CAPTAIN_FLASH_CS_PIN, HIGH);

    SPI.begin(CAPTAIN_FLASH_SCK_PIN, CAPTAIN_FLASH_MISO_PIN, CAPTAIN_FLASH_MOSI_PIN, CAPTAIN_FLASH_CS_PIN);
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));

    digitalWrite(CAPTAIN_FLASH_CS_PIN, LOW);
    SPI.transfer(0x9F);
    const uint8_t manufacturer = SPI.transfer(0x00);
    const uint8_t memoryType = SPI.transfer(0x00);
    const uint8_t capacity = SPI.transfer(0x00);
    digitalWrite(CAPTAIN_FLASH_CS_PIN, HIGH);

    SPI.endTransaction();

    Serial.printf("W25Q probe JEDEC: 0x%02X%02X%02X\n", manufacturer, memoryType, capacity);

    const bool winbond = manufacturer == 0xEF;
    const bool w25q128Capacity = capacity == 0x18;
    const bool knownType = (memoryType == 0x40) || (memoryType == 0x70);

    if (winbond && knownType && w25q128Capacity) {
        Serial.println("External flash OK: W25Q128 detected (16MB)");
    } else {
        Serial.println("External flash WARNING: unexpected JEDEC ID (check chip and wiring)");
    }
}

void initI2SAudio() {
    if (CAPTAIN_AUDIO_SWAP_BCLK_LRCK_FOR_TEST) {
        audioBclkPinEffective = CAPTAIN_AUDIO_LRCK_PIN;
        audioLrckPinEffective = CAPTAIN_AUDIO_BCLK_PIN;
    } else {
        audioBclkPinEffective = CAPTAIN_AUDIO_BCLK_PIN;
        audioLrckPinEffective = CAPTAIN_AUDIO_LRCK_PIN;
    }

    i2s_config_t i2sConfig = {};
    i2sConfig.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
    i2sConfig.sample_rate = CAPTAIN_AUDIO_SAMPLE_RATE;
    i2sConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    // Use standard stereo frames so LRCK always toggles during transmission.
    i2sConfig.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2sConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2sConfig.intr_alloc_flags = 0;
    i2sConfig.dma_buf_count = 8;
    i2sConfig.dma_buf_len = 128;
    i2sConfig.use_apll = false;
    i2sConfig.tx_desc_auto_clear = true;
    i2sConfig.fixed_mclk = 0;

    i2s_pin_config_t pinConfig = {};
    pinConfig.mck_io_num = I2S_PIN_NO_CHANGE;
    pinConfig.bck_io_num = audioBclkPinEffective;
    pinConfig.ws_io_num = audioLrckPinEffective;
    pinConfig.data_out_num = CAPTAIN_AUDIO_DIN_PIN;
    pinConfig.data_in_num = I2S_PIN_NO_CHANGE;

    const esp_err_t installResult = i2s_driver_install(CAPTAIN_AUDIO_I2S_PORT, &i2sConfig, 0, nullptr);
    if (installResult != ESP_OK) {
        Serial.printf("I2S init failed: driver install error %d\n", static_cast<int>(installResult));
        return;
    }

    const esp_err_t pinResult = i2s_set_pin(CAPTAIN_AUDIO_I2S_PORT, &pinConfig);
    if (pinResult != ESP_OK) {
        Serial.printf("I2S init failed: pin config error %d\n", static_cast<int>(pinResult));
        return;
    }

    i2sAudioReady = true;
    Serial.printf("I2S audio ready (DIN=%u BCLK=%u LRCK=%u)\n", CAPTAIN_AUDIO_DIN_PIN, audioBclkPinEffective, audioLrckPinEffective);
    if (CAPTAIN_AUDIO_SWAP_BCLK_LRCK_FOR_TEST) {
        Serial.println("I2S pin test mode: BCLK/LRCK swapped in firmware");
    }
}

void playI2STestTone(uint16_t frequencyHz, uint16_t durationMs) {
    if (!i2sAudioReady || frequencyHz == 0 || durationMs == 0) {
        return;
    }

    constexpr size_t chunkFrames = 128;
    int16_t buffer[chunkFrames * 2];
    const uint32_t totalFrames = static_cast<uint32_t>(CAPTAIN_AUDIO_SAMPLE_RATE) * durationMs / 1000;
    const uint32_t halfWaveSamples = max<uint32_t>(1, CAPTAIN_AUDIO_SAMPLE_RATE / (frequencyHz * 2UL));

    uint32_t generatedFrames = 0;
    while (generatedFrames < totalFrames) {
        const size_t frameCount = static_cast<size_t>(min<uint32_t>(chunkFrames, totalFrames - generatedFrames));
        for (size_t index = 0; index < frameCount; index++) {
            const uint32_t position = generatedFrames + index;
            const bool high = ((position / halfWaveSamples) % 2U) == 0U;
            const int16_t sample = high ? static_cast<int16_t>(CAPTAIN_AUDIO_TEST_AMPLITUDE) : static_cast<int16_t>(-CAPTAIN_AUDIO_TEST_AMPLITUDE);

            // Interleave left/right so both channels carry the same waveform.
            buffer[(index * 2) + 0] = sample;
            buffer[(index * 2) + 1] = sample;
        }

        size_t bytesWritten = 0;
        const esp_err_t writeResult = i2s_write(CAPTAIN_AUDIO_I2S_PORT, buffer, frameCount * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
        i2sWriteCalls++;
        i2sBytesWrittenTotal += static_cast<uint32_t>(bytesWritten);
        if (writeResult != ESP_OK || bytesWritten == 0) {
            i2sWriteErrors++;
        }
        generatedFrames += static_cast<uint32_t>(frameCount);
    }
}


void playStartupMelody() {
    if (!i2sAudioReady || !CAPTAIN_AUDIO_STARTUP_TEST_ENABLED) {
        return;
    }

    Serial.println("Audio test: startup pattern begin");

    const uint16_t startupPatternHz[] = {392, 523, 659, 784};
    constexpr uint8_t startupPatternCount = sizeof(startupPatternHz) / sizeof(startupPatternHz[0]);

    for (uint8_t repeat = 0; repeat < CAPTAIN_AUDIO_STARTUP_TEST_REPEATS; repeat++) {
        for (uint8_t i = 0; i < startupPatternCount; i++) {
            playI2STestTone(startupPatternHz[i], CAPTAIN_AUDIO_STARTUP_TONE_MS);
            delay(CAPTAIN_AUDIO_STARTUP_GAP_MS);
        }
    }

    Serial.println("Audio test: startup pattern complete");
}

void initSolenoids() {
    for (uint8_t index = 0; index < SOLENOID_COUNT; index++) {
        const uint8_t pin = CAPTAIN_SOLENOID_PINS[index];
        // Write LOW to the output latch before enabling OUTPUT to avoid drive glitch
        digitalWrite(pin, LOW);
        pinMode(pin, OUTPUT);
    }
}

void fireSolenoid(CaptainSolenoidId solenoidId);  // Forward declaration

void resetS2LimiterForNewBall(uint32_t nowMs) {
    s2FiresThisBall = 0;
    s2FiresInWindow = 0;
    s2WindowStartMs = nowMs;
    s2LastFireMs = 0;
}

bool tryFireS2WithLimits(uint32_t nowMs) {
    if (s2LastFireMs != 0 && (nowMs - s2LastFireMs) < S2_RETRIGGER_COOLDOWN_MS) {
        s2SuppressedCooldown++;
        return false;
    }

    if (s2WindowStartMs == 0 || (nowMs - s2WindowStartMs) >= S2_WINDOW_MS) {
        s2WindowStartMs = nowMs;
        s2FiresInWindow = 0;
    }

    if (s2FiresInWindow >= S2_MAX_FIRES_PER_WINDOW) {
        s2SuppressedWindow++;
        return false;
    }

    if (s2FiresThisBall >= S2_MAX_FIRES_PER_BALL) {
        s2SuppressedBall++;
        return false;
    }

    fireSolenoid(SOLENOID_S2);
    s2LastFireMs = nowMs;
    s2FiresInWindow++;
    s2FiresThisBall++;
    return true;
}

void fireSolenoid(CaptainSolenoidId solenoidId) {
    if (matrixLinkFaulted) {
        return;
    }

    if (solenoidId >= SOLENOID_COUNT) {
        return;
    }

    const uint8_t pin = CAPTAIN_SOLENOID_PINS[solenoidId];
    digitalWrite(pin, HIGH);
    solenoidActive[solenoidId] = true;
    solenoidStartedAtMs[solenoidId] = millis();
}

void updateSolenoidPulses(uint32_t now) {
    for (uint8_t index = 0; index < SOLENOID_COUNT; index++) {
        if (!solenoidActive[index]) {
            continue;
        }

        const uint8_t pin = CAPTAIN_SOLENOID_PINS[index];

        if (now - solenoidStartedAtMs[index] >= CAPTAIN_SOLENOID_PULSE_MS[index]) {
            digitalWrite(pin, LOW);
            solenoidActive[index] = false;
        }
    }
}

bool readDirectInputActive(CaptainDirectInputId inputId) {
    const uint8_t pin = CAPTAIN_DIRECT_INPUT_PINS[inputId];
    const int level = digitalRead(pin);
    if (CAPTAIN_DIRECT_INPUT_ACTIVE_LOW) {
        return level == LOW;
    }
    return level == HIGH;
}

const char* directInputName(CaptainDirectInputId inputId) {
    return CAPTAIN_DIRECT_INPUT_NAMES[static_cast<uint8_t>(inputId)];
}

void onDirectInputPressed(CaptainDirectInputId inputId) {
    Serial.printf("Direct input pressed: %s\n", directInputName(inputId));

    if (inputId == DIRECT_INPUT_START) {
        if (START_BUTTON_SOLENOID_TEST_ENABLED && (currentSW2Mode || !matrixDeviceReady)) {
            if (currentSW2Mode) {
                Serial.println("[TEST] START pressed in Test mode -> firing S2");
            } else {
                Serial.println("[TEST] START pressed while matrix offline -> firing S2");
            }
            fireSolenoid(SOLENOID_S2);
            queueTone(988, 80);
            return;
        }
        startNewGame();
        tiltLatched = false;
        currentSW2Mode = false;  // Reset SW2 (Game/Test) to Game on Start
        Serial.println("[START] New game started, tilt cleared, SW2 reset to Game mode");
        queueTone(880, 80);
    } else if (inputId == DIRECT_INPUT_TILT) {
        tiltLatched = true;
        Serial.println("[TILT] Tilt latch activated");
        queueTone(220, 120);
    } else if (inputId == DIRECT_INPUT_SW1) {
        // SW1 is a maintained slide switch: Easy (false) or Hard (true)
        bool newMode = directInputStable[DIRECT_INPUT_SW1];
        if (newMode != currentSW1Mode) {
            currentSW1Mode = newMode;
            const char* modeStr = currentSW1Mode ? "Hard" : "Easy";
            Serial.printf("[SW1] Mode changed to: %s\n", modeStr);
            queueTone(660, 60);
        }
    } else if (inputId == DIRECT_INPUT_SW2) {
        // SW2 is a maintained slide switch: Game (false) or Test (true)
        bool newMode = directInputStable[DIRECT_INPUT_SW2];
        if (newMode != currentSW2Mode) {
            currentSW2Mode = newMode;
            const char* modeStr = currentSW2Mode ? "Test" : "Game";
            Serial.printf("[SW2] Mode changed to: %s\n", modeStr);
            queueTone(740, 60);
        }
    }
}

void onDirectInputReleased(CaptainDirectInputId inputId) {
    Serial.printf("Direct input released: %s\n", directInputName(inputId));
}

void initDirectInputs() {
    for (uint8_t index = 0; index < DIRECT_INPUT_COUNT; index++) {
        pinMode(CAPTAIN_DIRECT_INPUT_PINS[index], INPUT);
        directInputStable[index] = readDirectInputActive(static_cast<CaptainDirectInputId>(index));
        directInputDebounceCounter[index] = 0;
    }
}

void processDirectInputs(uint32_t now) {
    if (now - lastDirectInputPollMs < DIRECT_INPUT_POLL_MS) {
        return;
    }
    lastDirectInputPollMs = now;

    for (uint8_t index = 0; index < DIRECT_INPUT_COUNT; index++) {
        const CaptainDirectInputId inputId = static_cast<CaptainDirectInputId>(index);
        const bool sampleActive = readDirectInputActive(inputId);

        if (sampleActive == directInputStable[index]) {
            directInputDebounceCounter[index] = 0;
            continue;
        }

        if (directInputDebounceCounter[index] < 255) {
            directInputDebounceCounter[index]++;
        }

        if (directInputDebounceCounter[index] >= DIRECT_INPUT_DEBOUNCE_TICKS) {
            directInputStable[index] = sampleActive;
            directInputDebounceCounter[index] = 0;
            if (sampleActive) {
                onDirectInputPressed(inputId);
            } else {
                onDirectInputReleased(inputId);
            }
        }
    }
}

void setHeadboxLamp(uint16_t& pattern, CaptainHeadboxLampId lampId, bool on) {
    if (lampId >= HEADBOX_LAMP_COUNT) {
        return;
    }

    const uint8_t srBit = CAPTAIN_HEADBOX_LAMP_TO_SR_BIT[lampId];
    const uint16_t mask = static_cast<uint16_t>(1u << srBit);
    if (on) {
        pattern |= mask;
    } else {
        pattern &= static_cast<uint16_t>(~mask);
    }
}

uint16_t composeHeadboxPattern(uint32_t score, bool blink) {
    static_cast<void>(score);
    uint16_t pattern = 0;

    if (gameplayState.mode == GAME_MODE_ATTRACT || gameplayState.mode == GAME_MODE_GAME_OVER) {
        setHeadboxLamp(pattern, HEADBOX_GAME_OVER, gameplayState.mode == GAME_MODE_GAME_OVER || blink);
    } else {
        setHeadboxLamp(pattern, HEADBOX_PLAYER_1, true);
        if (gameplayState.currentBall >= 1 && gameplayState.currentBall <= 5) {
            const CaptainHeadboxLampId ballLamp = static_cast<CaptainHeadboxLampId>(HEADBOX_BALL_1 - (gameplayState.currentBall - 1));
            setHeadboxLamp(pattern, ballLamp, true);
        }
    }

    setHeadboxLamp(pattern, HEADBOX_TILT, tiltLatched);

    return pattern;
}

void addGameplayScore(uint32_t points) {
    gameplayState.score += points;
    displayScore = gameplayState.score;
}

void logGameplayAward(const char* label, uint32_t points, uint16_t bonusAdded) {
    Serial.printf("[GAME] %-16s score=+%lu bonus=+%u total=%lu bonus_total=%u x%u ball=%u\n",
                  label,
                  static_cast<unsigned long>(points),
                  static_cast<unsigned>(bonusAdded),
                  static_cast<unsigned long>(gameplayState.score),
                  static_cast<unsigned>(gameplayState.bonus),
                  static_cast<unsigned>(gameplayState.bonusMultiplier),
                  static_cast<unsigned>(gameplayState.currentBall));
}

void addGameplayBonus(uint16_t points) {
    uint32_t newBonus = static_cast<uint32_t>(gameplayState.bonus) + points;
    gameplayState.bonus = static_cast<uint16_t>(newBonus > GAMEPLAY_BONUS_MAX ? GAMEPLAY_BONUS_MAX : newBonus);
}

void updateGameplayBonusMultiplier() {
    const uint8_t previousMultiplier = gameplayState.bonusMultiplier;
    if (gameplayState.target1Complete && gameplayState.target2Complete && gameplayState.target3Complete) {
        gameplayState.bonusMultiplier = 3;
    } else if (gameplayState.target1Complete && gameplayState.target2Complete) {
        gameplayState.bonusMultiplier = 2;
    } else {
        gameplayState.bonusMultiplier = 1;
    }

    if (gameplayState.bonusMultiplier != previousMultiplier) {
        Serial.printf("[GAME] Bonus multiplier -> %ux\n", static_cast<unsigned>(gameplayState.bonusMultiplier));
    }
}

void updateGameplayLaneCompletion() {
    const bool wasLit = gameplayState.samePlayerLit;
    gameplayState.samePlayerLit = gameplayState.laneAComplete &&
                                  gameplayState.laneBComplete &&
                                  gameplayState.laneCComplete &&
                                  gameplayState.laneDComplete;

    if (!wasLit && gameplayState.samePlayerLit) {
        Serial.println("[GAME] Lane set complete -> Same Player / return-lane feature lit");
    }
}

void resetGameplayStateForNewBall() {
    gameplayState.bonus = GAMEPLAY_BONUS_START;
    gameplayState.bonusMultiplier = 1;
    gameplayState.ballInPlay = false;
    gameplayState.laneAComplete = false;
    gameplayState.laneBComplete = false;
    gameplayState.laneCComplete = false;
    gameplayState.laneDComplete = false;
    gameplayState.target1Complete = false;
    gameplayState.target2Complete = false;
    gameplayState.target3Complete = false;
    gameplayState.samePlayerLit = false;
    gameplayState.lastBonusStepMs = 0;
}

void startNewGame() {
    gameplayState.mode = GAME_MODE_SERVE_BALL;
    gameplayState.score = 0;
    gameplayState.currentBall = 1;
    resetGameplayStateForNewBall();
    displayScore = 0;
    Serial.println("[GAME] New single-player 5-ball game");
}

void startBonusCountdown(uint32_t nowMs) {
    gameplayState.ballInPlay = false;
    gameplayState.mode = GAME_MODE_BONUS_COUNTDOWN;
    gameplayState.lastBonusStepMs = nowMs;
    Serial.printf("[GAME] Bonus countdown start: bonus=%u x%u\n",
                  static_cast<unsigned>(gameplayState.bonus),
                  static_cast<unsigned>(gameplayState.bonusMultiplier));
}

void finishCurrentBall() {
    if (gameplayState.currentBall >= 5) {
        gameplayState.mode = GAME_MODE_GAME_OVER;
        gameplayState.ballInPlay = false;
        Serial.printf("[GAME] Game over: final score=%lu\n", static_cast<unsigned long>(gameplayState.score));
        return;
    }

    Serial.printf("[GAME] Ball %u complete -> advancing to ball %u\n",
                  static_cast<unsigned>(gameplayState.currentBall),
                  static_cast<unsigned>(gameplayState.currentBall + 1));
    gameplayState.currentBall++;
    resetGameplayStateForNewBall();
    gameplayState.mode = GAME_MODE_SERVE_BALL;
}

void updateGameplayState(uint32_t nowMs) {
    if (currentSW2Mode) {
        return;
    }

    switch (gameplayState.mode) {
        case GAME_MODE_SERVE_BALL:
            Serial.printf("[GAME] Serving ball %u\n", static_cast<unsigned>(gameplayState.currentBall));
            resetS2LimiterForNewBall(nowMs);
            tryFireS2WithLimits(nowMs);
            triggerGameplayLedPulse(0, 0, 24, 24, nowMs);
            gameplayState.ballInPlay = true;
            gameplayState.mode = GAME_MODE_BALL_IN_PLAY;
            break;
        case GAME_MODE_BONUS_COUNTDOWN:
            if (gameplayState.bonus == 0) {
                finishCurrentBall();
                break;
            }
            if ((nowMs - gameplayState.lastBonusStepMs) < BONUS_COUNTDOWN_STEP_MS) {
                break;
            }
            gameplayState.lastBonusStepMs = nowMs;
            if (gameplayState.bonus >= 1000) {
                addGameplayScore(1000u * gameplayState.bonusMultiplier);
                gameplayState.bonus = static_cast<uint16_t>(gameplayState.bonus - 1000);
                Serial.printf("[GAME] Bonus step: +%lu remaining=%u x%u total=%lu\n",
                              static_cast<unsigned long>(1000u * gameplayState.bonusMultiplier),
                              static_cast<unsigned>(gameplayState.bonus),
                              static_cast<unsigned>(gameplayState.bonusMultiplier),
                              static_cast<unsigned long>(gameplayState.score));
            } else {
                addGameplayScore(static_cast<uint32_t>(gameplayState.bonus) * gameplayState.bonusMultiplier);
                Serial.printf("[GAME] Bonus final step: +%lu\n",
                              static_cast<unsigned long>(static_cast<uint32_t>(gameplayState.bonus) * gameplayState.bonusMultiplier));
                gameplayState.bonus = 0;
            }
            if (gameplayState.bonus == 0) {
                finishCurrentBall();
            }
            break;
        case GAME_MODE_GAME_OVER:
            gameplayState.ballInPlay = false;
            break;
        case GAME_MODE_ATTRACT:
        case GAME_MODE_BALL_IN_PLAY:
        default:
            break;
    }
}

void handleGameplaySwitchHit(uint8_t row, uint8_t col, uint32_t nowMs) {
    if (row == S20_OUTHOLE_SWITCH_ROW && col == S20_OUTHOLE_SWITCH_COL) {
        if (gameplayState.mode == GAME_MODE_BALL_IN_PLAY && gameplayState.ballInPlay) {
            Serial.println("[GAME] Ball drained -> bonus countdown");
            triggerGameplayLedPulse(static_cast<uint8_t>(GAMEPLAY_LED_SEGMENT_COUNT - 1), 24, 0, 0, nowMs);
            startBonusCountdown(nowMs);
        }
        return;
    }

    if (gameplayState.mode != GAME_MODE_BALL_IN_PLAY || !gameplayState.ballInPlay) {
        return;
    }

    const uint8_t ledZone = mapSwitchToGameplayLedZone(row, col);
    triggerGameplayLedPulse(ledZone, 0, 32, 0, nowMs);

    switch (row) {
        case 0:
            if (col == 2) {
                addGameplayScore(100);
                logGameplayAward("Spinner R", 100, 0);
            } else if (col == 3) {
                addGameplayScore(500);
                addGameplayBonus(500);
                logGameplayAward("Return Lane R", 500, 500);
            }
            break;
        case 1:
            if (col == 0) {
                addGameplayScore(1000);
                addGameplayBonus(1000);
                gameplayState.laneAComplete = true;
                updateGameplayLaneCompletion();
                logGameplayAward("Lane A", 1000, 1000);
            } else if (col == 2) {
                addGameplayScore(50);
                addGameplayBonus(2000);
                gameplayState.target1Complete = true;
                updateGameplayBonusMultiplier();
                logGameplayAward("Target 1", 50, 2000);
            } else if (col == 3) {
                addGameplayScore(100);
                if (MATRIX_SWITCH_SOLENOIDS_ENABLED) {
                    fireSolenoid(SOLENOID_S3);
                }
                logGameplayAward("Slingshot L", 100, 0);
            }
            break;
        case 2:
            if (col == 0) {
                addGameplayScore(1000);
                addGameplayBonus(1000);
                gameplayState.laneBComplete = true;
                updateGameplayLaneCompletion();
                logGameplayAward("Lane B", 1000, 1000);
            } else if (col == 1) {
                addGameplayScore(100);
                if (MATRIX_SWITCH_SOLENOIDS_ENABLED) {
                    fireSolenoid(SOLENOID_S5);
                }
                logGameplayAward("Bumper L", 100, 0);
            } else if (col == 2) {
                addGameplayScore(50);
                addGameplayBonus(50);
                logGameplayAward("Side Switch 1", 50, 50);
            } else if (col == 3) {
                addGameplayScore(500);
                addGameplayBonus(500);
                logGameplayAward("Return Lane L", 500, 500);
            }
            break;
        case 3:
            if (col == 0) {
                addGameplayScore(1000);
                addGameplayBonus(1000);
                gameplayState.laneCComplete = true;
                updateGameplayLaneCompletion();
                logGameplayAward("Lane C", 1000, 1000);
            } else if (col == 1) {
                addGameplayScore(100);
                if (MATRIX_SWITCH_SOLENOIDS_ENABLED) {
                    fireSolenoid(SOLENOID_S6);
                }
                logGameplayAward("Bumper R", 100, 0);
            } else if (col == 2) {
                addGameplayScore(100);
                logGameplayAward("Spinner L", 100, 0);
            } else if (col == 3) {
                addGameplayScore(500);
                addGameplayBonus(500);
                logGameplayAward("Bonus Lane L", 500, 500);
            }
            break;
        case 4:
            if (col == 0) {
                addGameplayScore(1000);
                addGameplayBonus(1000);
                gameplayState.laneDComplete = true;
                updateGameplayLaneCompletion();
                logGameplayAward("Lane D", 1000, 1000);
            } else if (col == 2) {
                addGameplayScore(50);
                addGameplayBonus(2000);
                gameplayState.target2Complete = true;
                updateGameplayBonusMultiplier();
                logGameplayAward("Target 2", 50, 2000);
            } else if (col == 3) {
                addGameplayScore(100);
                if (MATRIX_SWITCH_SOLENOIDS_ENABLED) {
                    fireSolenoid(SOLENOID_S4);
                }
                logGameplayAward("Slingshot R", 100, 0);
            }
            break;
        case 5:
            if (col == 0) {
                addGameplayScore(50);
                addGameplayBonus(2000);
                gameplayState.target3Complete = true;
                updateGameplayBonusMultiplier();
                logGameplayAward("Target 3", 50, 2000);
            } else if (col == 2) {
                addGameplayScore(50);
                addGameplayBonus(50);
                logGameplayAward("Side Switch 2", 50, 50);
            } else if (col == 3) {
                addGameplayScore(500);
                addGameplayBonus(500);
                logGameplayAward("Bonus Lane R", 500, 500);
            }
            break;
        default:
            break;
    }
}

void handleSwitchEdges(const uint8_t* switchBits) {
    const uint32_t nowMs = millis();
    if (matrixSwitchLogWindowStartMs == 0 || (nowMs - matrixSwitchLogWindowStartMs) >= MATRIX_SWITCH_LOG_REPORT_MS) {
        matrixSwitchLogWindowStartMs = nowMs;
        matrixSwitchEdgesThisWindow = 0;
        matrixSwitchLoggedThisWindow = 0;
        matrixSwitchLogSuppressedDebounce = 0;
        matrixSwitchLogSuppressedRate = 0;
    }

    uint8_t risingEdgesThisPoll = 0;
    for (uint8_t row = 0; row < CAPTAIN_SWITCH_ROWS; row++) {
        for (uint8_t col = 0; col < CAPTAIN_SWITCH_COLS; col++) {
            const size_t bit = captainSwitchBitIndex(row, col);
            const bool previous = captainGetBit(previousSwitchBits, bit);
            const bool current = captainGetBit(switchBits, bit);
            if (!previous && current) {
                risingEdgesThisPoll++;
            }
        }
    }

    // Ignore impossible bursts in normal mode; mapping mode must still log raw edges.
    if (!MATRIX_SWITCH_MAPPING_MODE && risingEdgesThisPoll > MATRIX_MAX_RISING_EDGES_PER_POLL) {
        matrixSwitchSuppressedBurst++;
        logMatrixTrace("sup_burst", CAPTAIN_MATRIX_REG_SWITCH_BASE, switchBits, CAPTAIN_SWITCH_BYTES);
        memcpy(previousSwitchBits, switchBits, sizeof(previousSwitchBits));
        return;
    }

    for (uint8_t row = 0; row < CAPTAIN_SWITCH_ROWS; row++) {
        for (uint8_t col = 0; col < CAPTAIN_SWITCH_COLS; col++) {
            const size_t bit = captainSwitchBitIndex(row, col);
            const bool previous = captainGetBit(previousSwitchBits, bit);
            const bool current = captainGetBit(switchBits, bit);
            if (!previous && current) {
                // Rows 6-7 are placeholders in the current switch-name table, not real playfield inputs.
                if (MATRIX_SWITCH_MAPPING_MODE && row >= 6) {
                    continue;
                }
                if (row == S20_OUTHOLE_SWITCH_ROW && col == S20_OUTHOLE_SWITCH_COL) {
                    if (s20OutholeLastRiseMs != 0 && (nowMs - s20OutholeLastRiseMs) < S20_OUTHOLE_RETRIGGER_COOLDOWN_MS) {
                        s20OutholeSuppressedSticky++;
                        continue;
                    }
                    s20OutholeLastRiseMs = nowMs;
                }

                matrixSwitchEdgesThisWindow++;

                if (MATRIX_SWITCH_MAPPING_MODE) {
                    if ((nowMs - lastMatrixSwitchEdgeLogMs) < MATRIX_SWITCH_LOG_DEBOUNCE_MS) {
                        matrixSwitchLogSuppressedDebounce++;
                        continue;
                    }
                    if (matrixSwitchLoggedThisWindow >= MATRIX_SWITCH_LOG_MAX_PER_REPORT) {
                        matrixSwitchLogSuppressedRate++;
                        continue;
                    }

                    lastMatrixSwitchEdgeLogMs = nowMs;
                    matrixSwitchLoggedThisWindow++;
                    Serial.printf("[MAP] rising row=%u col=%u bit=%u name=%s sw=[%02X %02X %02X %02X]\n",
                                  static_cast<unsigned>(row),
                                  static_cast<unsigned>(col),
                                  static_cast<unsigned>(bit),
                                  captainSwitchName(row, col),
                                  static_cast<unsigned>(switchBits[0]),
                                  static_cast<unsigned>(switchBits[1]),
                                  static_cast<unsigned>(switchBits[2]),
                                  static_cast<unsigned>(switchBits[3]));
                    continue;
                }

                handleGameplaySwitchHit(row, col, nowMs);
            }
        }
    }

    memcpy(previousSwitchBits, switchBits, sizeof(previousSwitchBits));
}

bool readMatrixSwitches(uint8_t* switchBits) {
    if (!matrixReadRegisters(CAPTAIN_MATRIX_REG_SWITCH_BASE, switchBits, CAPTAIN_SWITCH_BYTES)) {
        return false;
    }

    bool matchesLampFrame = true;
    for (uint8_t index = 0; index < CAPTAIN_SWITCH_BYTES; index++) {
        if (switchBits[index] != lastMatrixLampRows[index]) {
            matchesLampFrame = false;
            break;
        }
    }
    if (matchesLampFrame) {
        bool anyNonZero = false;
        for (uint8_t index = 0; index < CAPTAIN_SWITCH_BYTES; index++) {
            if (switchBits[index] != 0) {
                anyNonZero = true;
                break;
            }
        }
        if (anyNonZero) {
            logMatrixTrace("sup_lamp", CAPTAIN_MATRIX_REG_SWITCH_BASE, switchBits, CAPTAIN_SWITCH_BYTES);
            memset(switchBits, 0, CAPTAIN_SWITCH_BYTES);
            matrixSwitchSuppressedLampEcho++;
        }
    }

    for (uint8_t row = 6; row < CAPTAIN_SWITCH_ROWS; row++) {
        for (uint8_t col = 0; col < CAPTAIN_SWITCH_COLS; col++) {
            captainSetBit(switchBits, captainSwitchBitIndex(row, col), false);
        }
    }

    return true;
}

bool readMatrixDiagnostics(uint8_t* diagBytes) {
    return matrixReadRegisters(CAPTAIN_MATRIX_REG_DIAG_BASE, diagBytes, CAPTAIN_MATRIX_REG_DIAG_END - CAPTAIN_MATRIX_REG_DIAG_BASE + 1);
}

void normalizeMatrixSwitchBits(const uint8_t* rawSwitchBits, uint8_t* normalizedSwitchBits) {
    memcpy(normalizedSwitchBits, rawSwitchBits, CAPTAIN_SWITCH_BYTES);

    if (!MATRIX_SWITCH_BITS_ACTIVE_HIGH) {
        for (uint8_t index = 0; index < CAPTAIN_SWITCH_BYTES; index++) {
            normalizedSwitchBits[index] = static_cast<uint8_t>(~normalizedSwitchBits[index]);
        }
    }

    for (uint8_t row = 6; row < CAPTAIN_SWITCH_ROWS; row++) {
        for (uint8_t col = 0; col < CAPTAIN_SWITCH_COLS; col++) {
            captainSetBit(normalizedSwitchBits, captainSwitchBitIndex(row, col), false);
        }
    }
}

void filterMatrixSwitchBits(const uint8_t* rawSwitchBits, uint8_t* filteredSwitchBits) {
    for (uint8_t row = 0; row < CAPTAIN_SWITCH_ROWS; row++) {
        for (uint8_t col = 0; col < CAPTAIN_SWITCH_COLS; col++) {
            const size_t bitIndex = captainSwitchBitIndex(row, col);
            const bool rawClosed = captainGetBit(rawSwitchBits, bitIndex);
            const bool stableClosed = captainGetBit(stableMatrixSwitchBits, bitIndex);

            if (rawClosed == stableClosed) {
                matrixSwitchConfirmTicks[bitIndex] = 0;
                captainSetBit(candidateMatrixSwitchBits, bitIndex, stableClosed);
                continue;
            }

            const bool candidateClosed = captainGetBit(candidateMatrixSwitchBits, bitIndex);
            if (rawClosed != candidateClosed) {
                captainSetBit(candidateMatrixSwitchBits, bitIndex, rawClosed);
                if (rawClosed) {
                    captainSetBit(stableMatrixSwitchBits, bitIndex, true);
                    matrixSwitchConfirmTicks[bitIndex] = 0;
                    continue;
                }
                matrixSwitchConfirmTicks[bitIndex] = 1;
                continue;
            }

            if (matrixSwitchConfirmTicks[bitIndex] < 255) {
                matrixSwitchConfirmTicks[bitIndex]++;
            }

            if (matrixSwitchConfirmTicks[bitIndex] >= MATRIX_SWITCH_CONFIRM_POLLS) {
                captainSetBit(stableMatrixSwitchBits, bitIndex, rawClosed);
                matrixSwitchConfirmTicks[bitIndex] = 0;
            }
        }
    }

    memcpy(filteredSwitchBits, stableMatrixSwitchBits, CAPTAIN_SWITCH_BYTES);
}

// Matrix lamp attract: cycles through named lamp groups with a dark gap between each.
// Groups (each CAPTAIN_MATRIX_ATTRACT_PHASE_MS long, separated by CAPTAIN_MATRIX_ATTRACT_BLANK_MS):
//   0: lanes A-D   1: targets   2: low bonus 1K-5K   3: high bonus 6K-10K
//   4: multipliers  5: return lanes + same-player   6: ball indicators   7: ALL lamps
void computeMatrixAttractFrame(uint32_t now, uint8_t* lampRows) {
    constexpr uint32_t slotMs   = CAPTAIN_MATRIX_ATTRACT_PHASE_MS + CAPTAIN_MATRIX_ATTRACT_BLANK_MS;
    constexpr uint8_t  NUM_PHASES = 8;
    constexpr uint32_t cycleMs  = slotMs * NUM_PHASES;

    const uint32_t t       = now % cycleMs;
    const uint8_t  phase   = static_cast<uint8_t>(t / slotMs);
    const uint32_t inPhase = t % slotMs;

    if (inPhase >= CAPTAIN_MATRIX_ATTRACT_PHASE_MS) {
        return;  // dark gap — caller already zeroed lampRows
    }

    switch (phase) {
        case 0: // Lane lamps: A, B, C, D
            lampRows[1] |= captainMatrixLampRowMask(1);  // L1 A
            lampRows[2] |= captainMatrixLampRowMask(1);  // L2 B
            lampRows[3] |= captainMatrixLampRowMask(1);  // L3 C
            lampRows[4] |= captainMatrixLampRowMask(1);  // L4 D
            break;
        case 1: // Target lamps: L5 T3, L6 T1, L9 T2
            lampRows[5] |= captainMatrixLampRowMask(1);  // L5 Target 3
            lampRows[1] |= captainMatrixLampRowMask(3);  // L6 Target 1
            lampRows[4] |= captainMatrixLampRowMask(3);  // L9 Target 2
            break;
        case 2: // Low bonus: 1K-5K
            lampRows[1] |= captainMatrixLampRowMask(4);  // L19 1K
            lampRows[3] |= captainMatrixLampRowMask(4);  // L18 2K
            lampRows[4] |= captainMatrixLampRowMask(2);  // L17 3K
            lampRows[5] |= captainMatrixLampRowMask(2);  // L16 4K
            lampRows[2] |= captainMatrixLampRowMask(2);  // L15 5K
            break;
        case 3: // High bonus: 6K-10K
            lampRows[1] |= captainMatrixLampRowMask(2);  // L14 6K
            lampRows[3] |= captainMatrixLampRowMask(2);  // L13 7K
            lampRows[0] |= captainMatrixLampRowMask(2);  // L12 8K
            lampRows[0] |= captainMatrixLampRowMask(3);  // L11 9K
            lampRows[3] |= captainMatrixLampRowMask(3);  // L10 10K
            break;
        case 4: // Multiplier lamps: Double Bonus, Triple Bonus
            lampRows[2] |= captainMatrixLampRowMask(3);  // L7 Double Bonus
            lampRows[5] |= captainMatrixLampRowMask(3);  // L8 Triple Bonus
            break;
        case 5: // Return lanes + Same Player
            lampRows[0] |= captainMatrixLampRowMask(4);  // L22 Return Lane R
            lampRows[2] |= captainMatrixLampRowMask(4);  // L21 Return Lane L
            lampRows[4] |= captainMatrixLampRowMask(4);  // L20 Same Player
            break;
        case 6: // Ball indicators: B1-B5
            lampRows[6] |= captainMatrixLampRowMask(1);  // B1
            lampRows[6] |= captainMatrixLampRowMask(2);  // B2
            lampRows[6] |= captainMatrixLampRowMask(3);  // B3
            lampRows[6] |= captainMatrixLampRowMask(4);  // B4
            lampRows[5] |= captainMatrixLampRowMask(4);  // B5
            break;
        case 7: // ALL lamps — big flash
            lampRows[0] = 0x1F;
            for (uint8_t r = 1; r < CAPTAIN_LAMP_ROWS; r++) {
                lampRows[r] = 0x1E;  // cols 1-4 (col 0 unused on rows 1-7)
            }
            break;
    }
}

void buildGameplayLampFrame(uint8_t* lampRows) {
    // MVP is single-player only, so keep Player 1 lit during live gameplay.
    lampRows[7] |= captainMatrixLampRowMask(1);  // P1 Player 1

    switch (gameplayState.currentBall) {
        case 1:
            lampRows[6] |= captainMatrixLampRowMask(1);  // B1 Ball 1
            break;
        case 2:
            lampRows[6] |= captainMatrixLampRowMask(2);  // B2 Ball 2
            break;
        case 3:
            lampRows[6] |= captainMatrixLampRowMask(3);  // B3 Ball 3
            break;
        case 4:
            lampRows[6] |= captainMatrixLampRowMask(4);  // B4 Ball 4
            break;
        case 5:
            lampRows[5] |= captainMatrixLampRowMask(4);  // B5 Ball 5
            break;
        default:
            break;
    }

    if (gameplayState.laneAComplete) {
        lampRows[1] |= captainMatrixLampRowMask(1);
    }
    if (gameplayState.laneBComplete) {
        lampRows[2] |= captainMatrixLampRowMask(1);
    }
    if (gameplayState.laneCComplete) {
        lampRows[3] |= captainMatrixLampRowMask(1);
    }
    if (gameplayState.laneDComplete) {
        lampRows[4] |= captainMatrixLampRowMask(1);
    }

    if (gameplayState.target1Complete) {
        lampRows[1] |= captainMatrixLampRowMask(3);
    }
    if (gameplayState.target2Complete) {
        lampRows[4] |= captainMatrixLampRowMask(3);
    }
    if (gameplayState.target3Complete) {
        lampRows[5] |= captainMatrixLampRowMask(1);
    }

    if (gameplayState.bonusMultiplier >= 2) {
        lampRows[2] |= captainMatrixLampRowMask(3);
    }
    if (gameplayState.bonusMultiplier >= 3) {
        lampRows[5] |= captainMatrixLampRowMask(3);
    }

    if (gameplayState.samePlayerLit) {
        lampRows[4] |= captainMatrixLampRowMask(4);
        if (((millis() / 250u) % 2u) == 0u) {
            lampRows[0] |= captainMatrixLampRowMask(4);  // L22 Return Lane R
            lampRows[2] |= captainMatrixLampRowMask(4);  // L21 Return Lane L
        }
    }

    const uint8_t bonusThousands = static_cast<uint8_t>(gameplayState.bonus / 1000u);
    if (bonusThousands >= 1) lampRows[1] |= captainMatrixLampRowMask(4);
    if (bonusThousands >= 2) lampRows[3] |= captainMatrixLampRowMask(4);
    if (bonusThousands >= 3) lampRows[4] |= captainMatrixLampRowMask(2);
    if (bonusThousands >= 4) lampRows[5] |= captainMatrixLampRowMask(2);
    if (bonusThousands >= 5) lampRows[2] |= captainMatrixLampRowMask(2);
    if (bonusThousands >= 6) lampRows[1] |= captainMatrixLampRowMask(2);
    if (bonusThousands >= 7) lampRows[3] |= captainMatrixLampRowMask(2);
    if (bonusThousands >= 8) lampRows[0] |= captainMatrixLampRowMask(2);
    if (bonusThousands >= 9) lampRows[0] |= captainMatrixLampRowMask(3);
    if (bonusThousands >= 10) lampRows[3] |= captainMatrixLampRowMask(3);
}

bool writeMatrixCommand(uint32_t now) {
    uint8_t lampRows[CAPTAIN_LAMP_ROWS] = {};

    if (MATRIX_SWITCH_MAPPING_MODE) {
        memset(lampRows, 0, sizeof(lampRows));
    } else if (gameplayState.mode == GAME_MODE_ATTRACT) {
        if (CAPTAIN_MATRIX_ATTRACT_ENABLED) {
            computeMatrixAttractFrame(now, lampRows);
        }
    } else {
        buildGameplayLampFrame(lampRows);
    }

    const bool lampFrameChanged = !matrixLampFramePrimed ||
                                  memcmp(lampRows, lastMatrixLampRows, sizeof(lampRows)) != 0;
    const bool keepaliveDue = !matrixLampFramePrimed ||
                              (now - lastMatrixLampWriteMs) >= MATRIX_LAMP_KEEPALIVE_MS;
    if (!lampFrameChanged && !keepaliveDue) {
        return true;
    }

    // Append XOR checksum so the matrix can discard I2C-corrupted frames.
    uint8_t lampFrame[CAPTAIN_LAMP_ROWS + 1] = {};
    uint8_t xorAcc = 0;
    for (uint8_t i = 0; i < CAPTAIN_LAMP_ROWS; i++) {
        lampFrame[i] = lampRows[i];
        xorAcc ^= lampRows[i];
    }
    lampFrame[CAPTAIN_LAMP_ROWS] = xorAcc;

    const bool writeOk = matrixWriteRegisters(CAPTAIN_MATRIX_REG_LAMP_BASE, lampFrame, sizeof(lampFrame));
    if (!writeOk) {
        matrixDeviceReady = false;
        return false;
    }

    memcpy(lastMatrixLampRows, lampRows, sizeof(lastMatrixLampRows));
    matrixLampFramePrimed = true;
    lastMatrixLampWriteMs = now;

    return true;
}

void updateHeadboxLamps(uint16_t pattern) {
    if (CAPTAIN_HEADBOX_595_DATA_PIN < 0 || CAPTAIN_HEADBOX_595_CLOCK_PIN < 0 || CAPTAIN_HEADBOX_595_LATCH_PIN < 0) {
        return;
    }

    const uint8_t bitOrder = CAPTAIN_HEADBOX_595_MSB_FIRST ? MSBFIRST : LSBFIRST;
    digitalWrite(CAPTAIN_HEADBOX_595_LATCH_PIN, LOW);
    shiftOut(static_cast<uint8_t>(CAPTAIN_HEADBOX_595_DATA_PIN), static_cast<uint8_t>(CAPTAIN_HEADBOX_595_CLOCK_PIN), bitOrder, static_cast<uint8_t>((pattern >> 8) & 0xFF));
    shiftOut(static_cast<uint8_t>(CAPTAIN_HEADBOX_595_DATA_PIN), static_cast<uint8_t>(CAPTAIN_HEADBOX_595_CLOCK_PIN), bitOrder, static_cast<uint8_t>(pattern & 0xFF));
    digitalWrite(CAPTAIN_HEADBOX_595_LATCH_PIN, HIGH);
}

uint16_t updateHeadboxAttractLoop(uint32_t now) {
    const CaptainHeadboxLampId chaseOrder[] = {
        HEADBOX_PLAYER_1,
        HEADBOX_PLAYER_2,
        HEADBOX_PLAYER_3,
        HEADBOX_PLAYER_4,
        HEADBOX_TILT,
        HEADBOX_BALL_5,
        HEADBOX_BALL_4,
        HEADBOX_BALL_3,
        HEADBOX_BALL_2,
        HEADBOX_BALL_1
    };

    const uint8_t chaseSteps = static_cast<uint8_t>(sizeof(chaseOrder) / sizeof(chaseOrder[0]));
    const uint8_t laps = max<uint8_t>(1, CAPTAIN_HEADBOX_ATTRACT_CHASE_LAPS);
    const uint32_t totalSlots = static_cast<uint32_t>(chaseSteps) * laps;
    const uint32_t slotMs = max<uint32_t>(CAPTAIN_HEADBOX_ATTRACT_MIN_STEP_MS, CAPTAIN_HEADBOX_ATTRACT_LOOP_PERIOD_MS / totalSlots);
    const uint32_t loopMs = slotMs * totalSlots;

    const uint32_t phaseMs = now % loopMs;
    const uint32_t slotIndex = phaseMs / slotMs;
    const uint32_t inSlotMs = phaseMs % slotMs;

    // 50% duty pulse inside each LED slot.
    if (inSlotMs >= (slotMs / 2u)) {
        return 0;
    }

    const uint8_t chaseIndex = static_cast<uint8_t>(slotIndex % chaseSteps);
    uint16_t pattern = 0;
    setHeadboxLamp(pattern, chaseOrder[chaseIndex], true);
    return pattern;
}
}

void setup() {
    Serial.begin(115200);
    initSolenoids();
    delay(50);
    Serial.println("CAPTAIN_V2 setup start");
    Serial.printf("Build: git=%s state=%s built=%s %s\n",
                  CAPTAIN_BUILD_GIT_HASH,
                  CAPTAIN_BUILD_GIT_STATE,
                  __DATE__,
                  __TIME__);
    Serial.printf("Attract config: enabled=%u period=%lu minStep=%lu laps=%u\n",
                  CAPTAIN_HEADBOX_ATTRACT_LOOP ? 1u : 0u,
                  static_cast<unsigned long>(CAPTAIN_HEADBOX_ATTRACT_LOOP_PERIOD_MS),
                  static_cast<unsigned long>(CAPTAIN_HEADBOX_ATTRACT_MIN_STEP_MS),
                  static_cast<unsigned>(CAPTAIN_HEADBOX_ATTRACT_CHASE_LAPS));
    Wire.begin(CAPTAIN_I2C_SDA_PIN, CAPTAIN_I2C_SCL_PIN, CAPTAIN_I2C_FREQUENCY_HZ);

    if (CAPTAIN_HEADBOX_595_DATA_PIN >= 0 && CAPTAIN_HEADBOX_595_CLOCK_PIN >= 0 && CAPTAIN_HEADBOX_595_LATCH_PIN >= 0) {
        pinMode(CAPTAIN_HEADBOX_595_DATA_PIN, OUTPUT);
        pinMode(CAPTAIN_HEADBOX_595_CLOCK_PIN, OUTPUT);
        pinMode(CAPTAIN_HEADBOX_595_LATCH_PIN, OUTPUT);
    }

    resetS2LimiterForNewBall(millis());
    initDirectInputs();
    initHeartbeat();
    initI2SAudio();
    playStartupMelody();
    initExternalFlashProbe();
    initWifiAndOta();
    initMatrixDevice();

    initDisplay();
    displayStartupTest();
    displayGoodMessage(2000);
    clearDisplay();
    if (CAPTAIN_HEADBOX_ATTRACT_LOOP) {
        Serial.println("Headbox attract loop enabled: pulse chase");
        constexpr uint32_t chaseSteps = 10;
        const uint32_t totalSlots = chaseSteps * max<uint8_t>(1, CAPTAIN_HEADBOX_ATTRACT_CHASE_LAPS);
        const uint32_t slotMs = max<uint32_t>(CAPTAIN_HEADBOX_ATTRACT_MIN_STEP_MS, CAPTAIN_HEADBOX_ATTRACT_LOOP_PERIOD_MS / totalSlots);
        const uint32_t sweepMs = slotMs * totalSlots;
        Serial.printf("Attract timing: slot=%lums on=%lums off=%lums sweep=%lums laps=%u\n",
                      static_cast<unsigned long>(slotMs),
                      static_cast<unsigned long>(slotMs / 2u),
                      static_cast<unsigned long>(slotMs / 2u),
                      static_cast<unsigned long>(sweepMs),
                      static_cast<unsigned>(CAPTAIN_HEADBOX_ATTRACT_CHASE_LAPS));
    }
    Serial.printf("I2C bus: SDA=%u SCL=%u display=0x%02X matrix=0x%02X\n", CAPTAIN_I2C_SDA_PIN, CAPTAIN_I2C_SCL_PIN, CAPTAIN_DISPLAY_I2C_ADDRESS, CAPTAIN_MATRIX_I2C_ADDRESS);
    Serial.printf("External flash: W25Q128 SPI CS=%u MOSI=%u MISO=%u SCK=%u size=%lu bytes\n", CAPTAIN_FLASH_CS_PIN, CAPTAIN_FLASH_MOSI_PIN, CAPTAIN_FLASH_MISO_PIN, CAPTAIN_FLASH_SCK_PIN, static_cast<unsigned long>(CAPTAIN_FLASH_SIZE_BYTES));
    Serial.println("Captain v2 control board started");

    if (i2sAudioReady) {
        audioToneQueue = xQueueCreate(AUDIO_QUEUE_LENGTH, sizeof(AudioToneEvent));
        if (audioToneQueue == nullptr) {
            Serial.println("Audio queue create failed; tones disabled at runtime");
        }
    }

    if (audioToneQueue != nullptr) {
        xTaskCreatePinnedToCore(
            [](void*) {
                AudioToneEvent event = {};
                for (;;) {
                    if (xQueueReceive(audioToneQueue, &event, portMAX_DELAY) == pdPASS) {
                        playI2STestTone(event.frequencyHz, event.durationMs);
                    }
                }
            },
            "captain_audio",
            AUDIO_TASK_STACK_BYTES,
            nullptr,
            AUDIO_TASK_PRIORITY,
            &audioTaskHandle,
            AUDIO_TASK_CORE);
    }

    xTaskCreatePinnedToCore(
        [](void*) {
            static uint32_t lastPoll = 0;
            for (;;) {
                const uint32_t now = millis();
                updateHeartbeat(now);
                updateSolenoidPulses(now);
                processDirectInputs(now);

                if (otaReady) {
                    ArduinoOTA.handle();
                }

                if (otaInProgress) {
                    updateOtaVisual(now);
                    vTaskDelay(1);
                    continue;
                }

                updateGameplayState(now);
                updateGameplayLedPattern(now);
                updateGameplayLedPulse(now);

                if (now - lastPoll >= POLL_MS) {
                    lastPoll = now;
                    if (CAPTAIN_HEADBOX_ATTRACT_LOOP) {
                        headboxPattern = updateHeadboxAttractLoop(now);
                        updateHeadboxLamps(headboxPattern);
                    } else {
                        if (!matrixDeviceReady && (now - lastMatrixInitAttemptMs) >= MATRIX_INIT_RETRY_MS) {
                            initMatrixDevice();
                        }

                        if (!matrixDeviceReady) {
                            const bool blink = ((now / 350) % 2) != 0;
                            headboxPattern = composeHeadboxPattern(displayScore, blink);
                            updateHeadboxLamps(headboxPattern);
                            if (!matrixSwitch0Seen) {
                                lastMatrixSwitch0 = 0;
                                lastMatrixSwitch1 = 0;
                                lastMatrixSwitch2 = 0;
                                lastMatrixSwitch3 = 0;
                            }
                            logMatrixLinkSummary(now);
                            continue;
                        }

                        const bool writeOk = writeMatrixCommand(now);
                        if (writeOk) {
                            matrixWriteOkCount++;
                        } else {
                            matrixWriteFailCount++;
                        }

                        uint8_t switchBits[CAPTAIN_SWITCH_BYTES] = {};
                        const bool readOk = readMatrixSwitches(switchBits);
                        if (readOk) {
                            lastRawMatrixSwitch0 = switchBits[0];
                            lastRawMatrixSwitch1 = switchBits[1];
                            lastRawMatrixSwitch2 = switchBits[2];
                            lastRawMatrixSwitch3 = switchBits[3];
                            uint8_t normalizedSwitchBits[CAPTAIN_SWITCH_BYTES] = {};
                            normalizeMatrixSwitchBits(switchBits, normalizedSwitchBits);
                            uint8_t filteredSwitchBits[CAPTAIN_SWITCH_BYTES] = {};
                            filterMatrixSwitchBits(normalizedSwitchBits, filteredSwitchBits);
                            matrixReadOkCount++;
                            lastMatrixSwitch0 = filteredSwitchBits[0];
                            lastMatrixSwitch1 = filteredSwitchBits[1];
                            lastMatrixSwitch2 = filteredSwitchBits[2];
                            lastMatrixSwitch3 = filteredSwitchBits[3];
                            matrixSwitch0Seen = true;
                            memcpy(switchBits, filteredSwitchBits, CAPTAIN_SWITCH_BYTES);
                        } else {
                            matrixReadFailCount++;
                        }
                        if (writeOk && readOk && !matrixLinkFaulted) {
                            handleSwitchEdges(switchBits);
                            matrixDeviceReady = true;
                        } else {
                            matrixDeviceReady = false;
                        }

                        if (now - lastMatrixDiagPollMs >= MATRIX_DIAG_POLL_MS) {
                            lastMatrixDiagPollMs = now;
                            // Diagnostic polling removed in simplified bridge mode
                            // Bridge returns status 0x80 (enabled) always
                        }

                        const bool blink = ((now / 350) % 2) != 0;
                        headboxPattern = composeHeadboxPattern(displayScore, blink);
                        updateHeadboxLamps(headboxPattern);
                        if (!matrixSwitch0Seen) {
                            lastRawMatrixSwitch0 = 0;
                            lastRawMatrixSwitch1 = 0;
                            lastRawMatrixSwitch2 = 0;
                            lastRawMatrixSwitch3 = 0;
                            lastMatrixSwitch0 = 0;
                            lastMatrixSwitch1 = 0;
                            lastMatrixSwitch2 = 0;
                            lastMatrixSwitch3 = 0;
                        }
                        logMatrixLinkSummary(now);
                    }
                }

                if (now - lastDisplayUpdate >= 100) {
                    lastDisplayUpdate = now;
                    if (displayScore != lastDisplayedScore) {
                        updateLEDScore(displayScore);
                        lastDisplayedScore = displayScore;
                    }
                }

                vTaskDelay(1);
            }
        },
        "captain_ctrl",
        CONTROL_TASK_STACK_BYTES,
        nullptr,
        CONTROL_TASK_PRIORITY,
        &controlTaskHandle,
        CONTROL_TASK_CORE);
}

void loop() {
    vTaskDelay(portMAX_DELAY);
}
