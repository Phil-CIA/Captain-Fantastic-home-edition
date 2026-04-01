#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"

namespace {
static const char* TAG = "captain_idf";

constexpr uint8_t MATRIX_ROWS = 8;
constexpr uint8_t MATRIX_COLS = 4;

// Pin map aligned to current carrier wiring.
// GPIO9 (Sw_Col3) is intentionally reserved because it is a boot strap pin.
constexpr gpio_num_t ROW_PINS[MATRIX_ROWS] = {
    GPIO_NUM_15, GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_7,
    GPIO_NUM_8, GPIO_NUM_10, GPIO_NUM_11, GPIO_NUM_1
};

constexpr gpio_num_t SW_COL3_REMAP_PIN = GPIO_NUM_23;

constexpr gpio_num_t COL_PINS[MATRIX_COLS] = {
    GPIO_NUM_20, GPIO_NUM_19, GPIO_NUM_18, SW_COL3_REMAP_PIN
};
uint8_t switchPressedMaskByRow[MATRIX_ROWS] = {};

inline bool isUsableOutputPin(gpio_num_t pin) {
    return pin != GPIO_NUM_NC;
}

inline bool isUsableInputPin(gpio_num_t pin) {
    return pin != GPIO_NUM_NC;
}

void refreshLampMatrixStep() {
    static uint8_t row = 0;

    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        if (isUsableOutputPin(ROW_PINS[r])) {
            gpio_set_level(ROW_PINS[r], 1);
        }
    }

    if (isUsableOutputPin(ROW_PINS[row])) {
        gpio_set_level(ROW_PINS[row], 0);
        uint8_t pressedMask = 0;
        for (uint8_t col = 0; col < MATRIX_COLS; col++) {
            if (!isUsableInputPin(COL_PINS[col])) {
                continue;
            }
            const int level = gpio_get_level(COL_PINS[col]);
            if (level == 0) {
                pressedMask |= static_cast<uint8_t>(1u << col);
            }
        }
        switchPressedMaskByRow[row] = pressedMask;
    } else {
        switchPressedMaskByRow[row] = 0;
    }
    esp_rom_delay_us(250);
    if (isUsableOutputPin(ROW_PINS[row])) {
        gpio_set_level(ROW_PINS[row], 1);
    }

    row = static_cast<uint8_t>((row + 1) % MATRIX_ROWS);
}

void initGpio() {
    gpio_config_t rowCfg = {};
    rowCfg.mode = GPIO_MODE_OUTPUT;
    rowCfg.pin_bit_mask = 0;
    for (auto pin : ROW_PINS) {
        if (isUsableOutputPin(pin)) {
            rowCfg.pin_bit_mask |= (1ULL << pin);
        }
    }
    rowCfg.pull_up_en = GPIO_PULLUP_DISABLE;
    rowCfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    rowCfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&rowCfg);
    for (auto pin : ROW_PINS) {
        if (isUsableOutputPin(pin)) {
            gpio_set_level(pin, 1);
        }
    }

    gpio_config_t colCfg = {};
    colCfg.mode = GPIO_MODE_INPUT;
    colCfg.pin_bit_mask = 0;
    for (auto pin : COL_PINS) {
        if (isUsableInputPin(pin)) {
            colCfg.pin_bit_mask |= (1ULL << pin);
        }
    }
    colCfg.pull_up_en = GPIO_PULLUP_ENABLE;
    colCfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    colCfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&colCfg);

}
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Captain ESP-IDF matrix scaffold started");
    initGpio();

    TickType_t loopDelayTicks = pdMS_TO_TICKS(5);
    if (loopDelayTicks == 0) {
        loopDelayTicks = 1;
    }

    uint32_t tick = 0;
    while (true) {
        refreshLampMatrixStep();

        if ((tick % 200) == 0) {
            ESP_LOGI(TAG, "matrix scaffold alive tick=%lu", static_cast<unsigned long>(tick));
            ESP_LOGI(
                TAG,
                "switch pressed masks r0..r7: %u %u %u %u %u %u %u %u",
                static_cast<unsigned>(switchPressedMaskByRow[0]),
                static_cast<unsigned>(switchPressedMaskByRow[1]),
                static_cast<unsigned>(switchPressedMaskByRow[2]),
                static_cast<unsigned>(switchPressedMaskByRow[3]),
                static_cast<unsigned>(switchPressedMaskByRow[4]),
                static_cast<unsigned>(switchPressedMaskByRow[5]),
                static_cast<unsigned>(switchPressedMaskByRow[6]),
                static_cast<unsigned>(switchPressedMaskByRow[7])
            );
        }

        tick++;
        vTaskDelay(loopDelayTicks);
    }
}
