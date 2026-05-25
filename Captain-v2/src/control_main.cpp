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

namespace {
constexpr uint32_t POLL_MS = 30;
constexpr uint32_t DIRECT_INPUT_POLL_MS = 5;
constexpr uint8_t DIRECT_INPUT_DEBOUNCE_TICKS = 3;
constexpr uint8_t HEARTBEAT_PIN = 15;
constexpr uint16_t HEARTBEAT_LED_COUNT = 300;
enum HeartbeatRenderMode : uint8_t {
    HEARTBEAT_RENDER_GAMEPLAY = 0,
    HEARTBEAT_RENDER_SEGMENT_MAP,
    HEARTBEAT_RENDER_ZONE_MAP
};

constexpr HeartbeatRenderMode HEARTBEAT_RENDER_MODE = HEARTBEAT_RENDER_GAMEPLAY;
constexpr uint16_t HEARTBEAT_ACTIVE_LED_COUNT = 300;
constexpr uint32_t HEARTBEAT_MAX_CURRENT_MA = 3000;
constexpr uint32_t HEARTBEAT_FULL_WHITE_MA_PER_LED = 60;
constexpr uint8_t HEARTBEAT_BRIGHTNESS = static_cast<uint8_t>((255u * HEARTBEAT_MAX_CURRENT_MA) /
                                                              (HEARTBEAT_ACTIVE_LED_COUNT * HEARTBEAT_FULL_WHITE_MA_PER_LED));
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 900;
constexpr bool HEARTBEAT_IS_WS2812 = true;
constexpr uint32_t HEARTBEAT_ATTRACT_INTERVAL_MS = 120;
constexpr uint32_t HEARTBEAT_SERVE_INTERVAL_MS = 90;
constexpr uint32_t HEARTBEAT_BONUS_INTERVAL_MS = 70;
constexpr uint32_t HEARTBEAT_GAME_OVER_INTERVAL_MS = 220;
constexpr uint32_t MATRIX_DIAG_POLL_MS = 250;
constexpr uint32_t MATRIX_LINK_TIMEOUT_MS = 1000;
constexpr uint32_t MATRIX_LINK_SUMMARY_MS = 1000;
constexpr uint32_t MATRIX_INIT_RETRY_MS = 1000;
constexpr uint32_t MATRIX_LAMP_KEEPALIVE_MS = 1000;
constexpr uint32_t MATRIX_SWITCH_READ_INTERVAL_MS = 60;
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
constexpr uint8_t AUDIO_QUEUE_LENGTH = 48;

struct AudioToneEvent {
    uint16_t frequencyHz;
    uint16_t durationMs;
    uint16_t delayBeforeMs;
};

struct HeartbeatSegmentRange {
    uint16_t startIndex;
    uint16_t endIndexExclusive;
};

enum HeartbeatZoneId : uint8_t {
    HEARTBEAT_ZONE_START_MARKER = 0,
    HEARTBEAT_ZONE_SCORE_PANEL,
    HEARTBEAT_ZONE_RIGHT_CREATURE_EDGE,
    HEARTBEAT_ZONE_TITLE_BANNER,
    HEARTBEAT_ZONE_LEFT_MOON_EDGE,
    HEARTBEAT_ZONE_BOTTOM_STAGE,
    HEARTBEAT_ZONE_LOWER_LEFT_TRANSITION,
    HEARTBEAT_ZONE_RIGHT_INTERIOR_COLUMN,
    HEARTBEAT_ZONE_ORGAN_AND_ROCKET_TRAIL,
    HEARTBEAT_ZONE_PERFORMER_ROW,
    HEARTBEAT_ZONE_CENTER_SPLIT,
    HEARTBEAT_ZONE_CAPTAIN_RING,
    HEARTBEAT_ZONE_RABBIT_AND_LEFT_INTERIOR
};

struct HeartbeatZoneDefinition {
    HeartbeatZoneId id;
    uint8_t segmentIds[3];
    uint8_t segmentCount;
};

struct HeartbeatRgb {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

struct HeartbeatSwitchZoneMap {
    uint8_t row;
    uint8_t col;
    HeartbeatZoneId zoneId;
};

struct HeartbeatZonePulse {
    bool active;
    HeartbeatZoneId zoneId;
    uint32_t color;
    uint32_t expiresAtMs;
};

struct HeartbeatPulseProfile {
    HeartbeatZoneId zoneId;
    HeartbeatRgb color;
    uint32_t durationMs;
};

struct HeartbeatScoreAccent {
    bool active;
    uint8_t tier;
    uint32_t expiresAtMs;
};

enum HeartbeatEventChoreographyId : uint8_t {
    HEARTBEAT_EVENT_NONE = 0,
    HEARTBEAT_EVENT_LANE_SET_COMPLETE,
    HEARTBEAT_EVENT_BONUS_X3
};

struct HeartbeatEventChoreography {
    bool active;
    HeartbeatEventChoreographyId id;
    uint32_t startedAtMs;
    uint32_t expiresAtMs;
};

constexpr HeartbeatSegmentRange HEARTBEAT_SEGMENT_RANGES[] = {
    {0, 11},    // Seg 0: LEDs 1-11
    {11, 22},   // Seg 1: LEDs 12-22, outside edge bottom 2
    {22, 52},   // Seg 2: LEDs 23-52, outside edge right side
    {52, 84},   // Seg 3: LEDs 53-84, outside edge top
    {84, 114},  // Seg 4: LEDs 85-114, outside edge left side
    {114, 137}, // Seg 5: LEDs 115-137, outside edge bottom 1
    {137, 146}, // Seg 6: LEDs 138-146
    {146, 168}, // Seg 7: LEDs 147-168
    {168, 194}, // Seg 8: LEDs 169-194
    {194, 212}, // Seg 9: LEDs 195-212
    {212, 231}, // Seg 10: LEDs 213-231
    {231, 245}, // Seg 11: LEDs 232-245
    {245, 264}, // Seg 12: LEDs 246-264
    {264, 272}, // Seg 13: LEDs 265-272
    {272, 300}  // Seg 14: LEDs 273-300
};

constexpr HeartbeatZoneDefinition HEARTBEAT_ZONE_DEFINITIONS[] = {
    {HEARTBEAT_ZONE_START_MARKER, {0, 0, 0}, 1},                 // Seg 0: start notch
    {HEARTBEAT_ZONE_SCORE_PANEL, {1, 0, 0}, 1},                  // Seg 1: players / score side
    {HEARTBEAT_ZONE_RIGHT_CREATURE_EDGE, {2, 0, 0}, 1},          // Seg 2: right outer creature edge
    {HEARTBEAT_ZONE_TITLE_BANNER, {3, 0, 0}, 1},                 // Seg 3: title / top banner
    {HEARTBEAT_ZONE_LEFT_MOON_EDGE, {4, 0, 0}, 1},               // Seg 4: left outer moon side
    {HEARTBEAT_ZONE_BOTTOM_STAGE, {5, 0, 0}, 1},                 // Seg 5: lower platform / stage
    {HEARTBEAT_ZONE_LOWER_LEFT_TRANSITION, {6, 7, 0}, 2},        // Seg 6-7: lower-left transition / diagonal
    {HEARTBEAT_ZONE_RIGHT_INTERIOR_COLUMN, {8, 0, 0}, 1},        // Seg 8: right interior column
    {HEARTBEAT_ZONE_ORGAN_AND_ROCKET_TRAIL, {9, 0, 0}, 1},       // Seg 9: organ / rocket trail region
    {HEARTBEAT_ZONE_PERFORMER_ROW, {11, 0, 0}, 1},               // Seg 11: performer / crowd row
    {HEARTBEAT_ZONE_CENTER_SPLIT, {12, 0, 0}, 1},                // Seg 12: center split / start column
    {HEARTBEAT_ZONE_CAPTAIN_RING, {13, 0, 0}, 1},                // Seg 13: captain / ring accent
    {HEARTBEAT_ZONE_RABBIT_AND_LEFT_INTERIOR, {10, 14, 0}, 2}    // Seg 10 + 14: rabbit / left upper interior
};

constexpr HeartbeatZoneId HEARTBEAT_ATTRACT_SEQUENCE[] = {
    HEARTBEAT_ZONE_TITLE_BANNER,
    HEARTBEAT_ZONE_CAPTAIN_RING,
    HEARTBEAT_ZONE_PERFORMER_ROW,
    HEARTBEAT_ZONE_SCORE_PANEL,
    HEARTBEAT_ZONE_RIGHT_CREATURE_EDGE,
    HEARTBEAT_ZONE_ORGAN_AND_ROCKET_TRAIL,
    HEARTBEAT_ZONE_RABBIT_AND_LEFT_INTERIOR,
    HEARTBEAT_ZONE_LEFT_MOON_EDGE
};

constexpr HeartbeatSwitchZoneMap HEARTBEAT_SWITCH_ZONE_MAP[] = {
    {0, 2, HEARTBEAT_ZONE_RIGHT_CREATURE_EDGE},
    {0, 3, HEARTBEAT_ZONE_SCORE_PANEL},
    {1, 0, HEARTBEAT_ZONE_START_MARKER},
    {1, 2, HEARTBEAT_ZONE_RIGHT_INTERIOR_COLUMN},
    {1, 3, HEARTBEAT_ZONE_BOTTOM_STAGE},
    {2, 0, HEARTBEAT_ZONE_LEFT_MOON_EDGE},
    {2, 1, HEARTBEAT_ZONE_BOTTOM_STAGE},
    {2, 2, HEARTBEAT_ZONE_CENTER_SPLIT},
    {2, 3, HEARTBEAT_ZONE_RABBIT_AND_LEFT_INTERIOR},
    {3, 0, HEARTBEAT_ZONE_TITLE_BANNER},
    {3, 1, HEARTBEAT_ZONE_BOTTOM_STAGE},
    {3, 2, HEARTBEAT_ZONE_ORGAN_AND_ROCKET_TRAIL},
    {3, 3, HEARTBEAT_ZONE_PERFORMER_ROW},
    {4, 0, HEARTBEAT_ZONE_RIGHT_CREATURE_EDGE},
    {4, 2, HEARTBEAT_ZONE_CAPTAIN_RING},
    {4, 3, HEARTBEAT_ZONE_BOTTOM_STAGE},
    {5, 0, HEARTBEAT_ZONE_RABBIT_AND_LEFT_INTERIOR},
    {5, 2, HEARTBEAT_ZONE_CENTER_SPLIT},
    {5, 3, HEARTBEAT_ZONE_PERFORMER_ROW}
};

constexpr HeartbeatRgb HEARTBEAT_INCANDESCENT_GI_RGB = {255, 180, 110};
constexpr HeartbeatRgb HEARTBEAT_INCANDESCENT_PILOT_RGB = {255, 214, 170};
constexpr HeartbeatRgb HEARTBEAT_WARM_FLASH_RGB = {255, 232, 190};
constexpr HeartbeatRgb HEARTBEAT_LANE_FLASH_RGB = {255, 222, 150};
constexpr HeartbeatRgb HEARTBEAT_TARGET_FLASH_RGB = {255, 152, 72};
constexpr HeartbeatRgb HEARTBEAT_POP_FLASH_RGB = {255, 132, 72};
constexpr HeartbeatRgb HEARTBEAT_SPINNER_FLASH_RGB = {255, 198, 132};
constexpr HeartbeatRgb HEARTBEAT_RETURN_FLASH_RGB = {255, 214, 154};
constexpr HeartbeatRgb HEARTBEAT_MILESTONE_FLASH_RGB = {255, 236, 180};
constexpr HeartbeatRgb HEARTBEAT_SCORE_GLINT_RGB = {255, 238, 200};
constexpr HeartbeatRgb HEARTBEAT_SCORE_HOT_RGB = {255, 196, 120};
constexpr HeartbeatRgb HEARTBEAT_EVENT_SWEEP_RGB = {255, 232, 176};
constexpr HeartbeatRgb HEARTBEAT_EVENT_HOT_RGB = {255, 180, 90};
constexpr uint8_t HEARTBEAT_ZONE_BACKGROUND_SCALE = 34;
constexpr uint8_t HEARTBEAT_ZONE_NEIGHBOR_SCALE = 132;
constexpr uint8_t HEARTBEAT_ZONE_ACCENT_SCALE = 188;
constexpr uint32_t HEARTBEAT_ZONE_PULSE_MS = 450;
constexpr uint32_t HEARTBEAT_SHORT_PULSE_MS = 180;
constexpr uint32_t HEARTBEAT_MEDIUM_PULSE_MS = 260;
constexpr uint32_t HEARTBEAT_LONG_PULSE_MS = 420;
constexpr uint32_t HEARTBEAT_MILESTONE_PULSE_MS = 720;
constexpr uint32_t HEARTBEAT_SCORE_GLINT_MS = 180;
constexpr uint32_t HEARTBEAT_SCORE_HOT_MS = 260;
constexpr uint32_t HEARTBEAT_SCORE_JACKPOT_MS = 340;
constexpr uint32_t HEARTBEAT_EVENT_STEP_MS = 140;
constexpr uint32_t HEARTBEAT_EVENT_TRAIL_MS = 320;

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
    uint32_t serveBallAtMs = 0;
};

constexpr uint32_t BONUS_COUNTDOWN_STEP_MS = 350;
constexpr uint16_t GAMEPLAY_BONUS_START = 1000;
constexpr uint16_t GAMEPLAY_BONUS_MAX = 10000;
constexpr uint16_t GAMEPLAY_SCORE_CHIME_BASE_HZ = 400;
constexpr uint16_t GAMEPLAY_SCORE_CHIME_100_ACCENT_HZ = 620;
constexpr uint16_t GAMEPLAY_SCORE_CHIME_1000_ACCENT_HZ = 880;
constexpr uint16_t GAMEPLAY_SCORE_CHIME_BASE_MS = 70;
constexpr uint16_t GAMEPLAY_SCORE_CHIME_ACCENT_MS = 30;
constexpr uint16_t GAMEPLAY_SCORE_CHIME_GAP_MS = 50;
constexpr uint16_t BUMPER_TONE_DELAY_MS = 18;
constexpr uint16_t SLINGSHOT_TONE_DELAY_MS = 24;
constexpr uint16_t GAME_START_TONE_GAP_MS = 140;
constexpr uint32_t GAME_START_SERVE_DELAY_MS = 4100;
constexpr uint32_t GAMEPLAY_SWITCH_RETRIGGER_MS = 90;
constexpr uint32_t FAST_MECH_SWITCH_RETRIGGER_MS = 35;

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
uint32_t lastAudioDiagnosticMs = 0;
uint32_t lastAudioGpioToggleMs = 0;
bool audioGpioOnlyState = false;
bool heartbeatEnabled = false;
bool heartbeatState = false;
uint32_t lastHeartbeatToggleMs = 0;
uint16_t heartbeatPatternIndex = 0;
Adafruit_NeoPixel heartbeatPixel(HEARTBEAT_LED_COUNT, HEARTBEAT_PIN, NEO_GRB + NEO_KHZ800);
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
uint32_t lastMatrixSwitchReadMs = 0;
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
uint32_t lastGameplaySwitchHitMs[CAPTAIN_SWITCH_ROWS * CAPTAIN_SWITCH_COLS] = {};
bool currentSW1Mode = false;  // false = Easy, true = Hard
bool currentSW2Mode = false;  // false = Game, true = Test
uint16_t matrixSwitchLoggedThisWindow = 0;
uint16_t matrixSwitchEdgesThisWindow = 0;
uint32_t matrixSwitchLogWindowStartMs = 0;
HeartbeatZonePulse heartbeatZonePulse = {false, HEARTBEAT_ZONE_START_MARKER, 0, 0};
HeartbeatScoreAccent heartbeatScoreAccent = {false, 0, 0};
HeartbeatEventChoreography heartbeatEventChoreography = {false, HEARTBEAT_EVENT_NONE, 0, 0};
CaptainGameplayState gameplayState = {};
QueueHandle_t audioToneQueue = nullptr;
TaskHandle_t controlTaskHandle = nullptr;
TaskHandle_t audioTaskHandle = nullptr;

void renderHeartbeatPattern();
void resetGameplayStateForNewBall();
void startNewGame(uint32_t nowMs);
void startBonusCountdown(uint32_t nowMs);
void updateGameplayState(uint32_t nowMs);
void handleGameplaySwitchHit(uint8_t row, uint8_t col, uint32_t nowMs);
bool queueToneDelayed(uint16_t frequencyHz, uint16_t durationMs, uint16_t delayBeforeMs) {
    if (!i2sAudioReady || CAPTAIN_AUDIO_GPIO_ONLY_TEST_MODE || audioToneQueue == nullptr) {
        return false;
    }

    AudioToneEvent event = {frequencyHz, durationMs, delayBeforeMs};
    if (xQueueSend(audioToneQueue, &event
        , 0) == pdPASS) {
        return true;
    }
    return false;
}

bool queueTone(uint16_t frequencyHz, uint16_t durationMs) {
    return queueToneDelayed(frequencyHz, durationMs, 0);
}

void queueBumperTone() {
    queueToneDelayed(700, 100, BUMPER_TONE_DELAY_MS);
}

void queueSlingshotTone() {
    queueToneDelayed(700, 60, SLINGSHOT_TONE_DELAY_MS);
    queueTone(850, 50);
}

void queueTargetTone() {
    queueTone(1300, 80);
}

void queueRolloverTone() {
    queueTone(1300, 60);
}

void queueDrainTone() {
    queueTone(1200, 80);
    queueTone(1000, 80);
    queueTone(800, 80);
    queueTone(600, 80);
    queueTone(400, 80);
}

void queueBonusCountTone() {
    queueTone(1000, 50);
}

void queueScoringTone(uint32_t points) {
    const uint16_t pulseCount = static_cast<uint16_t>(max<uint32_t>(1, points / 100));
    const uint16_t accentHz = points >= 1000 ? GAMEPLAY_SCORE_CHIME_1000_ACCENT_HZ
                                             : GAMEPLAY_SCORE_CHIME_100_ACCENT_HZ;

    for (uint16_t pulse = 0; pulse < pulseCount; pulse++) {
        queueTone(GAMEPLAY_SCORE_CHIME_BASE_HZ, GAMEPLAY_SCORE_CHIME_BASE_MS);
        queueTone(accentHz, GAMEPLAY_SCORE_CHIME_ACCENT_MS);
        if (pulse + 1u < pulseCount) {
            queueToneDelayed(GAMEPLAY_SCORE_CHIME_BASE_HZ, 0, GAMEPLAY_SCORE_CHIME_GAP_MS);
        }
    }
}

void queueGameStartFanfare() {
    const uint16_t startPatternHz[] = {392, 523, 659, 784, 880, 784, 659, 523};
    constexpr uint16_t startToneMs = 360;

    queueTone(startPatternHz[0], startToneMs);
    for (size_t index = 1; index < (sizeof(startPatternHz) / sizeof(startPatternHz[0])); index++) {
        queueToneDelayed(startPatternHz[index], startToneMs, GAME_START_TONE_GAP_MS);
    }
}

uint32_t gameplaySwitchRetriggerMs(uint8_t row, uint8_t col) {
    if ((row == 0 && col == 2) || (row == 3 && col == 2)) {
        return 0;
    }

    if ((row == 1 && col == 3) || (row == 2 && col == 1) || (row == 3 && col == 1) || (row == 4 && col == 3)) {
        return FAST_MECH_SWITCH_RETRIGGER_MS;
    }

    return GAMEPLAY_SWITCH_RETRIGGER_MS;
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
    Serial.printf("Matrix link: ready=%u fault=%u wr_ok=%lu wr_fail=%lu rd_ok=%lu rd_fail=%lu diag_warn=%lu raw0=0x%02X raw1=0x%02X raw2=0x%02X raw3=0x%02X sw0=0x%02X sw1=0x%02X sw2=0x%02X sw3=0x%02X sw_edges=%u sw_log=%u sup_db=%lu sup_rate=%lu sup_le=%lu s20_sticky=%lu\n",
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
                  static_cast<unsigned long>(s20OutholeSuppressedSticky));

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

uint32_t heartbeatIntervalForMode() {
    switch (gameplayState.mode) {
        case GAME_MODE_SERVE_BALL:
        case GAME_MODE_BALL_IN_PLAY:
            return HEARTBEAT_SERVE_INTERVAL_MS;
        case GAME_MODE_BONUS_COUNTDOWN:
            return HEARTBEAT_BONUS_INTERVAL_MS;
        case GAME_MODE_GAME_OVER:
            return HEARTBEAT_GAME_OVER_INTERVAL_MS;
        case GAME_MODE_ATTRACT:
        default:
            return HEARTBEAT_ATTRACT_INTERVAL_MS;
    }
}

const HeartbeatZoneDefinition* heartbeatZoneDefinitionForId(HeartbeatZoneId zoneId) {
    for (const HeartbeatZoneDefinition& zone : HEARTBEAT_ZONE_DEFINITIONS) {
        if (zone.id == zoneId) {
            return &zone;
        }
    }
    return nullptr;
}

HeartbeatRgb heartbeatRgbForZone(HeartbeatZoneId zoneId) {
    switch (zoneId) {
        case HEARTBEAT_ZONE_START_MARKER:
            return {255, 235, 190};
        case HEARTBEAT_ZONE_SCORE_PANEL:
            return {255, 205, 145};
        case HEARTBEAT_ZONE_RIGHT_CREATURE_EDGE:
            return {255, 120, 70};
        case HEARTBEAT_ZONE_TITLE_BANNER:
            return {255, 180, 70};
        case HEARTBEAT_ZONE_LEFT_MOON_EDGE:
            return {255, 200, 140};
        case HEARTBEAT_ZONE_BOTTOM_STAGE:
            return {255, 150, 80};
        case HEARTBEAT_ZONE_LOWER_LEFT_TRANSITION:
            return {255, 185, 100};
        case HEARTBEAT_ZONE_RIGHT_INTERIOR_COLUMN:
            return {255, 165, 105};
        case HEARTBEAT_ZONE_ORGAN_AND_ROCKET_TRAIL:
            return {255, 128, 60};
        case HEARTBEAT_ZONE_PERFORMER_ROW:
            return {255, 210, 120};
        case HEARTBEAT_ZONE_CENTER_SPLIT:
            return {255, 225, 175};
        case HEARTBEAT_ZONE_CAPTAIN_RING:
            return {255, 190, 95};
        case HEARTBEAT_ZONE_RABBIT_AND_LEFT_INTERIOR:
            return {255, 170, 125};
        default:
            return {255, 220, 170};
    }
}

HeartbeatRgb scaleHeartbeatRgb(HeartbeatRgb color, uint8_t scale) {
    HeartbeatRgb scaled = {};
    scaled.red = static_cast<uint8_t>((static_cast<uint16_t>(color.red) * scale) / 255u);
    scaled.green = static_cast<uint8_t>((static_cast<uint16_t>(color.green) * scale) / 255u);
    scaled.blue = static_cast<uint8_t>((static_cast<uint16_t>(color.blue) * scale) / 255u);
    return scaled;
}

uint32_t heartbeatColorFromRgb(HeartbeatRgb color) {
    return heartbeatPixel.Color(color.red, color.green, color.blue);
}

uint32_t heartbeatColorForZone(HeartbeatZoneId zoneId, uint8_t scale = 255) {
    return heartbeatColorFromRgb(scaleHeartbeatRgb(heartbeatRgbForZone(zoneId), scale));
}

uint32_t heartbeatColorForIndex(uint16_t colorIndex) {
    return heartbeatColorForZone(static_cast<HeartbeatZoneId>(colorIndex));
}

void fillHeartbeatSegment(uint8_t segmentId, uint16_t activeCount, uint32_t segmentColor) {
    if (segmentId >= (sizeof(HEARTBEAT_SEGMENT_RANGES) / sizeof(HEARTBEAT_SEGMENT_RANGES[0]))) {
        return;
    }

    const uint16_t startLed = min<uint16_t>(HEARTBEAT_SEGMENT_RANGES[segmentId].startIndex, activeCount);
    const uint16_t endLed = min<uint16_t>(HEARTBEAT_SEGMENT_RANGES[segmentId].endIndexExclusive, activeCount);
    if (startLed >= endLed) {
        return;
    }

    heartbeatPixel.setPixelColor(startLed, heartbeatColorFromRgb(HEARTBEAT_INCANDESCENT_PILOT_RGB));
    for (uint16_t pixelIndex = startLed + 1u; pixelIndex < endLed; pixelIndex++) {
        heartbeatPixel.setPixelColor(pixelIndex, segmentColor);
    }
}

void fillHeartbeatZone(HeartbeatZoneId zoneId, uint16_t activeCount, uint32_t color) {
    const HeartbeatZoneDefinition* zone = heartbeatZoneDefinitionForId(zoneId);
    if (zone == nullptr) {
        return;
    }

    for (uint8_t segmentOffset = 0; segmentOffset < zone->segmentCount; segmentOffset++) {
        fillHeartbeatSegment(zone->segmentIds[segmentOffset], activeCount, color);
    }
}

void fillHeartbeatAllZones(uint16_t activeCount, uint8_t scale) {
    const uint32_t giColor = heartbeatColorFromRgb(scaleHeartbeatRgb(HEARTBEAT_INCANDESCENT_GI_RGB, scale));
    for (const HeartbeatZoneDefinition& zone : HEARTBEAT_ZONE_DEFINITIONS) {
        fillHeartbeatZone(zone.id, activeCount, giColor);
    }
}

HeartbeatZoneId heartbeatZoneForSwitch(uint8_t row, uint8_t col) {
    for (const HeartbeatSwitchZoneMap& mapping : HEARTBEAT_SWITCH_ZONE_MAP) {
        if (mapping.row == row && mapping.col == col) {
            return mapping.zoneId;
        }
    }
    return HEARTBEAT_ZONE_CENTER_SPLIT;
}

void triggerHeartbeatZonePulse(HeartbeatZoneId zoneId, uint32_t color, uint32_t durationMs) {
    heartbeatZonePulse.active = true;
    heartbeatZonePulse.zoneId = zoneId;
    heartbeatZonePulse.color = color;
    heartbeatZonePulse.expiresAtMs = millis() + durationMs;

    if (heartbeatEnabled && HEARTBEAT_IS_WS2812 && HEARTBEAT_RENDER_MODE == HEARTBEAT_RENDER_GAMEPLAY) {
        renderHeartbeatPattern();
    }
}

void triggerHeartbeatPulseProfile(const HeartbeatPulseProfile& profile) {
    triggerHeartbeatZonePulse(profile.zoneId, heartbeatColorFromRgb(profile.color), profile.durationMs);
}

void triggerHeartbeatScoreAccent(uint32_t points) {
    heartbeatScoreAccent.active = true;
    if (points >= 1000) {
        heartbeatScoreAccent.tier = 2;
        heartbeatScoreAccent.expiresAtMs = millis() + HEARTBEAT_SCORE_JACKPOT_MS;
    } else if (points >= 500) {
        heartbeatScoreAccent.tier = 1;
        heartbeatScoreAccent.expiresAtMs = millis() + HEARTBEAT_SCORE_HOT_MS;
    } else {
        heartbeatScoreAccent.tier = 0;
        heartbeatScoreAccent.expiresAtMs = millis() + HEARTBEAT_SCORE_GLINT_MS;
    }

    if (heartbeatEnabled && HEARTBEAT_IS_WS2812 && HEARTBEAT_RENDER_MODE == HEARTBEAT_RENDER_GAMEPLAY) {
        renderHeartbeatPattern();
    }
}

void triggerHeartbeatEventChoreography(HeartbeatEventChoreographyId id, uint32_t durationMs) {
    heartbeatEventChoreography.active = true;
    heartbeatEventChoreography.id = id;
    heartbeatEventChoreography.startedAtMs = millis();
    heartbeatEventChoreography.expiresAtMs = heartbeatEventChoreography.startedAtMs + durationMs;

    if (heartbeatEnabled && HEARTBEAT_IS_WS2812 && HEARTBEAT_RENDER_MODE == HEARTBEAT_RENDER_GAMEPLAY) {
        renderHeartbeatPattern();
    }
}

HeartbeatPulseProfile heartbeatPulseProfileForSwitch(uint8_t row, uint8_t col) {
    switch (row) {
        case 0:
            if (col == 2) {
                return {HEARTBEAT_ZONE_RIGHT_CREATURE_EDGE, HEARTBEAT_SPINNER_FLASH_RGB, HEARTBEAT_SHORT_PULSE_MS};
            }
            if (col == 3) {
                return {HEARTBEAT_ZONE_SCORE_PANEL, HEARTBEAT_RETURN_FLASH_RGB, HEARTBEAT_LONG_PULSE_MS};
            }
            break;
        case 1:
            if (col == 0) {
                return {HEARTBEAT_ZONE_START_MARKER, HEARTBEAT_LANE_FLASH_RGB, HEARTBEAT_LONG_PULSE_MS};
            }
            if (col == 2) {
                return {HEARTBEAT_ZONE_RIGHT_INTERIOR_COLUMN, HEARTBEAT_TARGET_FLASH_RGB, HEARTBEAT_LONG_PULSE_MS};
            }
            if (col == 3) {
                return {HEARTBEAT_ZONE_BOTTOM_STAGE, HEARTBEAT_POP_FLASH_RGB, HEARTBEAT_MEDIUM_PULSE_MS};
            }
            break;
        case 2:
            if (col == 0) {
                return {HEARTBEAT_ZONE_LEFT_MOON_EDGE, HEARTBEAT_LANE_FLASH_RGB, HEARTBEAT_LONG_PULSE_MS};
            }
            if (col == 1) {
                return {HEARTBEAT_ZONE_BOTTOM_STAGE, HEARTBEAT_POP_FLASH_RGB, HEARTBEAT_MEDIUM_PULSE_MS};
            }
            if (col == 2) {
                return {HEARTBEAT_ZONE_CENTER_SPLIT, HEARTBEAT_SPINNER_FLASH_RGB, HEARTBEAT_SHORT_PULSE_MS};
            }
            if (col == 3) {
                return {HEARTBEAT_ZONE_RABBIT_AND_LEFT_INTERIOR, HEARTBEAT_RETURN_FLASH_RGB, HEARTBEAT_LONG_PULSE_MS};
            }
            break;
        case 3:
            if (col == 0) {
                return {HEARTBEAT_ZONE_TITLE_BANNER, HEARTBEAT_LANE_FLASH_RGB, HEARTBEAT_LONG_PULSE_MS};
            }
            if (col == 1) {
                return {HEARTBEAT_ZONE_BOTTOM_STAGE, HEARTBEAT_POP_FLASH_RGB, HEARTBEAT_MEDIUM_PULSE_MS};
            }
            if (col == 2) {
                return {HEARTBEAT_ZONE_ORGAN_AND_ROCKET_TRAIL, HEARTBEAT_SPINNER_FLASH_RGB, HEARTBEAT_SHORT_PULSE_MS};
            }
            if (col == 3) {
                return {HEARTBEAT_ZONE_PERFORMER_ROW, HEARTBEAT_RETURN_FLASH_RGB, HEARTBEAT_LONG_PULSE_MS};
            }
            break;
        case 4:
            if (col == 0) {
                return {HEARTBEAT_ZONE_RIGHT_CREATURE_EDGE, HEARTBEAT_LANE_FLASH_RGB, HEARTBEAT_LONG_PULSE_MS};
            }
            if (col == 2) {
                return {HEARTBEAT_ZONE_CAPTAIN_RING, HEARTBEAT_TARGET_FLASH_RGB, HEARTBEAT_LONG_PULSE_MS};
            }
            if (col == 3) {
                return {HEARTBEAT_ZONE_BOTTOM_STAGE, HEARTBEAT_POP_FLASH_RGB, HEARTBEAT_MEDIUM_PULSE_MS};
            }
            break;
        case 5:
            if (col == 0) {
                return {HEARTBEAT_ZONE_RABBIT_AND_LEFT_INTERIOR, HEARTBEAT_TARGET_FLASH_RGB, HEARTBEAT_LONG_PULSE_MS};
            }
            if (col == 2) {
                return {HEARTBEAT_ZONE_CENTER_SPLIT, HEARTBEAT_SPINNER_FLASH_RGB, HEARTBEAT_SHORT_PULSE_MS};
            }
            if (col == 3) {
                return {HEARTBEAT_ZONE_PERFORMER_ROW, HEARTBEAT_RETURN_FLASH_RGB, HEARTBEAT_LONG_PULSE_MS};
            }
            break;
        default:
            break;
    }

    return {heartbeatZoneForSwitch(row, col), HEARTBEAT_WARM_FLASH_RGB, HEARTBEAT_ZONE_PULSE_MS};
}

void applyHeartbeatZonePulse(uint16_t activeCount, uint32_t nowMs) {
    if (!heartbeatZonePulse.active) {
        return;
    }
    if (nowMs >= heartbeatZonePulse.expiresAtMs) {
        heartbeatZonePulse.active = false;
        return;
    }

    fillHeartbeatZone(heartbeatZonePulse.zoneId, activeCount, heartbeatZonePulse.color);
}

void applyHeartbeatScoreAccent(uint16_t activeCount, uint32_t nowMs) {
    if (!heartbeatScoreAccent.active) {
        return;
    }
    if (nowMs >= heartbeatScoreAccent.expiresAtMs) {
        heartbeatScoreAccent.active = false;
        return;
    }

    const bool shimmerOn = (heartbeatPatternIndex % 2u) == 0u;
    fillHeartbeatZone(HEARTBEAT_ZONE_SCORE_PANEL, activeCount,
                      heartbeatColorFromRgb(shimmerOn ? HEARTBEAT_SCORE_GLINT_RGB : HEARTBEAT_SCORE_HOT_RGB));
    fillHeartbeatZone(HEARTBEAT_ZONE_CENTER_SPLIT, activeCount,
                      heartbeatColorForZone(HEARTBEAT_ZONE_CENTER_SPLIT, shimmerOn ? 255 : HEARTBEAT_ZONE_ACCENT_SCALE));

    if (heartbeatScoreAccent.tier >= 1) {
        fillHeartbeatZone(HEARTBEAT_ZONE_PERFORMER_ROW, activeCount,
                          heartbeatColorFromRgb(shimmerOn ? HEARTBEAT_SCORE_HOT_RGB : HEARTBEAT_SCORE_GLINT_RGB));
    }
    if (heartbeatScoreAccent.tier >= 2) {
        fillHeartbeatZone(HEARTBEAT_ZONE_CAPTAIN_RING, activeCount,
                          heartbeatColorFromRgb(HEARTBEAT_SCORE_HOT_RGB));
    }
}

void applyHeartbeatEventChoreography(uint16_t activeCount, uint32_t nowMs) {
    if (!heartbeatEventChoreography.active) {
        return;
    }
    if (nowMs >= heartbeatEventChoreography.expiresAtMs) {
        heartbeatEventChoreography.active = false;
        heartbeatEventChoreography.id = HEARTBEAT_EVENT_NONE;
        return;
    }

    static constexpr HeartbeatZoneId laneSetSweep[] = {
        HEARTBEAT_ZONE_START_MARKER,
        HEARTBEAT_ZONE_LEFT_MOON_EDGE,
        HEARTBEAT_ZONE_TITLE_BANNER,
        HEARTBEAT_ZONE_RIGHT_CREATURE_EDGE,
        HEARTBEAT_ZONE_SCORE_PANEL
    };

    static constexpr HeartbeatZoneId bonus3xSweep[] = {
        HEARTBEAT_ZONE_RIGHT_INTERIOR_COLUMN,
        HEARTBEAT_ZONE_CAPTAIN_RING,
        HEARTBEAT_ZONE_ORGAN_AND_ROCKET_TRAIL,
        HEARTBEAT_ZONE_PERFORMER_ROW,
        HEARTBEAT_ZONE_SCORE_PANEL
    };

    const HeartbeatZoneId* sequence = nullptr;
    uint8_t sequenceCount = 0;
    switch (heartbeatEventChoreography.id) {
        case HEARTBEAT_EVENT_LANE_SET_COMPLETE:
            sequence = laneSetSweep;
            sequenceCount = static_cast<uint8_t>(sizeof(laneSetSweep) / sizeof(laneSetSweep[0]));
            break;
        case HEARTBEAT_EVENT_BONUS_X3:
            sequence = bonus3xSweep;
            sequenceCount = static_cast<uint8_t>(sizeof(bonus3xSweep) / sizeof(bonus3xSweep[0]));
            break;
        case HEARTBEAT_EVENT_NONE:
        default:
            return;
    }

    const uint32_t elapsedMs = nowMs - heartbeatEventChoreography.startedAtMs;
    const uint8_t leadIndex = static_cast<uint8_t>((elapsedMs / HEARTBEAT_EVENT_STEP_MS) % sequenceCount);
    const uint8_t trailIndex = static_cast<uint8_t>((leadIndex + sequenceCount - 1u) % sequenceCount);

    fillHeartbeatZone(sequence[trailIndex], activeCount, heartbeatColorFromRgb(HEARTBEAT_EVENT_SWEEP_RGB));
    fillHeartbeatZone(sequence[leadIndex], activeCount, heartbeatColorFromRgb(HEARTBEAT_EVENT_HOT_RGB));
}

void renderHeartbeatAttractZones(uint16_t activeCount) {
    fillHeartbeatAllZones(activeCount, HEARTBEAT_ZONE_BACKGROUND_SCALE);

    const uint8_t sequenceCount = static_cast<uint8_t>(sizeof(HEARTBEAT_ATTRACT_SEQUENCE) / sizeof(HEARTBEAT_ATTRACT_SEQUENCE[0]));
    const uint8_t leadIndex = static_cast<uint8_t>(heartbeatPatternIndex % sequenceCount);
    const uint8_t trailIndex = static_cast<uint8_t>((leadIndex + sequenceCount - 1u) % sequenceCount);
    const uint8_t mirrorIndex = static_cast<uint8_t>((leadIndex + (sequenceCount / 2u)) % sequenceCount);
    const bool bannerPulseOn = (heartbeatPatternIndex % 2u) == 0u;

    fillHeartbeatZone(HEARTBEAT_ATTRACT_SEQUENCE[trailIndex], activeCount,
                      heartbeatColorForZone(HEARTBEAT_ATTRACT_SEQUENCE[trailIndex], HEARTBEAT_ZONE_NEIGHBOR_SCALE));
    fillHeartbeatZone(HEARTBEAT_ATTRACT_SEQUENCE[mirrorIndex], activeCount,
                      heartbeatColorForZone(HEARTBEAT_ATTRACT_SEQUENCE[mirrorIndex], HEARTBEAT_ZONE_ACCENT_SCALE));
    fillHeartbeatZone(HEARTBEAT_ATTRACT_SEQUENCE[leadIndex], activeCount,
                      heartbeatColorForZone(HEARTBEAT_ATTRACT_SEQUENCE[leadIndex], 255));
    fillHeartbeatZone(HEARTBEAT_ZONE_TITLE_BANNER, activeCount,
                      heartbeatColorForZone(HEARTBEAT_ZONE_TITLE_BANNER,
                                            bannerPulseOn ? 255 : HEARTBEAT_ZONE_NEIGHBOR_SCALE));
    fillHeartbeatZone(HEARTBEAT_ZONE_SCORE_PANEL, activeCount,
                      heartbeatColorForZone(HEARTBEAT_ZONE_SCORE_PANEL,
                                            bannerPulseOn ? HEARTBEAT_ZONE_NEIGHBOR_SCALE : HEARTBEAT_ZONE_ACCENT_SCALE));
}

void renderHeartbeatServeBallZones(uint16_t activeCount) {
    fillHeartbeatAllZones(activeCount, HEARTBEAT_ZONE_BACKGROUND_SCALE);

    const uint8_t pulseScale = ((heartbeatPatternIndex % 2u) == 0u) ? 255u : HEARTBEAT_ZONE_ACCENT_SCALE;
    fillHeartbeatZone(HEARTBEAT_ZONE_START_MARKER, activeCount, heartbeatColorForZone(HEARTBEAT_ZONE_START_MARKER, pulseScale));
    fillHeartbeatZone(HEARTBEAT_ZONE_CENTER_SPLIT, activeCount, heartbeatColorForZone(HEARTBEAT_ZONE_CENTER_SPLIT, pulseScale));
    fillHeartbeatZone(HEARTBEAT_ZONE_BOTTOM_STAGE, activeCount, heartbeatColorForZone(HEARTBEAT_ZONE_BOTTOM_STAGE, HEARTBEAT_ZONE_NEIGHBOR_SCALE));
    applyHeartbeatScoreAccent(activeCount, millis());
    applyHeartbeatEventChoreography(activeCount, millis());
}

void renderHeartbeatBallInPlayZones(uint16_t activeCount, uint32_t nowMs) {
    fillHeartbeatAllZones(activeCount, HEARTBEAT_ZONE_BACKGROUND_SCALE);

    if (gameplayState.laneAComplete) {
        fillHeartbeatZone(HEARTBEAT_ZONE_START_MARKER, activeCount, heartbeatColorForZone(HEARTBEAT_ZONE_START_MARKER, HEARTBEAT_ZONE_ACCENT_SCALE));
    }
    if (gameplayState.laneBComplete) {
        fillHeartbeatZone(HEARTBEAT_ZONE_LEFT_MOON_EDGE, activeCount, heartbeatColorForZone(HEARTBEAT_ZONE_LEFT_MOON_EDGE, HEARTBEAT_ZONE_ACCENT_SCALE));
    }
    if (gameplayState.laneCComplete) {
        fillHeartbeatZone(HEARTBEAT_ZONE_TITLE_BANNER, activeCount, heartbeatColorForZone(HEARTBEAT_ZONE_TITLE_BANNER, HEARTBEAT_ZONE_ACCENT_SCALE));
    }
    if (gameplayState.laneDComplete) {
        fillHeartbeatZone(HEARTBEAT_ZONE_RIGHT_CREATURE_EDGE, activeCount, heartbeatColorForZone(HEARTBEAT_ZONE_RIGHT_CREATURE_EDGE, HEARTBEAT_ZONE_ACCENT_SCALE));
    }

    if (gameplayState.target1Complete) {
        fillHeartbeatZone(HEARTBEAT_ZONE_RIGHT_INTERIOR_COLUMN, activeCount, heartbeatColorForZone(HEARTBEAT_ZONE_RIGHT_INTERIOR_COLUMN, 255));
    }
    if (gameplayState.target2Complete) {
        fillHeartbeatZone(HEARTBEAT_ZONE_CAPTAIN_RING, activeCount, heartbeatColorForZone(HEARTBEAT_ZONE_CAPTAIN_RING, 255));
    }
    if (gameplayState.target3Complete) {
        fillHeartbeatZone(HEARTBEAT_ZONE_RABBIT_AND_LEFT_INTERIOR, activeCount, heartbeatColorForZone(HEARTBEAT_ZONE_RABBIT_AND_LEFT_INTERIOR, 255));
    }

    if (gameplayState.bonusMultiplier >= 2) {
        fillHeartbeatZone(HEARTBEAT_ZONE_PERFORMER_ROW, activeCount, heartbeatColorForZone(HEARTBEAT_ZONE_PERFORMER_ROW, HEARTBEAT_ZONE_ACCENT_SCALE));
    }
    if (gameplayState.bonusMultiplier >= 3) {
        fillHeartbeatZone(HEARTBEAT_ZONE_ORGAN_AND_ROCKET_TRAIL, activeCount, heartbeatColorForZone(HEARTBEAT_ZONE_ORGAN_AND_ROCKET_TRAIL, HEARTBEAT_ZONE_ACCENT_SCALE));
    }
    if (gameplayState.samePlayerLit) {
        fillHeartbeatZone(HEARTBEAT_ZONE_SCORE_PANEL, activeCount, heartbeatColorForZone(HEARTBEAT_ZONE_SCORE_PANEL, 255));
    }

    applyHeartbeatZonePulse(activeCount, nowMs);
    applyHeartbeatScoreAccent(activeCount, nowMs);
    applyHeartbeatEventChoreography(activeCount, nowMs);
}

void renderHeartbeatBonusCountdownZones(uint16_t activeCount) {
    fillHeartbeatAllZones(activeCount, HEARTBEAT_ZONE_BACKGROUND_SCALE);

    const bool flashOn = (heartbeatPatternIndex % 2u) == 0u;
    const uint32_t flashColor = flashOn ? heartbeatPixel.Color(255, 120, 0) : heartbeatPixel.Color(32, 8, 0);
    fillHeartbeatZone(HEARTBEAT_ZONE_SCORE_PANEL, activeCount, flashColor);
    fillHeartbeatZone(HEARTBEAT_ZONE_PERFORMER_ROW, activeCount, flashColor);
    fillHeartbeatZone(HEARTBEAT_ZONE_CAPTAIN_RING, activeCount,
                      flashOn ? heartbeatPixel.Color(255, 220, 120) : heartbeatPixel.Color(28, 10, 0));
    applyHeartbeatScoreAccent(activeCount, millis());
    applyHeartbeatEventChoreography(activeCount, millis());
}

void renderHeartbeatGameOverZones(uint16_t activeCount) {
    fillHeartbeatAllZones(activeCount, static_cast<uint8_t>(HEARTBEAT_ZONE_BACKGROUND_SCALE / 2u));

    static constexpr HeartbeatZoneId gameOverSequence[] = {
        HEARTBEAT_ZONE_TITLE_BANNER,
        HEARTBEAT_ZONE_CAPTAIN_RING,
        HEARTBEAT_ZONE_BOTTOM_STAGE,
        HEARTBEAT_ZONE_SCORE_PANEL
    };

    const uint8_t sequenceCount = static_cast<uint8_t>(sizeof(gameOverSequence) / sizeof(gameOverSequence[0]));
    const uint8_t leadIndex = static_cast<uint8_t>(heartbeatPatternIndex % sequenceCount);
    const uint8_t trailIndex = static_cast<uint8_t>((leadIndex + sequenceCount - 1u) % sequenceCount);
    const bool flashOn = (heartbeatPatternIndex % 2u) == 0u;

    fillHeartbeatZone(gameOverSequence[trailIndex], activeCount,
                      flashOn ? heartbeatPixel.Color(120, 12, 0) : heartbeatPixel.Color(48, 6, 0));
    fillHeartbeatZone(gameOverSequence[leadIndex], activeCount,
                      flashOn ? heartbeatPixel.Color(255, 24, 0) : heartbeatPixel.Color(140, 10, 0));
    fillHeartbeatZone(HEARTBEAT_ZONE_CENTER_SPLIT, activeCount,
                      flashOn ? heartbeatPixel.Color(255, 150, 80) : heartbeatPixel.Color(36, 10, 0));
}

void renderHeartbeatPattern() {
    heartbeatPixel.clear();

    const uint16_t activeCount = min<uint16_t>(HEARTBEAT_ACTIVE_LED_COUNT, HEARTBEAT_LED_COUNT);
    if (activeCount == 0) {
        heartbeatPixel.show();
        return;
    }

    if (HEARTBEAT_RENDER_MODE == HEARTBEAT_RENDER_ZONE_MAP) {
        const uint16_t zoneCount = static_cast<uint16_t>(sizeof(HEARTBEAT_ZONE_DEFINITIONS) / sizeof(HEARTBEAT_ZONE_DEFINITIONS[0]));
        const uint16_t zoneIndex = heartbeatPatternIndex % zoneCount;
        const HeartbeatZoneDefinition& zone = HEARTBEAT_ZONE_DEFINITIONS[zoneIndex];
        const uint32_t zoneColor = heartbeatColorForIndex(zoneIndex);

        for (uint8_t segmentOffset = 0; segmentOffset < zone.segmentCount; segmentOffset++) {
            fillHeartbeatSegment(zone.segmentIds[segmentOffset], activeCount, zoneColor);
        }

        heartbeatPixel.show();
        return;
    }

    if (HEARTBEAT_RENDER_MODE == HEARTBEAT_RENDER_SEGMENT_MAP) {
        const uint16_t segmentCount = static_cast<uint16_t>(sizeof(HEARTBEAT_SEGMENT_RANGES) / sizeof(HEARTBEAT_SEGMENT_RANGES[0]));
        const uint16_t segmentIndex = heartbeatPatternIndex % segmentCount;
        fillHeartbeatSegment(static_cast<uint8_t>(segmentIndex), activeCount, heartbeatColorForIndex(segmentIndex));

        heartbeatPixel.show();
        return;
    }

    const uint32_t nowMs = millis();

    switch (gameplayState.mode) {
        case GAME_MODE_BALL_IN_PLAY:
            renderHeartbeatBallInPlayZones(activeCount, nowMs);
            heartbeatPixel.show();
            return;
        case GAME_MODE_SERVE_BALL:
            renderHeartbeatServeBallZones(activeCount);
            heartbeatPixel.show();
            return;
        case GAME_MODE_BONUS_COUNTDOWN:
            renderHeartbeatBonusCountdownZones(activeCount);
            heartbeatPixel.show();
            return;
        case GAME_MODE_GAME_OVER:
            renderHeartbeatGameOverZones(activeCount);
            heartbeatPixel.show();
            return;
        case GAME_MODE_ATTRACT:
        default:
            renderHeartbeatAttractZones(activeCount);
            heartbeatPixel.show();
            return;
    }
}

void initHeartbeat() {
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
        heartbeatPixel.setBrightness(HEARTBEAT_BRIGHTNESS);
        heartbeatPatternIndex = 0;
        renderHeartbeatPattern();
    } else {
        pinMode(HEARTBEAT_PIN, OUTPUT);
        digitalWrite(HEARTBEAT_PIN, LOW);
    }

    heartbeatEnabled = true;
    heartbeatState = false;
    lastHeartbeatToggleMs = millis();
    Serial.printf("Heartbeat enabled on GPIO%u (%s), strip=%u, active=%u, brightness=%u, cap=%lumA\n",
                  HEARTBEAT_PIN,
                  HEARTBEAT_IS_WS2812 ? "WS2812" : "GPIO",
                  HEARTBEAT_LED_COUNT,
                  HEARTBEAT_ACTIVE_LED_COUNT,
                  HEARTBEAT_BRIGHTNESS,
                  static_cast<unsigned long>(HEARTBEAT_MAX_CURRENT_MA));
}

void updateHeartbeat(uint32_t now) {
    if (!heartbeatEnabled) {
        return;
    }

    const uint32_t intervalMs = HEARTBEAT_IS_WS2812
        ? ((HEARTBEAT_RENDER_MODE == HEARTBEAT_RENDER_GAMEPLAY) ? heartbeatIntervalForMode() : HEARTBEAT_INTERVAL_MS)
        : HEARTBEAT_INTERVAL_MS;
    if (now - lastHeartbeatToggleMs >= intervalMs) {
        lastHeartbeatToggleMs = now;
        heartbeatState = !heartbeatState;
        if (HEARTBEAT_IS_WS2812) {
            heartbeatPatternIndex++;
            renderHeartbeatPattern();
        } else {
            digitalWrite(HEARTBEAT_PIN, heartbeatState ? HIGH : LOW);
        }
    }
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
    if (CAPTAIN_AUDIO_GPIO_ONLY_TEST_MODE) {
        pinMode(CAPTAIN_AUDIO_GPIO_ONLY_TEST_PIN, OUTPUT);
        digitalWrite(CAPTAIN_AUDIO_GPIO_ONLY_TEST_PIN, LOW);
        audioGpioOnlyState = false;
        lastAudioGpioToggleMs = millis();
        Serial.printf("Audio GPIO-only test mode active on pin %u (half-period=%u ms)\n",
                      static_cast<unsigned>(CAPTAIN_AUDIO_GPIO_ONLY_TEST_PIN),
                      static_cast<unsigned>(CAPTAIN_AUDIO_GPIO_ONLY_HALF_PERIOD_MS));
        return;
    }

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

void serviceAudioDiagnostics(uint32_t nowMs) {
    if (CAPTAIN_AUDIO_GPIO_ONLY_TEST_MODE) {
        if ((nowMs - lastAudioGpioToggleMs) >= CAPTAIN_AUDIO_GPIO_ONLY_HALF_PERIOD_MS) {
            lastAudioGpioToggleMs = nowMs;
            audioGpioOnlyState = !audioGpioOnlyState;
            digitalWrite(CAPTAIN_AUDIO_GPIO_ONLY_TEST_PIN, audioGpioOnlyState ? HIGH : LOW);
        }
        return;
    }

    if (!CAPTAIN_AUDIO_CONTINUOUS_DIAGNOSTIC || !i2sAudioReady || audioToneQueue == nullptr) {
        return;
    }

    if ((nowMs - lastAudioDiagnosticMs) < CAPTAIN_AUDIO_CONTINUOUS_INTERVAL_MS) {
        return;
    }

    lastAudioDiagnosticMs = nowMs;
    queueTone(440, CAPTAIN_AUDIO_CONTINUOUS_TONE_MS);
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
        const uint32_t nowMs = millis();
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
        startNewGame(nowMs);
        tiltLatched = false;
        currentSW2Mode = false;  // Reset SW2 (Game/Test) to Game on Start
        Serial.println("[START] New game started, tilt cleared, SW2 reset to Game mode");
        queueGameStartFanfare();
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

    if (gameplayState.mode == GAME_MODE_ATTRACT) {
        setHeadboxLamp(pattern, HEADBOX_GAME_OVER, blink);
    } else if (gameplayState.mode == GAME_MODE_GAME_OVER) {
        setHeadboxLamp(pattern, HEADBOX_GAME_OVER, true);
        setHeadboxLamp(pattern, HEADBOX_PLAYER_1, blink);
        setHeadboxLamp(pattern, HEADBOX_BALL_1, blink);
    } else {
        const bool ballLampOn = (gameplayState.mode == GAME_MODE_SERVE_BALL) ? blink : true;

        setHeadboxLamp(pattern, HEADBOX_PLAYER_1, true);
        if (gameplayState.currentBall >= 1 && gameplayState.currentBall <= 5) {
            const CaptainHeadboxLampId ballLamp = static_cast<CaptainHeadboxLampId>(HEADBOX_BALL_1 - (gameplayState.currentBall - 1));
            setHeadboxLamp(pattern, ballLamp, ballLampOn);
        }

        // Single-player MVP uses the unused player lamps as status indicators.
        setHeadboxLamp(pattern, HEADBOX_PLAYER_2, gameplayState.bonusMultiplier >= 2);
        setHeadboxLamp(pattern, HEADBOX_PLAYER_3, gameplayState.bonusMultiplier >= 3);
        setHeadboxLamp(pattern, HEADBOX_PLAYER_4,
                       gameplayState.samePlayerLit && (gameplayState.mode == GAME_MODE_BONUS_COUNTDOWN || blink));
    }

    setHeadboxLamp(pattern, HEADBOX_TILT, tiltLatched);

    return pattern;
}

void addGameplayScore(uint32_t points) {
    gameplayState.score += points;
    displayScore = gameplayState.score;
    triggerHeartbeatScoreAccent(points);
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
    const uint8_t completedTargets =
        static_cast<uint8_t>(gameplayState.target1Complete ? 1 : 0) +
        static_cast<uint8_t>(gameplayState.target2Complete ? 1 : 0) +
        static_cast<uint8_t>(gameplayState.target3Complete ? 1 : 0);
    if (completedTargets >= 3) {
        gameplayState.bonusMultiplier = 3;
    } else if (completedTargets >= 2) {
        gameplayState.bonusMultiplier = 2;
    } else {
        gameplayState.bonusMultiplier = 1;
    }

    if (gameplayState.bonusMultiplier != previousMultiplier) {
        Serial.printf("[GAME] Bonus multiplier -> %ux\n", static_cast<unsigned>(gameplayState.bonusMultiplier));
        if (gameplayState.bonusMultiplier == 2) {
            triggerHeartbeatZonePulse(HEARTBEAT_ZONE_PERFORMER_ROW,
                                      heartbeatColorFromRgb(HEARTBEAT_MILESTONE_FLASH_RGB),
                                      HEARTBEAT_MILESTONE_PULSE_MS);
        } else if (gameplayState.bonusMultiplier == 3) {
            triggerHeartbeatZonePulse(HEARTBEAT_ZONE_ORGAN_AND_ROCKET_TRAIL,
                                      heartbeatColorFromRgb(HEARTBEAT_TARGET_FLASH_RGB),
                                      HEARTBEAT_MILESTONE_PULSE_MS);
            triggerHeartbeatEventChoreography(HEARTBEAT_EVENT_BONUS_X3,
                                              HEARTBEAT_EVENT_STEP_MS * 6u + HEARTBEAT_EVENT_TRAIL_MS);
        }
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
        triggerHeartbeatZonePulse(HEARTBEAT_ZONE_SCORE_PANEL,
                                  heartbeatColorFromRgb(HEARTBEAT_MILESTONE_FLASH_RGB),
                                  HEARTBEAT_MILESTONE_PULSE_MS);
        triggerHeartbeatEventChoreography(HEARTBEAT_EVENT_LANE_SET_COMPLETE,
                                          HEARTBEAT_EVENT_STEP_MS * 6u + HEARTBEAT_EVENT_TRAIL_MS);
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
    gameplayState.serveBallAtMs = 0;
}

void startNewGame(uint32_t nowMs) {
    gameplayState.mode = GAME_MODE_SERVE_BALL;
    gameplayState.score = 0;
    gameplayState.currentBall = 1;
    resetGameplayStateForNewBall();
    gameplayState.serveBallAtMs = nowMs + GAME_START_SERVE_DELAY_MS;
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
            if (gameplayState.serveBallAtMs != 0 && nowMs < gameplayState.serveBallAtMs) {
                break;
            }
            Serial.printf("[GAME] Serving ball %u\n", static_cast<unsigned>(gameplayState.currentBall));
            fireSolenoid(SOLENOID_S2);
            gameplayState.ballInPlay = true;
            gameplayState.serveBallAtMs = 0;
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
                queueBonusCountTone();
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
            queueDrainTone();
            startBonusCountdown(nowMs);
        }
        return;
    }

    if (gameplayState.mode != GAME_MODE_BALL_IN_PLAY || !gameplayState.ballInPlay) {
        return;
    }

    const size_t switchIndex = captainSwitchBitIndex(row, col);
    const uint32_t retriggerMs = gameplaySwitchRetriggerMs(row, col);
    if (retriggerMs != 0 && lastGameplaySwitchHitMs[switchIndex] != 0 &&
        (nowMs - lastGameplaySwitchHitMs[switchIndex]) < retriggerMs) {
        return;
    }
    lastGameplaySwitchHitMs[switchIndex] = nowMs;

    triggerHeartbeatPulseProfile(heartbeatPulseProfileForSwitch(row, col));

    switch (row) {
        case 0:
            if (col == 2) {
                addGameplayScore(100);
                queueScoringTone(100);
                logGameplayAward("Spinner R", 100, 0);
            } else if (col == 3) {
                addGameplayScore(500);
                addGameplayBonus(1000);
                queueScoringTone(500);
                logGameplayAward("Return Lane R", 500, 1000);
            }
            break;
        case 1:
            if (col == 0) {
                addGameplayScore(1000);
                addGameplayBonus(1000);
                gameplayState.laneAComplete = true;
                updateGameplayLaneCompletion();
                queueScoringTone(1000);
                logGameplayAward("Lane A", 1000, 1000);
            } else if (col == 2) {
                addGameplayScore(500);
                addGameplayBonus(2000);
                gameplayState.target1Complete = true;
                updateGameplayBonusMultiplier();
                queueScoringTone(500);
                logGameplayAward("Target 1", 500, 2000);
            } else if (col == 3) {
                addGameplayScore(100);
                if (MATRIX_SWITCH_SOLENOIDS_ENABLED) {
                    fireSolenoid(SOLENOID_S3);
                }
                queueSlingshotTone();
                logGameplayAward("Slingshot L", 100, 0);
            }
            break;
        case 2:
            if (col == 0) {
                addGameplayScore(1000);
                addGameplayBonus(1000);
                gameplayState.laneBComplete = true;
                updateGameplayLaneCompletion();
                queueScoringTone(1000);
                logGameplayAward("Lane B", 1000, 1000);
            } else if (col == 1) {
                addGameplayScore(100);
                if (MATRIX_SWITCH_SOLENOIDS_ENABLED) {
                    fireSolenoid(SOLENOID_S5);
                }
                queueBumperTone();
                logGameplayAward("Bumper L", 100, 0);
            } else if (col == 2) {
                addGameplayScore(50);
                addGameplayBonus(1000);
                queueScoringTone(50);
                logGameplayAward("Side Switch 1", 50, 1000);
            } else if (col == 3) {
                addGameplayScore(500);
                addGameplayBonus(1000);
                queueScoringTone(500);
                logGameplayAward("Return Lane L", 500, 1000);
            }
            break;
        case 3:
            if (col == 0) {
                addGameplayScore(1000);
                addGameplayBonus(1000);
                gameplayState.laneCComplete = true;
                updateGameplayLaneCompletion();
                queueScoringTone(1000);
                logGameplayAward("Lane C", 1000, 1000);
            } else if (col == 1) {
                addGameplayScore(100);
                if (MATRIX_SWITCH_SOLENOIDS_ENABLED) {
                    fireSolenoid(SOLENOID_S6);
                }
                queueBumperTone();
                logGameplayAward("Bumper R", 100, 0);
            } else if (col == 2) {
                addGameplayScore(100);
                queueScoringTone(100);
                logGameplayAward("Spinner L", 100, 0);
            } else if (col == 3) {
                addGameplayScore(500);
                addGameplayBonus(1000);
                queueScoringTone(500);
                logGameplayAward("Bonus Lane L", 500, 1000);
            }
            break;
        case 4:
            if (col == 0) {
                addGameplayScore(1000);
                addGameplayBonus(1000);
                gameplayState.laneDComplete = true;
                updateGameplayLaneCompletion();
                queueScoringTone(1000);
                logGameplayAward("Lane D", 1000, 1000);
            } else if (col == 2) {
                addGameplayScore(500);
                addGameplayBonus(2000);
                gameplayState.target2Complete = true;
                updateGameplayBonusMultiplier();
                queueScoringTone(500);
                logGameplayAward("Target 2", 500, 2000);
            } else if (col == 3) {
                addGameplayScore(100);
                if (MATRIX_SWITCH_SOLENOIDS_ENABLED) {
                    fireSolenoid(SOLENOID_S4);
                }
                queueSlingshotTone();
                logGameplayAward("Slingshot R", 100, 0);
            }
            break;
        case 5:
            if (col == 0) {
                addGameplayScore(1000);
                addGameplayBonus(2000);
                gameplayState.target3Complete = true;
                updateGameplayBonusMultiplier();
                queueScoringTone(1000);
                logGameplayAward("Target 3", 1000, 2000);
            } else if (col == 2) {
                addGameplayScore(50);
                addGameplayBonus(1000);
                queueScoringTone(50);
                logGameplayAward("Side Switch 2", 50, 1000);
            } else if (col == 3) {
                addGameplayScore(500);
                addGameplayBonus(1000);
                queueScoringTone(500);
                logGameplayAward("Bonus Lane R", 500, 1000);
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

    if (!gameplayState.laneAComplete) {
        lampRows[1] |= captainMatrixLampRowMask(1);
    }
    if (!gameplayState.laneBComplete) {
        lampRows[2] |= captainMatrixLampRowMask(1);
    }
    if (!gameplayState.laneCComplete) {
        lampRows[3] |= captainMatrixLampRowMask(1);
    }
    if (!gameplayState.laneDComplete) {
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
                        if (event.delayBeforeMs != 0) {
                            vTaskDelay(pdMS_TO_TICKS(event.delayBeforeMs));
                        }
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
                serviceAudioDiagnostics(now);
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

                        uint8_t switchBits[CAPTAIN_SWITCH_BYTES] = {};
                        const bool readDue = (now - lastMatrixSwitchReadMs) >= MATRIX_SWITCH_READ_INTERVAL_MS;
                        bool writeOk = true;
                        bool readOk = true;
                        if (readDue) {
                            lastMatrixSwitchReadMs = now;
                            readOk = readMatrixSwitches(switchBits);
                        } else {
                            writeOk = writeMatrixCommand(now);
                        }

                        if (!readDue) {
                            if (writeOk) {
                                matrixWriteOkCount++;
                            } else {
                                matrixWriteFailCount++;
                            }
                        }
                        if (readOk && readDue) {
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
                            if (readDue) {
                                matrixReadFailCount++;
                            }
                        }
                        if (((!readDue && writeOk) || (readDue && readOk)) && !matrixLinkFaulted) {
                            if (readDue) {
                                handleSwitchEdges(switchBits);
                            }
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
