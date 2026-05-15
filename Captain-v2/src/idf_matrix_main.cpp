#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"

namespace {
static const char* TAG = "captain_matrix_idf";

constexpr uint8_t MATRIX_ROWS = 8;
constexpr uint8_t MATRIX_COLS = 4;

// Matches May 12 matrix bring-up wiring used by the shared mapping.
constexpr gpio_num_t ROW_PINS[MATRIX_ROWS] = {
    GPIO_NUM_0, GPIO_NUM_1, GPIO_NUM_2, GPIO_NUM_3,
    GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_7
};

constexpr gpio_num_t COL_PINS[MATRIX_COLS] = {
    GPIO_NUM_20, GPIO_NUM_18, GPIO_NUM_21, GPIO_NUM_19
};

constexpr gpio_num_t SR_DATA_PIN = GPIO_NUM_15;
constexpr gpio_num_t SR_CLOCK_PIN = GPIO_NUM_22;
constexpr gpio_num_t SR_LATCH_PIN = GPIO_NUM_23;

constexpr uint8_t SR_BIT_COL0 = 0;
constexpr uint8_t SR_BIT_COL1 = 1;
constexpr uint8_t SR_BIT_COL2 = 2;
constexpr uint8_t SR_BIT_COL3 = 3;
constexpr uint8_t SR_BIT_COL4 = 4;

constexpr uint8_t LAMP_ROWS = 8;
constexpr uint8_t LAMP_COLS = 5;
constexpr uint8_t LAMP_BYTES = (LAMP_ROWS * LAMP_COLS + 7) / 8;
constexpr gpio_num_t STATUS_RGB_PIN = GPIO_NUM_8;
constexpr uint32_t STATUS_HEARTBEAT_MS = 500;
constexpr uint32_t SERIAL_HEARTBEAT_MS = 500;
constexpr uint8_t STATUS_ON_GREEN = 64;

uint8_t lampBits[LAMP_BYTES] = {};
rmt_channel_handle_t statusLedChannel = nullptr;
rmt_encoder_handle_t statusLedEncoder = nullptr;
bool statusLedOn = false;
uint32_t statusLastHeartbeatMs = 0;
bool serialHeartbeatOn = false;
uint32_t serialHeartbeatLastMs = 0;

inline size_t lampBitIndex(uint8_t row, uint8_t col) {
    return static_cast<size_t>(row) * LAMP_COLS + col;
}

inline bool getBit(const uint8_t* buffer, size_t bitIndex) {
    const size_t byteIndex = bitIndex / 8;
    const uint8_t mask = static_cast<uint8_t>(1u << (bitIndex % 8));
    return (buffer[byteIndex] & mask) != 0;
}

inline void setBit(uint8_t* buffer, size_t bitIndex, bool state) {
    const size_t byteIndex = bitIndex / 8;
    const uint8_t mask = static_cast<uint8_t>(1u << (bitIndex % 8));
    if (state) {
        buffer[byteIndex] |= mask;
    } else {
        buffer[byteIndex] &= static_cast<uint8_t>(~mask);
    }
}

void writeShiftRegister16(uint16_t value) {
    gpio_set_level(SR_LATCH_PIN, 0);
    for (int8_t bit = 15; bit >= 0; bit--) {
        gpio_set_level(SR_CLOCK_PIN, 0);
        const bool state = (value & (1u << bit)) != 0;
        gpio_set_level(SR_DATA_PIN, state ? 1 : 0);
        gpio_set_level(SR_CLOCK_PIN, 1);
    }
    gpio_set_level(SR_LATCH_PIN, 1);
}

uint16_t composeLampColumnShiftValue(uint8_t row) {
    uint16_t value = 0;
    if (getBit(lampBits, lampBitIndex(row, 0))) value |= static_cast<uint16_t>(1u << SR_BIT_COL0);
    if (getBit(lampBits, lampBitIndex(row, 1))) value |= static_cast<uint16_t>(1u << SR_BIT_COL1);
    if (getBit(lampBits, lampBitIndex(row, 2))) value |= static_cast<uint16_t>(1u << SR_BIT_COL2);
    if (getBit(lampBits, lampBitIndex(row, 3))) value |= static_cast<uint16_t>(1u << SR_BIT_COL3);
    if (getBit(lampBits, lampBitIndex(row, 4))) value |= static_cast<uint16_t>(1u << SR_BIT_COL4);
    return value;
}

void refreshLampMatrixStep() {
    static uint8_t row = 0;

    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        gpio_set_level(ROW_PINS[r], 1);
    }

    const uint16_t shiftValue = composeLampColumnShiftValue(row);
    writeShiftRegister16(shiftValue);

    gpio_set_level(ROW_PINS[row], 0);
    esp_rom_delay_us(250);
    gpio_set_level(ROW_PINS[row], 1);

    row = static_cast<uint8_t>((row + 1) % MATRIX_ROWS);
}

