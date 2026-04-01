#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"

namespace {
static const char* TAG = "captain_matrix_idf";

constexpr uint8_t MATRIX_ROWS = 8;
constexpr uint8_t MATRIX_COLS = 4;

// Matches current matrix mapping used in Arduino firmware.
constexpr gpio_num_t ROW_PINS[MATRIX_ROWS] = {
    GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_5,
    GPIO_NUM_18, GPIO_NUM_19, GPIO_NUM_23, GPIO_NUM_13
};

constexpr gpio_num_t COL_PINS[MATRIX_COLS] = {
    GPIO_NUM_35, GPIO_NUM_34, GPIO_NUM_39, GPIO_NUM_36
};

constexpr gpio_num_t SR_DATA_PIN = GPIO_NUM_2;
constexpr gpio_num_t SR_CLOCK_PIN = GPIO_NUM_12;
constexpr gpio_num_t SR_LATCH_PIN = GPIO_NUM_4;

constexpr uint8_t SR_BIT_COL0 = 0;
constexpr uint8_t SR_BIT_COL1 = 1;
constexpr uint8_t SR_BIT_COL2 = 2;
constexpr uint8_t SR_BIT_COL3 = 3;
constexpr uint8_t SR_BIT_COL4 = 4;

constexpr uint8_t LAMP_ROWS = 8;
constexpr uint8_t LAMP_COLS = 5;
constexpr uint8_t LAMP_BYTES = (LAMP_ROWS * LAMP_COLS + 7) / 8;

uint8_t lampBits[LAMP_BYTES] = {};

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
    ets_delay_us(250);
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
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Captain matrix ESP-IDF migration scaffold started");
    initGpio();

    uint32_t tick = 0;
    while (true) {
        runLampChasePattern(tick);
        refreshLampMatrixStep();

        if ((tick % 200) == 0) {
            ESP_LOGI(TAG, "matrix scaffold alive tick=%lu", static_cast<unsigned long>(tick));
        }

        tick++;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
