#ifdef CAPTAIN_MATRIX_BAREBONES

#include <inttypes.h>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#include "matrix_lamp_driver_config.h"

namespace {
constexpr char TAG[] = "mx_bare";

// Bare-bones test target.
constexpr uint8_t TEST_ROW_INDEX = 4;  // Row 5 (0-based)
constexpr uint8_t TEST_COL_INDEX = 4;  // Col 5 / L20 path (0-based)

// Safety-first pulse settings.
constexpr uint32_t TEST_PERIOD_US = 5000;   // 200 Hz frame
constexpr uint32_t TEST_ON_US = 250;         // 5% duty
constexpr uint32_t TEST_BOOT_WINDOW_MS = 500;
constexpr bool TEST_ENABLE_OUTPUTS = false;

// Hardware mapping switches for quick A/B.
constexpr bool SR_CHAIN_IS_COL_THEN_ROW = false;
constexpr bool SR_ROW_ACTIVE_LOW = true;
constexpr bool SR_COL_ACTIVE_LOW = true;

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

void writeShiftRegister16(uint16_t value) {
    gpio_set_level(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SR_LATCH_PIN), 0);
    for (int8_t bit = 15; bit >= 0; bit--) {
        gpio_set_level(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SR_CLOCK_PIN), 0);
        const bool state = (value & (1u << bit)) != 0;
        gpio_set_level(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SR_DATA_PIN), state ? 1 : 0);
        gpio_set_level(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SR_CLOCK_PIN), 1);
    }
    gpio_set_level(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SR_LATCH_PIN), 1);
}

uint16_t composeShiftFrame(uint8_t rowMask, uint8_t colMask) {
    const uint8_t rowOut = SR_ROW_ACTIVE_LOW ? static_cast<uint8_t>(~rowMask) : rowMask;
    const uint8_t colOut = SR_COL_ACTIVE_LOW ? static_cast<uint8_t>(~colMask) : colMask;

    if (SR_CHAIN_IS_COL_THEN_ROW) {
        return static_cast<uint16_t>((static_cast<uint16_t>(colOut) << 8) | rowOut);
    }

    return static_cast<uint16_t>((static_cast<uint16_t>(rowOut) << 8) | colOut);
}

void initPins() {
    configureOutputPin(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SR_DATA_PIN), 0);
    configureOutputPin(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SR_CLOCK_PIN), 0);
    configureOutputPin(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SR_LATCH_PIN), 0);
    // OE# is active-low: drive high to force all outputs off when safety lockout is enabled.
    configureOutputPin(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SR_OE_N_PIN), TEST_ENABLE_OUTPUTS ? 0 : 1);
}

void logConfig() {
    ESP_LOGI(TAG,
             "barebones test row=%u col=%u period_us=%" PRIu32 " on_us=%" PRIu32 " boot_window_ms=%" PRIu32,
             static_cast<unsigned>(TEST_ROW_INDEX),
             static_cast<unsigned>(TEST_COL_INDEX),
             TEST_PERIOD_US,
             TEST_ON_US,
             TEST_BOOT_WINDOW_MS);
    ESP_LOGI(TAG,
             "sr_pins data=%d clk=%d latch=%d oe_n=%d chain_col_then_row=%u row_active_low=%u col_active_low=%u",
             static_cast<int>(CAPTAIN_MATRIX_SR_DATA_PIN),
             static_cast<int>(CAPTAIN_MATRIX_SR_CLOCK_PIN),
             static_cast<int>(CAPTAIN_MATRIX_SR_LATCH_PIN),
             static_cast<int>(CAPTAIN_MATRIX_SR_OE_N_PIN),
             SR_CHAIN_IS_COL_THEN_ROW ? 1u : 0u,
             SR_ROW_ACTIVE_LOW ? 1u : 0u,
             SR_COL_ACTIVE_LOW ? 1u : 0u);
    ESP_LOGW(TAG, "output_lockout=%u (set TEST_ENABLE_OUTPUTS=true only after fuse-safe checks)",
             TEST_ENABLE_OUTPUTS ? 0u : 1u);
}
} // namespace

extern "C" void app_main(void) {
    initPins();
    logConfig();

    const uint8_t rowMask = static_cast<uint8_t>(1u << TEST_ROW_INDEX);
    const uint8_t colMask = static_cast<uint8_t>(1u << TEST_COL_INDEX);
    const uint16_t onFrame = composeShiftFrame(rowMask, colMask);
    const uint16_t offFrame = composeShiftFrame(rowMask, 0x00);
    const uint64_t startUs = static_cast<uint64_t>(esp_timer_get_time());

    if (!TEST_ENABLE_OUTPUTS) {
        writeShiftRegister16(composeShiftFrame(0x00, 0x00));
        while (true) {
            esp_rom_delay_us(50000);
        }
    }

    while (true) {
        const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());
        if (TEST_BOOT_WINDOW_MS > 0) {
            const uint64_t elapsedMs = (nowUs - startUs) / 1000ULL;
            if (elapsedMs >= TEST_BOOT_WINDOW_MS) {
                writeShiftRegister16(composeShiftFrame(0x00, 0x00));
                esp_rom_delay_us(20000);
                continue;
            }
        }

        writeShiftRegister16(onFrame);
        esp_rom_delay_us(TEST_ON_US);

        writeShiftRegister16(offFrame);
        esp_rom_delay_us(TEST_PERIOD_US - TEST_ON_US);
    }
}

#endif  // CAPTAIN_MATRIX_BAREBONES