void initGpio() {
    gpio_config_t rowCfg = {};
    rowCfg.mode = GPIO_MODE_OUTPUT;
    rowCfg.pin_bit_mask = 0;
    for (auto pin : ROW_PINS) {
        rowCfg.pin_bit_mask |= (1ULL << pin);
    }
    rowCfg.pull_up_en = GPIO_PULLUP_DISABLE;
    rowCfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    rowCfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&rowCfg);
    for (auto pin : ROW_PINS) {
        gpio_set_level(pin, 1);
    }

    gpio_config_t colCfg = {};
    colCfg.mode = GPIO_MODE_INPUT;
    colCfg.pin_bit_mask = 0;
    for (auto pin : COL_PINS) {
        colCfg.pin_bit_mask |= (1ULL << pin);
    }
    colCfg.pull_up_en = GPIO_PULLUP_ENABLE;
    colCfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    colCfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&colCfg);

    gpio_config_t srCfg = {};
    srCfg.mode = GPIO_MODE_OUTPUT;
    srCfg.pin_bit_mask = (1ULL << SR_DATA_PIN) | (1ULL << SR_CLOCK_PIN) | (1ULL << SR_LATCH_PIN);
    srCfg.pull_up_en = GPIO_PULLUP_DISABLE;
    srCfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    srCfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&srCfg);

    writeShiftRegister16(0);
}

void runLampChasePattern(uint32_t tick) {
    memset(lampBits, 0, sizeof(lampBits));
    const uint8_t row = static_cast<uint8_t>((tick / 5) % LAMP_ROWS);
    const uint8_t col = static_cast<uint8_t>((tick / 5) % LAMP_COLS);
    setBit(lampBits, lampBitIndex(row, col), true);
}

void setStatusLedColor(uint8_t red, uint8_t green, uint8_t blue) {
    if (statusLedChannel == nullptr || statusLedEncoder == nullptr) {
        return;
    }

    uint8_t grb[3] = {green, red, blue};
    rmt_transmit_config_t txConfig = {};
    txConfig.loop_count = 0;

    const esp_err_t txErr = rmt_transmit(
        statusLedChannel,
        statusLedEncoder,
        grb,
        sizeof(grb),
        &txConfig);
    if (txErr == ESP_OK) {
        rmt_tx_wait_all_done(statusLedChannel, pdMS_TO_TICKS(10));
        // WS2812 needs a low-level latch/reset window after each frame.
        esp_rom_delay_us(80);
    }
}

void initStatusHeartbeatLed() {
    rmt_tx_channel_config_t txChannelConfig = {};
    txChannelConfig.clk_src = RMT_CLK_SRC_DEFAULT;
    txChannelConfig.gpio_num = STATUS_RGB_PIN;
    txChannelConfig.mem_block_symbols = 64;
    txChannelConfig.resolution_hz = 10000000;
    txChannelConfig.trans_queue_depth = 4;

    esp_err_t err = rmt_new_tx_channel(&txChannelConfig, &statusLedChannel);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "status LED channel init failed on GPIO%u: %s", static_cast<unsigned>(STATUS_RGB_PIN), esp_err_to_name(err));
        statusLedChannel = nullptr;
        return;
    }

    rmt_bytes_encoder_config_t bytesEncoderConfig = {};
    bytesEncoderConfig.bit0.level0 = 1;
    bytesEncoderConfig.bit0.duration0 = 4;
    bytesEncoderConfig.bit0.level1 = 0;
    bytesEncoderConfig.bit0.duration1 = 8;
    bytesEncoderConfig.bit1.level0 = 1;
    bytesEncoderConfig.bit1.duration0 = 8;
    bytesEncoderConfig.bit1.level1 = 0;
    bytesEncoderConfig.bit1.duration1 = 4;
    bytesEncoderConfig.flags.msb_first = 1;

    err = rmt_new_bytes_encoder(&bytesEncoderConfig, &statusLedEncoder);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "status LED encoder init failed: %s", esp_err_to_name(err));
        statusLedEncoder = nullptr;
        return;
    }

    err = rmt_enable(statusLedChannel);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "status LED RMT enable failed: %s", esp_err_to_name(err));
        return;
    }

    setStatusLedColor(0, 0, 0);
    statusLastHeartbeatMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    ESP_LOGI(TAG, "status LED ready on GPIO%u (WS2812 via RMT)", static_cast<unsigned>(STATUS_RGB_PIN));
}

void updateStatusHeartbeatLed() {
    if (statusLedChannel == nullptr || statusLedEncoder == nullptr) {
        return;
    }

    const uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    if ((nowMs - statusLastHeartbeatMs) < STATUS_HEARTBEAT_MS) {
        return;
    }

    statusLastHeartbeatMs = nowMs;
    statusLedOn = !statusLedOn;
    setStatusLedColor(0, statusLedOn ? STATUS_ON_GREEN : 0, 0);
}

void updateSerialHeartbeat() {
    const uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    if ((nowMs - serialHeartbeatLastMs) < SERIAL_HEARTBEAT_MS) {
        return;
    }

    serialHeartbeatLastMs = nowMs;
    serialHeartbeatOn = !serialHeartbeatOn;
    ESP_LOGI(TAG, "heartbeat=%s uptime_ms=%lu", serialHeartbeatOn ? "ON" : "OFF", static_cast<unsigned long>(nowMs));
}
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Captain matrix ESP-IDF migration scaffold started");
    initGpio();
    initStatusHeartbeatLed();
    serialHeartbeatLastMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);

    uint32_t tick = 0;
    while (true) {
        runLampChasePattern(tick);
        refreshLampMatrixStep();
        updateStatusHeartbeatLed();
        updateSerialHeartbeat();

        if ((tick % 200) == 0) {
            ESP_LOGI(TAG, "matrix scaffold alive tick=%lu", static_cast<unsigned long>(tick));
        }

        tick++;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
