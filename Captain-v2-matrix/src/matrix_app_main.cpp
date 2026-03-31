#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"

#include "captain_mapping.h"
#include "captain_protocol.h"
#include "i2c_bus_config.h"
#include "matrix_lamp_driver_config.h"

namespace {
constexpr char TAG[] = "captain_matrix";
constexpr uint32_t LOOP_DELAY_US = 5000;

uint8_t lampRowRam[CAPTAIN_LAMP_ROWS] = {};
uint8_t switchStateBytes[CAPTAIN_SWITCH_BYTES] = {};
uint8_t lampPulseWidthLevel = CAPTAIN_MATRIX_DEFAULT_PULSE_WIDTH_LEVEL;
bool matrixSystemEnabled = false;
bool matrixOutputEnabled = false;

void configureOutputPin(gpio_num_t pin, int initialLevel) {
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << static_cast<uint64_t>(pin);
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_ERROR_CHECK(gpio_set_level(pin, initialLevel));
}

void configureInputPin(gpio_num_t pin) {
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << static_cast<uint64_t>(pin);
    config.mode = GPIO_MODE_INPUT;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&config));
}

void initMatrixPins() {
    for (uint8_t row = 0; row < CAPTAIN_SWITCH_ROWS; row++) {
        configureOutputPin(static_cast<gpio_num_t>(CAPTAIN_MATRIX_ROW_PINS[row]), 1);
    }

    for (uint8_t col = 0; col < CAPTAIN_SWITCH_COLS; col++) {
        configureInputPin(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SWITCH_COL_PINS[col]));
    }

    configureOutputPin(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SR_DATA_PIN), 0);
    configureOutputPin(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SR_CLOCK_PIN), 0);
    configureOutputPin(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SR_LATCH_PIN), 0);
}

void logBootSummary() {
    ESP_LOGI(TAG, "Captain matrix ESP-IDF scaffold started");
    ESP_LOGI(TAG,
             "target=ESP32-C6 i2c_addr=0x%02X sda=%u scl=%u pulse_level=%u",
             CAPTAIN_MATRIX_I2C_ADDRESS,
             static_cast<unsigned>(CAPTAIN_MATRIX_I2C_SDA_PIN),
             static_cast<unsigned>(CAPTAIN_MATRIX_I2C_SCL_PIN),
             static_cast<unsigned>(lampPulseWidthLevel));
    ESP_LOGI(TAG,
             "rows=%u cols=%u lamp_rows=%u lamp_cols=%u",
             static_cast<unsigned>(CAPTAIN_SWITCH_ROWS),
             static_cast<unsigned>(CAPTAIN_SWITCH_COLS),
             static_cast<unsigned>(CAPTAIN_LAMP_ROWS),
             static_cast<unsigned>(CAPTAIN_LAMP_COLS));
}

void logLoopHeartbeat() {
    static uint32_t loopCounter = 0;
    loopCounter++;
    if ((loopCounter % 200) == 0) {
        ESP_LOGI(TAG,
                 "alive loops=%" PRIu32 " system=%u output=%u lamp0=0x%02X sw0=0x%02X",
                 loopCounter,
                 matrixSystemEnabled ? 1u : 0u,
                 matrixOutputEnabled ? 1u : 0u,
                 lampRowRam[0],
                 switchStateBytes[0]);
    }
}
}

extern "C" void app_main(void) {
    memset(lampRowRam, 0, sizeof(lampRowRam));
    memset(switchStateBytes, 0, sizeof(switchStateBytes));

    initMatrixPins();
    logBootSummary();

    while (true) {
        logLoopHeartbeat();
        esp_rom_delay_us(LOOP_DELAY_US);
    }
}