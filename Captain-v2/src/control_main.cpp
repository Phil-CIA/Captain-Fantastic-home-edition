#include <Arduino.h>
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
#include "displays.h"

namespace {
constexpr uint32_t POLL_MS = 15;
constexpr uint32_t DIRECT_INPUT_POLL_MS = 5;
constexpr uint8_t DIRECT_INPUT_DEBOUNCE_TICKS = 3;
constexpr uint8_t HEARTBEAT_PIN = 2;
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 500;

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
uint32_t lastAudioDiagnosticMs = 0;
bool audioDiagnosticToneFlip = false;
uint32_t lastAudioGpioPulseToggleMs = 0;
bool audioGpioPulseState = false;
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

void updateHeadboxLamps(uint16_t pattern);

bool matrixWriteCommandByte(uint8_t value) {
    Wire.beginTransmission(CAPTAIN_MATRIX_I2C_ADDRESS);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool matrixWriteRegisters(uint8_t startRegister, const uint8_t* data, size_t len) {
    Wire.beginTransmission(CAPTAIN_MATRIX_I2C_ADDRESS);
    Wire.write(startRegister);
    Wire.write(data, len);
    return Wire.endTransmission() == 0;
}

bool matrixReadRegisters(uint8_t startRegister, uint8_t* out, size_t len) {
    Wire.beginTransmission(CAPTAIN_MATRIX_I2C_ADDRESS);
    Wire.write(startRegister);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }

    const size_t received = Wire.requestFrom(static_cast<int>(CAPTAIN_MATRIX_I2C_ADDRESS), static_cast<int>(len));
    if (received != len) {
        while (Wire.available()) {
            Wire.read();
        }
        return false;
    }

    for (size_t index = 0; index < len; index++) {
        out[index] = static_cast<uint8_t>(Wire.read());
    }
    return true;
}

void initMatrixDevice() {
    const bool pulseOk = matrixWriteCommandByte(static_cast<uint8_t>(CAPTAIN_MATRIX_CMD_PULSE_WIDTH_BASE | CAPTAIN_MATRIX_DEFAULT_PULSE_WIDTH_LEVEL));
    const bool systemOk = matrixWriteCommandByte(static_cast<uint8_t>(CAPTAIN_MATRIX_CMD_SYSTEM_SETUP | CAPTAIN_MATRIX_CMD_SYSTEM_ENABLE));
    const bool outputOk = matrixWriteCommandByte(static_cast<uint8_t>(CAPTAIN_MATRIX_CMD_OUTPUT_SETUP | CAPTAIN_MATRIX_CMD_OUTPUT_ENABLE));
    matrixDeviceReady = pulseOk && systemOk && outputOk;

    if (matrixDeviceReady) {
        Serial.printf("Matrix device ready at 0x%02X (pulseLevel=%u)\n",
                      CAPTAIN_MATRIX_I2C_ADDRESS,
                      static_cast<unsigned>(CAPTAIN_MATRIX_DEFAULT_PULSE_WIDTH_LEVEL));
    } else {
        Serial.printf("Matrix device init failed at 0x%02X (pulse=%u system=%u output=%u)\n",
                      CAPTAIN_MATRIX_I2C_ADDRESS,
                      pulseOk ? 1u : 0u,
                      systemOk ? 1u : 0u,
                      outputOk ? 1u : 0u);
    }
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

void runAudioPinSanityPulseTest() {
    if (!CAPTAIN_AUDIO_PIN_SANITY_TEST) {
        return;
    }

    Serial.println("Audio pin sanity: pulsing GPIO13 (LRCLK label)");
    pinMode(CAPTAIN_AUDIO_LRCK_PIN, OUTPUT);
    for (uint16_t i = 0; i < CAPTAIN_AUDIO_PIN_SANITY_PULSES; i++) {
        digitalWrite(CAPTAIN_AUDIO_LRCK_PIN, HIGH);
        delay(CAPTAIN_AUDIO_PIN_SANITY_PULSE_MS);
        digitalWrite(CAPTAIN_AUDIO_LRCK_PIN, LOW);
        delay(CAPTAIN_AUDIO_PIN_SANITY_PULSE_MS);
    }

    Serial.println("Audio pin sanity: pulsing GPIO14 (BCLK label)");
    pinMode(CAPTAIN_AUDIO_BCLK_PIN, OUTPUT);
    for (uint16_t i = 0; i < CAPTAIN_AUDIO_PIN_SANITY_PULSES; i++) {
        digitalWrite(CAPTAIN_AUDIO_BCLK_PIN, HIGH);
        delay(CAPTAIN_AUDIO_PIN_SANITY_PULSE_MS);
        digitalWrite(CAPTAIN_AUDIO_BCLK_PIN, LOW);
        delay(CAPTAIN_AUDIO_PIN_SANITY_PULSE_MS);
    }

    Serial.println("Audio pin sanity: complete");
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

void runExternalFlashSelfTest() {
    if (!CAPTAIN_FLASH_ENABLE_RW_SELF_TEST) {
        return;
    }

    Serial.printf("External flash self-test sector: 0x%06lX\n", static_cast<unsigned long>(CAPTAIN_FLASH_TEST_SECTOR_ADDR));

    const uint8_t writePattern[16] = {
        0xC1, 0x2A, 0x7F, 0x99,
        0x10, 0x55, 0xAA, 0x3C,
        0xD4, 0xE2, 0x19, 0x68,
        0x77, 0x42, 0x0B, 0xF0
    };
    uint8_t readBack[16] = {};

    if (!flashEraseSector4K(CAPTAIN_FLASH_TEST_SECTOR_ADDR)) {
        Serial.println("External flash self-test FAILED: erase timeout");
        return;
    }

    if (!flashProgramPage(CAPTAIN_FLASH_TEST_DATA_ADDR, writePattern, sizeof(writePattern))) {
        Serial.println("External flash self-test FAILED: program timeout");
        return;
    }

    flashReadBytes(CAPTAIN_FLASH_TEST_DATA_ADDR, readBack, sizeof(readBack));
    if (memcmp(writePattern, readBack, sizeof(writePattern)) != 0) {
        Serial.println("External flash self-test FAILED: verify mismatch");
        return;
    }

    if (!flashEraseSector4K(CAPTAIN_FLASH_TEST_SECTOR_ADDR)) {
        Serial.println("External flash self-test WARNING: cleanup erase timeout");
        return;
    }

    Serial.println("External flash self-test PASSED (erase/program/read/verify/cleanup)");
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
        runExternalFlashSelfTest();
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

void runI2SDiagnostics() {
    if (!i2sAudioReady) {
        Serial.println("Audio diagnostics: I2S not ready");
        return;
    }

    const uint32_t beforeCalls = i2sWriteCalls;
    const uint32_t beforeErrors = i2sWriteErrors;
    const uint32_t beforeBytes = i2sBytesWrittenTotal;

    Serial.printf("Audio diagnostics: start (sampleRate=%luHz LRCK~%luHz)\n",
                  static_cast<unsigned long>(CAPTAIN_AUDIO_SAMPLE_RATE),
                  static_cast<unsigned long>(CAPTAIN_AUDIO_SAMPLE_RATE));

    playI2STestTone(1000, 600);
    delay(50);
    playI2STestTone(700, 600);

    const uint32_t deltaCalls = i2sWriteCalls - beforeCalls;
    const uint32_t deltaErrors = i2sWriteErrors - beforeErrors;
    const uint32_t deltaBytes = i2sBytesWrittenTotal - beforeBytes;

    Serial.printf("Audio diagnostics: tx calls=%lu bytes=%lu errors=%lu\n",
                  static_cast<unsigned long>(deltaCalls),
                  static_cast<unsigned long>(deltaBytes),
                  static_cast<unsigned long>(deltaErrors));
    Serial.println("Audio diagnostics: if bytes>0 and errors=0, ESP32 is transmitting I2S data to amp DIN/BCLK/LRCK pins");
}

void updateContinuousAudioDiagnostic(uint32_t now) {
    if (!CAPTAIN_AUDIO_CONTINUOUS_DIAGNOSTIC || !i2sAudioReady) {
        return;
    }

    if (now - lastAudioDiagnosticMs < CAPTAIN_AUDIO_CONTINUOUS_INTERVAL_MS) {
        return;
    }
    lastAudioDiagnosticMs = now;

    audioDiagnosticToneFlip = !audioDiagnosticToneFlip;
    const uint16_t frequency = audioDiagnosticToneFlip ? 1000 : 700;
    playI2STestTone(frequency, CAPTAIN_AUDIO_CONTINUOUS_TONE_MS);
}

void updateAudioGpioOnlyPulseTest(uint32_t now) {
    if (!CAPTAIN_AUDIO_GPIO_ONLY_TEST_MODE) {
        return;
    }

    if (now - lastAudioGpioPulseToggleMs < CAPTAIN_AUDIO_GPIO_ONLY_HALF_PERIOD_MS) {
        return;
    }

    lastAudioGpioPulseToggleMs = now;
    audioGpioPulseState = !audioGpioPulseState;
    digitalWrite(CAPTAIN_AUDIO_GPIO_ONLY_TEST_PIN, audioGpioPulseState ? HIGH : LOW);
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
        pinMode(CAPTAIN_SOLENOID_PINS[index], OUTPUT);
        digitalWrite(CAPTAIN_SOLENOID_PINS[index], LOW);
    }
}

void fireSolenoid(CaptainSolenoidId solenoidId) {
    if (solenoidId >= SOLENOID_COUNT) {
        return;
    }

    const uint8_t pin = CAPTAIN_SOLENOID_PINS[solenoidId];
    digitalWrite(pin, HIGH);
    solenoidActive[solenoidId] = true;
    solenoidStartedAtMs[solenoidId] = millis();
    Serial.printf("Solenoid fired: %s (GPIO %u)\n", CAPTAIN_SOLENOID_NAMES[solenoidId], pin);
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

void onDirectInputPressed(CaptainDirectInputId inputId) {
    Serial.printf("Direct input pressed: %s (GPIO %u)\n", CAPTAIN_DIRECT_INPUT_NAMES[inputId], CAPTAIN_DIRECT_INPUT_PINS[inputId]);

    if (inputId == DIRECT_INPUT_START) {
        displayScore = 0;
        tiltLatched = false;
        playI2STestTone(880, 80);
    } else if (inputId == DIRECT_INPUT_TILT) {
        tiltLatched = true;
        playI2STestTone(220, 120);
    } else if (inputId == DIRECT_INPUT_SW1) {
        displayScore += 100;
        fireSolenoid(SOLENOID_S5);
        playI2STestTone(660, 60);
    } else if (inputId == DIRECT_INPUT_SW2) {
        displayScore += 100;
        fireSolenoid(SOLENOID_S6);
        playI2STestTone(740, 60);
    }
}

void onDirectInputReleased(CaptainDirectInputId inputId) {
    Serial.printf("Direct input released: %s (GPIO %u)\n", CAPTAIN_DIRECT_INPUT_NAMES[inputId], CAPTAIN_DIRECT_INPUT_PINS[inputId]);
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
    for (uint8_t row = 0; row < CAPTAIN_SWITCH_ROWS; row++) {
        for (uint8_t col = 0; col < CAPTAIN_SWITCH_COLS; col++) {
            const size_t bit = captainSwitchBitIndex(row, col);
            const bool previous = captainGetBit(previousSwitchBits, bit);
            const bool current = captainGetBit(switchBits, bit);
            if (!previous && current) {
                const uint32_t points = scoreForSwitch(row, col);
                displayScore += points;
                Serial.printf("Switch M%u/SW%u: %s (+%lu)\n", row + 1, col + 1, captainSwitchName(row, col), static_cast<unsigned long>(points));

                if (row == 0 && col == 0) {
                    fireSolenoid(SOLENOID_S2);
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

void writeMatrixCommand() {
    uint8_t lampRows[CAPTAIN_LAMP_ROWS] = {};
    lampRows[1] |= captainMatrixLampRowMask(1);
    lampRows[2] |= captainMatrixLampRowMask(1);
    lampRows[3] |= captainMatrixLampRowMask(1);

    const bool blink = ((millis() / 350) % 2) != 0;
    if (blink) {
        lampRows[4] |= captainMatrixLampRowMask(1);
        lampRows[2] |= captainMatrixLampRowMask(3);
        lampRows[5] |= captainMatrixLampRowMask(3);
    }

    const bool writeOk = matrixWriteRegisters(CAPTAIN_MATRIX_REG_LAMP_BASE, lampRows, sizeof(lampRows));
    if (!writeOk) {
        matrixDeviceReady = false;
    }
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
    initDirectInputs();
    initHeartbeat();
    if (CAPTAIN_AUDIO_GPIO_ONLY_TEST_MODE) {
        pinMode(CAPTAIN_AUDIO_GPIO_ONLY_TEST_PIN, OUTPUT);
        digitalWrite(CAPTAIN_AUDIO_GPIO_ONLY_TEST_PIN, LOW);
        Serial.printf("Audio GPIO-only test mode enabled: pulsing GPIO%u (half-period=%ums)\n",
                      CAPTAIN_AUDIO_GPIO_ONLY_TEST_PIN,
                      static_cast<unsigned>(CAPTAIN_AUDIO_GPIO_ONLY_HALF_PERIOD_MS));
    } else {
        runAudioPinSanityPulseTest();
        initI2SAudio();
        playStartupMelody();
        runI2SDiagnostics();
        if (CAPTAIN_AUDIO_CONTINUOUS_DIAGNOSTIC) {
            Serial.println("Audio diagnostics: continuous test mode enabled");
        }
    }
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
}

void loop() {
    static uint32_t lastPoll = 0;
    const uint32_t now = millis();
    updateHeartbeat(now);
    updateSolenoidPulses(now);
    processDirectInputs(now);

    if (otaReady) {
        ArduinoOTA.handle();
    }

    if (otaInProgress) {
        updateOtaVisual(now);
        return;
    }

    if (CAPTAIN_AUDIO_GPIO_ONLY_TEST_MODE) {
        updateAudioGpioOnlyPulseTest(now);
    } else {
        updateContinuousAudioDiagnostic(now);
    }

    if (now - lastPoll >= POLL_MS) {
        lastPoll = now;
        if (CAPTAIN_HEADBOX_ATTRACT_LOOP) {
            headboxPattern = updateHeadboxAttractLoop(now);
            updateHeadboxLamps(headboxPattern);
        } else {
            if (!matrixDeviceReady) {
                initMatrixDevice();
            }

            writeMatrixCommand();

            uint8_t switchBits[CAPTAIN_SWITCH_BYTES] = {};
            if (readMatrixSwitches(switchBits)) {
                handleSwitchEdges(switchBits);
                matrixDeviceReady = true;
            } else {
                matrixDeviceReady = false;
            }

            const bool blink = ((now / 350) % 2) != 0;
            headboxPattern = composeHeadboxPattern(displayScore, blink);
            updateHeadboxLamps(headboxPattern);
        }
    }

    if (now - lastDisplayUpdate >= 100) {
        lastDisplayUpdate = now;
        updateLEDScore(displayScore);
    }
}
