#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
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
constexpr uint32_t POLL_MS = 15;
constexpr uint32_t DIRECT_INPUT_POLL_MS = 5;
constexpr uint8_t DIRECT_INPUT_DEBOUNCE_TICKS = 3;
constexpr uint8_t HEARTBEAT_PIN = 16;
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 500;
constexpr uint32_t MATRIX_DIAG_POLL_MS = 250;
constexpr uint32_t MATRIX_LINK_TIMEOUT_MS = 1000;
constexpr uint32_t MATRIX_LINK_SUMMARY_MS = 1000;
constexpr uint32_t MATRIX_INIT_RETRY_MS = 1000;
constexpr uint32_t MATRIX_SWITCH_LOG_DEBOUNCE_MS = 250;
constexpr uint32_t MATRIX_SWITCH_LOG_REPORT_MS = 1000;
constexpr uint16_t MATRIX_SWITCH_LOG_MAX_PER_REPORT = 12;
// Diagnostic mode: force matrix lamps off to isolate switch mapping from lamp-scan coupling.
// Keep disabled for normal bring-up; when enabled it intentionally drives lamp frames to all-zero.
constexpr bool MATRIX_SWITCH_MAPPING_MODE = false;
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

uint32_t displayScore = 0;
uint32_t lastDisplayUpdate = 0;
uint8_t previousSwitchBits[CAPTAIN_SWITCH_BYTES] = {};
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
uint8_t lastMatrixSwitch0 = 0;
uint8_t lastMatrixSwitch1 = 0;
uint8_t lastMatrixSwitch2 = 0;
uint8_t lastMatrixSwitch3 = 0;
bool matrixSwitch0Seen = false;
bool matrixDiagFaulted = false;
uint32_t matrixSwitchLogSuppressedDebounce = 0;
uint32_t matrixSwitchLogSuppressedRate = 0;
uint32_t matrixSwitchSuppressedBurst = 0;
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
QueueHandle_t audioToneQueue = nullptr;
TaskHandle_t controlTaskHandle = nullptr;
TaskHandle_t audioTaskHandle = nullptr;
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
    recordMatrixTransactionResult(ok);
    return ok;
}

bool matrixReadRegisters(uint8_t startRegister, uint8_t* out, size_t len) {
    Wire.beginTransmission(CAPTAIN_MATRIX_I2C_ADDRESS);
    Wire.write(startRegister);
    if (Wire.endTransmission(false) != 0) {
        recordMatrixTransactionResult(false);
        return false;
    }

    const size_t received = Wire.requestFrom(static_cast<int>(CAPTAIN_MATRIX_I2C_ADDRESS), static_cast<int>(len));
    if (received != len) {
        while (Wire.available()) {
            Wire.read();
        }
        recordMatrixTransactionResult(false);
        return false;
    }

    for (size_t index = 0; index < len; index++) {
        out[index] = static_cast<uint8_t>(Wire.read());
    }
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
    Serial.printf("Matrix link: ready=%u fault=%u wr_ok=%lu wr_fail=%lu rd_ok=%lu rd_fail=%lu diag_warn=%lu sw0=0x%02X sw1=0x%02X sw2=0x%02X sw3=0x%02X sw_edges=%u sw_log=%u sup_db=%lu sup_rate=%lu s2_fire=%u s2_cd=%lu s2_win=%lu s2_ball=%lu\n",
                  matrixDeviceReady ? 1u : 0u,
                  matrixLinkFaulted ? 1u : 0u,
                  static_cast<unsigned long>(matrixWriteOkCount),
                  static_cast<unsigned long>(matrixWriteFailCount),
                  static_cast<unsigned long>(matrixReadOkCount),
                  static_cast<unsigned long>(matrixReadFailCount),
                  static_cast<unsigned long>(matrixDiagWarnCount),
                  static_cast<unsigned>(lastMatrixSwitch0),
                  static_cast<unsigned>(lastMatrixSwitch1),
                  static_cast<unsigned>(lastMatrixSwitch2),
                  static_cast<unsigned>(lastMatrixSwitch3),
                  static_cast<unsigned>(matrixSwitchEdgesThisWindow),
                  static_cast<unsigned>(matrixSwitchLoggedThisWindow),
                  static_cast<unsigned long>(matrixSwitchLogSuppressedDebounce),
                  static_cast<unsigned long>(matrixSwitchLogSuppressedRate),
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
    const bool conflictsHeadboxShiftRegister =
        (HEARTBEAT_PIN == static_cast<uint8_t>(CAPTAIN_HEADBOX_595_DATA_PIN)) ||
        (HEARTBEAT_PIN == static_cast<uint8_t>(CAPTAIN_HEADBOX_595_CLOCK_PIN)) ||
        (HEARTBEAT_PIN == static_cast<uint8_t>(CAPTAIN_HEADBOX_595_LATCH_PIN));

    if (conflictsHeadboxShiftRegister) {
        heartbeatEnabled = false;
        Serial.printf("Heartbeat disabled: GPIO%u shared with headbox 74HC595\n", HEARTBEAT_PIN);
        return;
    }

    pinMode(HEARTBEAT_PIN, OUTPUT);
    digitalWrite(HEARTBEAT_PIN, LOW);
    heartbeatEnabled = true;
    heartbeatState = false;
    lastHeartbeatToggleMs = millis();
    Serial.printf("Heartbeat enabled on GPIO%u\n", HEARTBEAT_PIN);
}

void updateHeartbeat(uint32_t now) {
    if (!heartbeatEnabled) {
        return;
    }

    if (now - lastHeartbeatToggleMs >= HEARTBEAT_INTERVAL_MS) {
        lastHeartbeatToggleMs = now;
        heartbeatState = !heartbeatState;
        digitalWrite(HEARTBEAT_PIN, heartbeatState ? HIGH : LOW);
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
        // Write LOW to the output latch before enabling OUTPUT to avoid drive glitch
        digitalWrite(CAPTAIN_SOLENOID_PINS[index], LOW);
        pinMode(CAPTAIN_SOLENOID_PINS[index], OUTPUT);
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

        if (now - solenoidStartedAtMs[index] >= CAPTAIN_SOLENOID_PULSE_MS[index]) {
            digitalWrite(CAPTAIN_SOLENOID_PINS[index], LOW);
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
        displayScore = 0;
        tiltLatched = false;
        currentSW2Mode = false;  // Reset SW2 (Game/Test) to Game on Start
        resetS2LimiterForNewBall(millis());
        Serial.println("[START] Score reset, tilt cleared, S2 limiter reset, SW2 reset to Game mode");
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
    uint16_t pattern = 0;

    const uint8_t ballIndex = static_cast<uint8_t>((score / 1000) % 5);
    setHeadboxLamp(pattern, static_cast<CaptainHeadboxLampId>(HEADBOX_BALL_1 + ballIndex), true);

    const uint8_t playerIndex = static_cast<uint8_t>((score / 5000) % 4);
    setHeadboxLamp(pattern, static_cast<CaptainHeadboxLampId>(HEADBOX_PLAYER_1 + playerIndex), true);

    setHeadboxLamp(pattern, HEADBOX_TILT, tiltLatched || blink);
    setHeadboxLamp(pattern, HEADBOX_GAME_OVER, score == 0);

    return pattern;
}

uint32_t scoreForSwitch(uint8_t row, uint8_t col) {
    if (row == 0 && col == 2) return 100;
    if (row == 0 && col == 3) return 500;
    if (row == 1 && col == 0) return 1000;
    if (row == 1 && col == 2) return 50;
    if (row == 1 && col == 3) return 100;
    if (row == 2 && col == 0) return 1000;
    if (row == 2 && col == 1) return 100;
    if (row == 2 && col == 2) return 50;
    if (row == 2 && col == 3) return 500;
    if (row == 3 && col == 0) return 1000;
    if (row == 3 && col == 1) return 100;
    if (row == 3 && col == 2) return 100;
    if (row == 3 && col == 3) return 500;
    if (row == 4 && col == 0) return 1000;
    if (row == 4 && col == 2) return 50;
    if (row == 4 && col == 3) return 100;
    if (row == 5 && col == 0) return 50;
    if (row == 5 && col == 2) return 50;
    if (row == 5 && col == 3) return 500;
    return 0;
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

    // Ignore impossible bursts; they are scan/bus transients that would otherwise score and fire coils.
    if (risingEdgesThisPoll > MATRIX_MAX_RISING_EDGES_PER_POLL) {
        matrixSwitchSuppressedBurst++;
        return;
    }

    for (uint8_t row = 0; row < CAPTAIN_SWITCH_ROWS; row++) {
        for (uint8_t col = 0; col < CAPTAIN_SWITCH_COLS; col++) {
            const size_t bit = captainSwitchBitIndex(row, col);
            const bool previous = captainGetBit(previousSwitchBits, bit);
            const bool current = captainGetBit(switchBits, bit);
            if (!previous && current) {
                matrixSwitchEdgesThisWindow++;
                const uint32_t points = scoreForSwitch(row, col);
                displayScore += points;

                if (row == 0 && col == 0) {
                    tryFireS2WithLimits(nowMs);
                } else if (row == 2 && col == 1) {
                    fireSolenoid(SOLENOID_S3);
                } else if (row == 3 && col == 1) {
                    fireSolenoid(SOLENOID_S4);
                } else if (row == 1 && col == 3) {
                    fireSolenoid(SOLENOID_S5);
                } else if (row == 4 && col == 3) {
                    fireSolenoid(SOLENOID_S6);
                }
            }
        }
    }

    memcpy(previousSwitchBits, switchBits, sizeof(previousSwitchBits));
}

bool readMatrixSwitches(uint8_t* switchBits) {
    return matrixReadRegisters(CAPTAIN_MATRIX_REG_SWITCH_BASE, switchBits, CAPTAIN_SWITCH_BYTES);
}

bool readMatrixDiagnostics(uint8_t* diagBytes) {
    return matrixReadRegisters(CAPTAIN_MATRIX_REG_DIAG_BASE, diagBytes, CAPTAIN_MATRIX_REG_DIAG_END - CAPTAIN_MATRIX_REG_DIAG_BASE + 1);
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

bool writeMatrixCommand(uint32_t now) {
    uint8_t lampRows[CAPTAIN_LAMP_ROWS] = {};

    if (MATRIX_SWITCH_MAPPING_MODE) {
        memset(lampRows, 0, sizeof(lampRows));
    } else if (CAPTAIN_MATRIX_ATTRACT_ENABLED) {
        computeMatrixAttractFrame(now, lampRows);
    } else {
        // Static bring-up test pattern: L2, L7, L10, L20
        lampRows[2] |= captainMatrixLampRowMask(1);
        lampRows[2] |= captainMatrixLampRowMask(3);
        lampRows[3] |= captainMatrixLampRowMask(3);
        lampRows[4] |= captainMatrixLampRowMask(4);
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
    }

    return writeOk;
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

    initSolenoids();
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
                            matrixReadOkCount++;
                            lastMatrixSwitch0 = switchBits[0];
                            lastMatrixSwitch1 = switchBits[1];
                            lastMatrixSwitch2 = switchBits[2];
                            lastMatrixSwitch3 = switchBits[3];
                            matrixSwitch0Seen = true;
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
                    updateLEDScore(displayScore);
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
