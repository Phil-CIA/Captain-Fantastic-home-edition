#ifndef CAPTAIN_MATRIX_BAREBONES

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "captain_mapping.h"
#include "captain_protocol.h"
#include "i2c_bus_config.h"
#include "matrix_lamp_driver_config.h"

namespace {
constexpr char TAG[] = "captain_matrix";
constexpr uint32_t LOOP_DELAY_US = 5000;
constexpr uint32_t OLED_REFRESH_MS = 250;
constexpr uint32_t MATRIX_LINK_LOG_MS = 1000;
constexpr uint16_t MATRIX_LAMP_PULSE_MIN_US = 100;
constexpr uint16_t MATRIX_LAMP_PULSE_STEP_US = 100;
constexpr uint16_t MATRIX_ROW_BLANK_US = 50;
constexpr uint16_t MATRIX_ROW_SETTLE_US = 50;
constexpr uint16_t MATRIX_ROW_POST_HOLD_US = 50;
constexpr uint8_t MATRIX_SWITCH_DEBOUNCE_TICKS = 4;
// Safety-limited proof mode: one row/column path, low duty, short timeout.
constexpr bool MATRIX_SAFE_PROOF_MODE = false;
constexpr uint8_t MATRIX_SAFE_PROOF_ROW = 4;  // 0-based (Row 5)
constexpr uint8_t MATRIX_SAFE_PROOF_COL = 4;  // 0-based (L20)
constexpr uint32_t MATRIX_SAFE_PROOF_DURATION_MS = 2000;
constexpr uint32_t MATRIX_SAFE_PROOF_PERIOD_US = 5000;
constexpr uint32_t MATRIX_SAFE_PROOF_ON_US = 1000;  // 20% duty
// Guarded boot test: briefly drive one known lamp at low energy, then force all-off.
constexpr bool MATRIX_DIAG_GUARDED_BOOT_TEST = false;
constexpr uint8_t MATRIX_DIAG_BOOT_TEST_ROW = 4;   // 0-based row index (Row 5)
constexpr uint8_t MATRIX_DIAG_BOOT_TEST_COL = 4;   // 0-based lamp column index (L20)
constexpr bool MATRIX_DIAG_BOOT_TEST_SWEEP_COLS = true;
constexpr uint8_t MATRIX_DIAG_BOOT_TEST_PULSE_LEVEL = 6;
constexpr uint32_t MATRIX_DIAG_BOOT_TEST_COL_STEP_MS = 600;
constexpr uint32_t MATRIX_DIAG_BOOT_TEST_COLUMN_PERIOD_US = 2500;
constexpr uint32_t MATRIX_DIAG_BOOT_TEST_COLUMN_ON_US = 500;  // 20% duty
constexpr uint32_t MATRIX_DIAG_BOOT_TEST_DURATION_MS =
    MATRIX_DIAG_BOOT_TEST_SWEEP_COLS ? (MATRIX_DIAG_BOOT_TEST_COL_STEP_MS * CAPTAIN_LAMP_COLS) : 5000;
// Bench-only mode to force one lamp path for conduction debugging.
constexpr bool MATRIX_DIAG_FORCE_SINGLE_LAMP = false;
constexpr bool MATRIX_DIAG_SKIP_SWITCH_SCAN = true;
constexpr uint8_t MATRIX_DIAG_FORCE_ROW = 4;   // 0-based row index (4 => Row 5)
constexpr uint8_t MATRIX_DIAG_FORCE_COL = 4;   // 0-based lamp column index (4 => L20)
constexpr uint8_t MATRIX_DIAG_FORCE_PULSE_LEVEL = 10;
// When true, bypass scan pulsing and hold one row/col frame continuously for DMM checks.
constexpr bool MATRIX_DIAG_HOLD_ONE_LAMP_DC = false;
// Keep row asserted while pulsing only the selected column for visible burn testing.
constexpr bool MATRIX_DIAG_HOLD_ROW_PULSE_COLUMN = false;
constexpr uint32_t MATRIX_DIAG_COLUMN_PULSE_PERIOD_US = 3000;
constexpr uint32_t MATRIX_DIAG_COLUMN_PULSE_ON_US = 2000;
// Shift-register drive mapping (set from bench results).
constexpr bool MATRIX_SR_CHAIN_IS_COL_THEN_ROW = true;
constexpr bool MATRIX_SR_ROW_ACTIVE_LOW = false;
constexpr bool MATRIX_SR_COL_ACTIVE_LOW = false;
constexpr i2c_port_t MATRIX_I2C_PORT = I2C_NUM_0;
constexpr size_t MATRIX_I2C_RX_BUFFER = 128;
constexpr size_t MATRIX_I2C_TX_BUFFER = 128;
constexpr uint32_t OLED_SW_I2C_DELAY_US = 4;
constexpr uint8_t OLED_I2C_SDA_PIN = 7;
constexpr uint8_t OLED_I2C_SCL_PIN = 6;
// OLED drawing is bit-banged and can pause scan refresh for ~100ms bursts.
// Disable during lamp bring-up to keep row/column timing continuous.
constexpr bool CAPTAIN_MATRIX_ENABLE_OLED_DIAGNOSTICS = false;
constexpr uint8_t OLED_ADDR_A = 0x3C;
constexpr uint8_t OLED_ADDR_B = 0x3D;

uint8_t lampRowRam[CAPTAIN_LAMP_ROWS] = {};
uint8_t switchStateBytes[CAPTAIN_SWITCH_BYTES] = {};
uint8_t debounceCandidateBits[CAPTAIN_SWITCH_BYTES] = {};
uint8_t debounceTickCounters[CAPTAIN_SWITCH_ROWS * CAPTAIN_SWITCH_COLS] = {};
uint8_t registerPointer = CAPTAIN_MATRIX_REG_SWITCH_BASE;
uint8_t lampPulseWidthLevel = CAPTAIN_MATRIX_DEFAULT_PULSE_WIDTH_LEVEL;
// Default enabled for bench bring-up so lamps can respond without extra setup commands.
bool matrixSystemEnabled = true;
bool matrixOutputEnabled = true;
uint32_t matrixI2CRxPacketCount = 0;
uint32_t matrixLampWriteBurstCount = 0;
uint32_t matrixLampWriteByteCount = 0;
uint8_t matrixLastCommand = 0;
uint32_t matrixLastLinkLogMs = 0;
bool safeProofActive = false;
uint64_t safeProofEndUs = 0;
bool diagBootTestActive = false;
uint64_t diagBootTestEndUs = 0;
bool oledReady = false;
uint8_t oledAddress = 0;
uint32_t lastOledRefreshMs = 0;

uint8_t guardedBootTestColumn() {
    uint8_t bootCol = MATRIX_DIAG_BOOT_TEST_COL;
    if (MATRIX_DIAG_BOOT_TEST_SWEEP_COLS) {
        const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());
        const uint64_t elapsedUs = nowUs >= diagBootTestEndUs
                                       ? static_cast<uint64_t>(MATRIX_DIAG_BOOT_TEST_DURATION_MS) * 1000ULL
                                       : (static_cast<uint64_t>(MATRIX_DIAG_BOOT_TEST_DURATION_MS) * 1000ULL) -
                                             (diagBootTestEndUs - nowUs);
        const uint32_t elapsedMs = static_cast<uint32_t>(elapsedUs / 1000ULL);
        const uint32_t step = elapsedMs / MATRIX_DIAG_BOOT_TEST_COL_STEP_MS;
        bootCol = static_cast<uint8_t>(step % CAPTAIN_LAMP_COLS);
    }
    return bootCol;
}

uint16_t appliedLampPulseWidthUs() {
    return static_cast<uint16_t>(MATRIX_LAMP_PULSE_MIN_US +
                                 static_cast<uint16_t>(lampPulseWidthLevel) * MATRIX_LAMP_PULSE_STEP_US);
}

inline void delayWallUs(uint32_t delayUs) {
    const int64_t endUs = esp_timer_get_time() + static_cast<int64_t>(delayUs);
    while (esp_timer_get_time() < endUs) {}
}

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

void oledI2CDelay() {
    esp_rom_delay_us(OLED_SW_I2C_DELAY_US);
}

void oledSda(bool high) {
    gpio_set_level(static_cast<gpio_num_t>(OLED_I2C_SDA_PIN), high ? 1 : 0);
}

void oledScl(bool high) {
    gpio_set_level(static_cast<gpio_num_t>(OLED_I2C_SCL_PIN), high ? 1 : 0);
}

void oledI2CStart() {
    oledSda(true);
    oledScl(true);
    oledI2CDelay();
    oledSda(false);
    oledI2CDelay();
    oledScl(false);
}

void oledI2CStop() {
    oledSda(false);
    oledI2CDelay();
    oledScl(true);
    oledI2CDelay();
    oledSda(true);
    oledI2CDelay();
}

bool oledI2CWriteByte(uint8_t value) {
    for (uint8_t bit = 0; bit < 8; bit++) {
        oledSda((value & 0x80) != 0);
        oledI2CDelay();
        oledScl(true);
        oledI2CDelay();
        oledScl(false);
        value <<= 1;
    }

    gpio_set_direction(static_cast<gpio_num_t>(OLED_I2C_SDA_PIN), GPIO_MODE_INPUT_OUTPUT_OD);
    oledSda(true);
    oledI2CDelay();
    oledScl(true);
    oledI2CDelay();
    const int ackLevel = gpio_get_level(static_cast<gpio_num_t>(OLED_I2C_SDA_PIN));
    oledScl(false);
    return ackLevel == 0;
}

bool oledWritePayload(uint8_t control, const uint8_t* data, size_t length) {
    oledI2CStart();
    if (!oledI2CWriteByte(static_cast<uint8_t>((oledAddress << 1) | 0))) {
        oledI2CStop();
        return false;
    }
    if (!oledI2CWriteByte(control)) {
        oledI2CStop();
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        if (!oledI2CWriteByte(data[i])) {
            oledI2CStop();
            return false;
        }
    }
    oledI2CStop();
    return true;
}

bool probeOledAddress(uint8_t address) {
    oledAddress = address;
    oledI2CStart();
    const bool ack = oledI2CWriteByte(static_cast<uint8_t>((oledAddress << 1) | 0));
    oledI2CStop();
    return ack;
}

bool oledWriteCommand(uint8_t command) {
    return oledWritePayload(0x00, &command, 1);
}

bool oledWriteData(const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0) {
        return true;
    }
    if (length > 128) {
        length = 128;
    }
    return oledWritePayload(0x40, data, length);
}

void oledClear() {
    if (!oledReady) {
        return;
    }

    uint8_t pageData[128] = {};
    for (uint8_t page = 0; page < 8; page++) {
        if (!oledWriteCommand(static_cast<uint8_t>(0xB0 + page)) ||
            !oledWriteCommand(0x00) ||
            !oledWriteCommand(0x10) ||
            !oledWriteData(pageData, sizeof(pageData))) {
            oledReady = false;
            return;
        }
    }
}

void initOled() {
    if (!CAPTAIN_MATRIX_ENABLE_OLED_DIAGNOSTICS) {
        return;
    }

    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << OLED_I2C_SDA_PIN) | (1ULL << OLED_I2C_SCL_PIN);
    cfg.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&cfg) != ESP_OK) {
        ESP_LOGW(TAG, "OLED: gpio_config failed");
        return;
    }
    oledSda(true);
    oledScl(true);

    if (probeOledAddress(OLED_ADDR_A)) {
        oledAddress = OLED_ADDR_A;
    } else if (probeOledAddress(OLED_ADDR_B)) {
        oledAddress = OLED_ADDR_B;
    } else {
        ESP_LOGW(TAG, "OLED: not found at 0x3C/0x3D");
        return;
    }

    const uint8_t initCmds[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0x8F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
        0x2E, 0xAF
    };

    for (size_t i = 0; i < sizeof(initCmds); i++) {
        if (!oledWriteCommand(initCmds[i])) {
            ESP_LOGW(TAG, "OLED: init command failed");
            return;
        }
    }

    oledReady = true;
    oledClear();
    ESP_LOGI(TAG, "OLED: enabled at 0x%02X", oledAddress);
}

void updateOledStatus() {
    if (!oledReady) {
        return;
    }
    if (!CAPTAIN_MATRIX_ENABLE_OLED_DIAGNOSTICS) {
        return;
    }

    const uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    if ((nowMs - lastOledRefreshMs) < OLED_REFRESH_MS) {
        return;
    }
    lastOledRefreshMs = nowMs;

    uint8_t page0[128] = {};
    if (matrixSystemEnabled) {
        for (uint8_t i = 2; i < 22; i++) page0[i] = 0x7E;
    }
    if (matrixOutputEnabled) {
        for (uint8_t i = 26; i < 46; i++) page0[i] = 0x7E;
    }
    const uint8_t pulseBar = static_cast<uint8_t>((lampPulseWidthLevel * 30U) / 15U);
    for (uint8_t i = 50; i < static_cast<uint8_t>(50 + pulseBar) && i < 80; i++) page0[i] = 0x7E;
    if (switchStateBytes[0] != 0 || lampRowRam[0] != 0) {
        for (uint8_t i = 84; i < 104; i++) page0[i] = 0x7E;
    }

    oledWriteCommand(0xB0);
    oledWriteCommand(0x00);
    oledWriteCommand(0x10);
    if (!oledWriteData(page0, sizeof(page0))) {
        oledReady = false;
        return;
    }

    for (uint8_t page = 1; page < 8; page++) {
        uint8_t rowData[128] = {};
        const uint8_t row = static_cast<uint8_t>(page - 1);
        const uint8_t rowMask = lampRowRam[row];

        for (uint8_t col = 0; col < CAPTAIN_LAMP_COLS; col++) {
            const uint8_t x0 = static_cast<uint8_t>(4 + col * 24);
            const uint8_t x1 = static_cast<uint8_t>(x0 + 18);
            const bool on = (rowMask & captainMatrixLampRowMask(col)) != 0;
            for (uint8_t x = x0; x < x1 && x < 128; x++) {
                rowData[x] = on ? 0x7E : 0x42;
            }
        }

        oledWriteCommand(static_cast<uint8_t>(0xB0 + page));
        oledWriteCommand(0x00);
        oledWriteCommand(0x10);
        if (!oledWriteData(rowData, sizeof(rowData))) {
            oledReady = false;
            return;
        }
    }
}

void initMatrixPins() {
    for (uint8_t col = 0; col < CAPTAIN_SWITCH_COLS; col++) {
        configureInputPin(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SWITCH_COL_PINS[col]));
    }

    configureOutputPin(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SR_DATA_PIN), 0);
    configureOutputPin(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SR_CLOCK_PIN), 0);
    configureOutputPin(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SR_LATCH_PIN), 0);
    // OE# is active-low. Set LOW (0) once at boot to enable shift-register outputs, leave alone forever.
    configureOutputPin(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SR_OE_N_PIN), 0);
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

uint16_t composeLampColumnShiftValue(uint8_t row) {
    if (diagBootTestActive) {
        if (row == MATRIX_DIAG_BOOT_TEST_ROW) {
            const uint8_t bootCol = guardedBootTestColumn();
            return static_cast<uint16_t>(captainMatrixLampRowMask(bootCol));
        }
        return 0;
    }

    if (MATRIX_DIAG_FORCE_SINGLE_LAMP) {
        if (row == MATRIX_DIAG_FORCE_ROW) {
            return static_cast<uint16_t>(captainMatrixLampRowMask(MATRIX_DIAG_FORCE_COL));
        }
        return 0;
    }

    return static_cast<uint16_t>(lampRowRam[row] & 0x1Fu);
}

// Logical col0 is unused/not wired. Shift logical cols 1..4 down to physical bits 0..3.
static inline uint8_t remapLogicalToPhysicalCols(uint8_t logicalColMask) {
    return static_cast<uint8_t>((logicalColMask >> 1) & 0x0Fu);
}

uint16_t composeShiftFrame(uint8_t rowMask, uint8_t colMask) {
    const uint8_t rowOut = MATRIX_SR_ROW_ACTIVE_LOW ? static_cast<uint8_t>(~rowMask) : rowMask;
    const uint8_t physColMask = remapLogicalToPhysicalCols(colMask);
    const uint8_t colOut = MATRIX_SR_COL_ACTIVE_LOW ? static_cast<uint8_t>(~physColMask) : physColMask;

    if (MATRIX_SR_CHAIN_IS_COL_THEN_ROW) {
        // Shift [col][row] so the first byte lands on downstream register.
        return static_cast<uint16_t>((static_cast<uint16_t>(colOut) << 8) | rowOut);
    }

    // Alternate chain order: shift [row][col].
    return static_cast<uint16_t>((static_cast<uint16_t>(rowOut) << 8) | colOut);
}

void refreshLampMatrixStep() {
    static uint8_t row = 0;

    if (safeProofActive) {
        const uint8_t rowMask = static_cast<uint8_t>(1u << MATRIX_SAFE_PROOF_ROW);
        const uint8_t colMask = static_cast<uint8_t>(captainMatrixLampRowMask(MATRIX_SAFE_PROOF_COL));
        const uint32_t phaseUs = static_cast<uint32_t>(esp_timer_get_time() % MATRIX_SAFE_PROOF_PERIOD_US);
        const bool columnEnabled = phaseUs < MATRIX_SAFE_PROOF_ON_US;
        const uint8_t activeColMask = columnEnabled ? colMask : 0;
        writeShiftRegister16(composeShiftFrame(rowMask, activeColMask));
        return;
    }

    if (diagBootTestActive) {
        const uint8_t rowMask = static_cast<uint8_t>(1u << MATRIX_DIAG_BOOT_TEST_ROW);
        const uint8_t colMask = static_cast<uint8_t>(captainMatrixLampRowMask(guardedBootTestColumn()));
        const uint32_t phaseUs = static_cast<uint32_t>(esp_timer_get_time() % MATRIX_DIAG_BOOT_TEST_COLUMN_PERIOD_US);
        const bool columnEnabled = phaseUs < MATRIX_DIAG_BOOT_TEST_COLUMN_ON_US;
        const uint8_t activeColMask = columnEnabled ? colMask : 0;

        writeShiftRegister16(composeShiftFrame(rowMask, activeColMask));
        return;
    }

    if (MATRIX_DIAG_FORCE_SINGLE_LAMP && MATRIX_DIAG_HOLD_ONE_LAMP_DC) {
        const uint8_t rowMask = static_cast<uint8_t>(1u << MATRIX_DIAG_FORCE_ROW);
        const uint8_t colMask = static_cast<uint8_t>(captainMatrixLampRowMask(MATRIX_DIAG_FORCE_COL));

        if (!matrixSystemEnabled || !matrixOutputEnabled) {
            writeShiftRegister16(composeShiftFrame(0x00, 0x00));
            return;
        }

        bool columnEnabled = true;
        if (MATRIX_DIAG_HOLD_ROW_PULSE_COLUMN) {
            const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());
            const uint32_t phaseUs = static_cast<uint32_t>(nowUs % MATRIX_DIAG_COLUMN_PULSE_PERIOD_US);
            columnEnabled = (phaseUs < MATRIX_DIAG_COLUMN_PULSE_ON_US);
        }

        const uint8_t activeColMask = columnEnabled ? colMask : 0;
        writeShiftRegister16(composeShiftFrame(rowMask, activeColMask));
        return;
    }

    // Enforce an explicit all-off phase before every row transition.
    writeShiftRegister16(composeShiftFrame(0x00, 0x00));
    delayWallUs(MATRIX_ROW_BLANK_US);

    if (!matrixSystemEnabled || !matrixOutputEnabled) {
        row = static_cast<uint8_t>((row + 1) % CAPTAIN_SWITCH_ROWS);
        return;
    }

    const uint8_t rowMask = static_cast<uint8_t>(1u << row);
    const uint8_t colMask = static_cast<uint8_t>(composeLampColumnShiftValue(row));

    // Phase B: row-only settle before enabling any lamp columns.
    writeShiftRegister16(composeShiftFrame(rowMask, 0x00));
    delayWallUs(MATRIX_ROW_SETTLE_US);

    // Phase C: row pulse window. Keep timing constant regardless of colMask so
    // scan cadence does not vary with lamp pattern density.
    writeShiftRegister16(composeShiftFrame(rowMask, colMask));
    delayWallUs(appliedLampPulseWidthUs());

    // Hold row after pulse so row and column edges are cleanly separated on LA.
    writeShiftRegister16(composeShiftFrame(rowMask, 0x00));
    delayWallUs(MATRIX_ROW_POST_HOLD_US);

    // Phase D: all-off release.
    writeShiftRegister16(composeShiftFrame(0x00, 0x00));

    row = static_cast<uint8_t>((row + 1) % CAPTAIN_SWITCH_ROWS);
}

void scanSwitchMatrix() {
    uint8_t sampleBits[CAPTAIN_SWITCH_BYTES] = {};

    if (!matrixSystemEnabled) {
        memset(switchStateBytes, 0, sizeof(switchStateBytes));
        memset(debounceCandidateBits, 0, sizeof(debounceCandidateBits));
        memset(debounceTickCounters, 0, sizeof(debounceTickCounters));
        return;
    }

    for (uint8_t row = 0; row < CAPTAIN_SWITCH_ROWS; row++) {
        writeShiftRegister16(composeShiftFrame(0x00, 0x00));
        esp_rom_delay_us(MATRIX_ROW_BLANK_US);

        const uint8_t rowMask = static_cast<uint8_t>(1u << row);
        writeShiftRegister16(composeShiftFrame(rowMask, 0x00));
        esp_rom_delay_us(MATRIX_ROW_SETTLE_US);

        for (uint8_t col = 0; col < CAPTAIN_SWITCH_COLS; col++) {
            const int level = gpio_get_level(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SWITCH_COL_PINS[col]));
            const bool closed = (level == 0);
            captainSetBit(sampleBits, captainSwitchBitIndex(row, col), closed);
        }
    }

    writeShiftRegister16(composeShiftFrame(0x00, 0x00));

    for (uint8_t row = 0; row < CAPTAIN_SWITCH_ROWS; row++) {
        for (uint8_t col = 0; col < CAPTAIN_SWITCH_COLS; col++) {
            const size_t bitIndex = captainSwitchBitIndex(row, col);
            const bool sampleClosed = captainGetBit(sampleBits, bitIndex);
            const bool stableClosed = captainGetBit(switchStateBytes, bitIndex);

            if (sampleClosed == stableClosed) {
                debounceTickCounters[bitIndex] = 0;
                captainSetBit(debounceCandidateBits, bitIndex, stableClosed);
                continue;
            }

            const bool candidateClosed = captainGetBit(debounceCandidateBits, bitIndex);
            if (sampleClosed != candidateClosed) {
                captainSetBit(debounceCandidateBits, bitIndex, sampleClosed);
                debounceTickCounters[bitIndex] = 1;
                continue;
            }

            if (debounceTickCounters[bitIndex] < 255) {
                debounceTickCounters[bitIndex]++;
            }

            if (debounceTickCounters[bitIndex] >= MATRIX_SWITCH_DEBOUNCE_TICKS) {
                captainSetBit(switchStateBytes, bitIndex, sampleClosed);
                debounceTickCounters[bitIndex] = 0;
            }
        }
    }
}

void fillDiagnosticBytes(uint8_t* diag) {
    diag[0] = 0;
    if (matrixSystemEnabled) {
        diag[0] |= CAPTAIN_MATRIX_DIAG_FLAG_SYSTEM_ENABLED;
    }
    if (matrixOutputEnabled) {
        diag[0] |= CAPTAIN_MATRIX_DIAG_FLAG_OUTPUT_ENABLED;
    }
    diag[1] = lampPulseWidthLevel;
    diag[2] = lampRowRam[0];
    diag[3] = switchStateBytes[0];
}

void queueI2CResponse() {
    uint8_t txData[CAPTAIN_LAMP_ROWS] = {};
    size_t txLength = 0;

    if (captainMatrixSwitchRegister(registerPointer)) {
        const uint8_t offset = static_cast<uint8_t>(registerPointer - CAPTAIN_MATRIX_REG_SWITCH_BASE);
        txLength = CAPTAIN_SWITCH_BYTES - offset;
        memcpy(txData, switchStateBytes + offset, txLength);
        registerPointer = CAPTAIN_MATRIX_REG_SWITCH_END;
    } else if (captainMatrixLampRegister(registerPointer)) {
        const uint8_t offset = static_cast<uint8_t>(registerPointer - CAPTAIN_MATRIX_REG_LAMP_BASE);
        txLength = CAPTAIN_LAMP_ROWS - offset;
        memcpy(txData, lampRowRam + offset, txLength);
        registerPointer = CAPTAIN_MATRIX_REG_LAMP_END;
    } else if (captainMatrixDiagnosticRegister(registerPointer)) {
        uint8_t diag[CAPTAIN_MATRIX_REG_DIAG_END - CAPTAIN_MATRIX_REG_DIAG_BASE + 1] = {};
        fillDiagnosticBytes(diag);
        const uint8_t offset = static_cast<uint8_t>(registerPointer - CAPTAIN_MATRIX_REG_DIAG_BASE);
        txLength = sizeof(diag) - offset;
        memcpy(txData, diag + offset, txLength);
        registerPointer = CAPTAIN_MATRIX_REG_DIAG_END;
    } else {
        txData[0] = 0;
        txLength = 1;
    }

    i2c_reset_tx_fifo(MATRIX_I2C_PORT);
    i2c_slave_write_buffer(MATRIX_I2C_PORT, txData, txLength, 0);
}

void handleI2CReceive(const uint8_t* packet, size_t length) {
    if (length == 0) {
        return;
    }

    const uint8_t command = packet[0];
    registerPointer = command;
    matrixLastCommand = command;
    const size_t payloadLength = length - 1;

    if ((command & 0xFEu) == CAPTAIN_MATRIX_CMD_SYSTEM_SETUP && payloadLength == 0) {
        matrixSystemEnabled = (command & CAPTAIN_MATRIX_CMD_SYSTEM_ENABLE) != 0;
        if (!matrixSystemEnabled) {
            writeShiftRegister16(composeShiftFrame(0x00, 0x00));
        }
        queueI2CResponse();
        return;
    }

    if ((command & 0xFEu) == CAPTAIN_MATRIX_CMD_OUTPUT_SETUP && payloadLength == 0) {
        matrixOutputEnabled = (command & CAPTAIN_MATRIX_CMD_OUTPUT_ENABLE) != 0;
        if (!matrixOutputEnabled) {
            writeShiftRegister16(composeShiftFrame(0x00, 0x00));
        }
        queueI2CResponse();
        return;
    }

    if ((command & 0xF0u) == CAPTAIN_MATRIX_CMD_PULSE_WIDTH_BASE && payloadLength == 0) {
        lampPulseWidthLevel = static_cast<uint8_t>(command & CAPTAIN_MATRIX_CMD_PULSE_WIDTH_MASK);
        queueI2CResponse();
        return;
    }

    if (captainMatrixLampRegister(command) && payloadLength > 0) {
        uint8_t writeBytes = 0;
        uint8_t target = command;
        for (size_t index = 1; index < length && captainMatrixLampRegister(target); index++, target++) {
            lampRowRam[target - CAPTAIN_MATRIX_REG_LAMP_BASE] = static_cast<uint8_t>(packet[index]) & 0x1Fu;
            writeBytes++;
        }
        if (writeBytes > 0) {
            matrixLampWriteBurstCount++;
            matrixLampWriteByteCount += writeBytes;
        }
    }

    queueI2CResponse();
}

void serviceI2C() {
    uint8_t rxPacket[32] = {};
    const int bytesRead = i2c_slave_read_buffer(MATRIX_I2C_PORT, rxPacket, sizeof(rxPacket), 0);
    if (bytesRead > 0) {
        matrixI2CRxPacketCount++;
        handleI2CReceive(rxPacket, static_cast<size_t>(bytesRead));
    }
}

void initI2CSlave() {
    i2c_config_t config = {};
    config.mode = I2C_MODE_SLAVE;
    config.sda_io_num = static_cast<gpio_num_t>(CAPTAIN_MATRIX_I2C_SDA_PIN);
    config.scl_io_num = static_cast<gpio_num_t>(CAPTAIN_MATRIX_I2C_SCL_PIN);
    config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    config.slave.addr_10bit_en = 0;
    config.slave.slave_addr = CAPTAIN_MATRIX_I2C_ADDRESS;

    ESP_ERROR_CHECK(i2c_param_config(MATRIX_I2C_PORT, &config));
    ESP_ERROR_CHECK(i2c_driver_install(
        MATRIX_I2C_PORT,
        config.mode,
        MATRIX_I2C_RX_BUFFER,
        MATRIX_I2C_TX_BUFFER,
        0));

    queueI2CResponse();
}

void logBootSummary() {
    ESP_LOGI(TAG, "Captain matrix ESP-IDF started");
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
    ESP_LOGI(TAG,
             "oled_bus=sda%u scl%u addr=%s",
             static_cast<unsigned>(OLED_I2C_SDA_PIN),
             static_cast<unsigned>(OLED_I2C_SCL_PIN),
             oledReady ? "OK" : "missing");
    ESP_LOGI(TAG,
             "diag_force_single_lamp=%u row=%u col=%u skip_switch_scan=%u",
             MATRIX_DIAG_FORCE_SINGLE_LAMP ? 1u : 0u,
             static_cast<unsigned>(MATRIX_DIAG_FORCE_ROW),
             static_cast<unsigned>(MATRIX_DIAG_FORCE_COL),
             MATRIX_DIAG_SKIP_SWITCH_SCAN ? 1u : 0u);
    ESP_LOGI(TAG,
             "diag_hold_one_lamp_dc=%u",
             MATRIX_DIAG_HOLD_ONE_LAMP_DC ? 1u : 0u);
    ESP_LOGI(TAG,
             "safe_proof_mode=%u row=%u col=%u duty=%u/%u duration_ms=%u",
             MATRIX_SAFE_PROOF_MODE ? 1u : 0u,
             static_cast<unsigned>(MATRIX_SAFE_PROOF_ROW),
             static_cast<unsigned>(MATRIX_SAFE_PROOF_COL),
             static_cast<unsigned>(MATRIX_SAFE_PROOF_ON_US),
             static_cast<unsigned>(MATRIX_SAFE_PROOF_PERIOD_US),
             static_cast<unsigned>(MATRIX_SAFE_PROOF_DURATION_MS));
    ESP_LOGI(TAG,
             "diag_guarded_boot_test=%u row=%u col=%u sweep_cols=%u step_ms=%u duty=%u/%u duration_ms=%u pulse_level=%u",
             MATRIX_DIAG_GUARDED_BOOT_TEST ? 1u : 0u,
             static_cast<unsigned>(MATRIX_DIAG_BOOT_TEST_ROW),
             static_cast<unsigned>(MATRIX_DIAG_BOOT_TEST_COL),
             MATRIX_DIAG_BOOT_TEST_SWEEP_COLS ? 1u : 0u,
             static_cast<unsigned>(MATRIX_DIAG_BOOT_TEST_COL_STEP_MS),
             static_cast<unsigned>(MATRIX_DIAG_BOOT_TEST_COLUMN_ON_US),
             static_cast<unsigned>(MATRIX_DIAG_BOOT_TEST_COLUMN_PERIOD_US),
             static_cast<unsigned>(MATRIX_DIAG_BOOT_TEST_DURATION_MS),
             static_cast<unsigned>(MATRIX_DIAG_BOOT_TEST_PULSE_LEVEL));
}

void logLinkHeartbeat() {
    const uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    if ((nowMs - matrixLastLinkLogMs) < MATRIX_LINK_LOG_MS) {
        return;
    }

    matrixLastLinkLogMs = nowMs;
    ESP_LOGI(TAG,
             "link rx_pkts=%" PRIu32 " lamp_bursts=%" PRIu32 " lamp_bytes=%" PRIu32
             " last_cmd=0x%02X pulse_us=%u lamp=[%02X,%02X,%02X,%02X,%02X] sw0=0x%02X",
             matrixI2CRxPacketCount,
             matrixLampWriteBurstCount,
             matrixLampWriteByteCount,
             static_cast<unsigned>(matrixLastCommand),
             static_cast<unsigned>(appliedLampPulseWidthUs()),
             lampRowRam[0],
             lampRowRam[1],
             lampRowRam[2],
             lampRowRam[3],
             lampRowRam[4],
             switchStateBytes[0]);
}
}  // namespace

extern "C" void app_main(void) {
    memset(lampRowRam, 0, sizeof(lampRowRam));
    memset(switchStateBytes, 0, sizeof(switchStateBytes));

    initMatrixPins();
    writeShiftRegister16(0);
    initI2CSlave();
    initOled();
    logBootSummary();

    if (MATRIX_SAFE_PROOF_MODE) {
        safeProofActive = true;
        safeProofEndUs = static_cast<uint64_t>(esp_timer_get_time()) +
                         static_cast<uint64_t>(MATRIX_SAFE_PROOF_DURATION_MS) * 1000ULL;
        matrixSystemEnabled = true;
        matrixOutputEnabled = true;
        lampPulseWidthLevel = 0;
        ESP_LOGW(TAG,
                 "safe proof active: row=%u col=%u duty=%u/%u duration_ms=%u",
                 static_cast<unsigned>(MATRIX_SAFE_PROOF_ROW),
                 static_cast<unsigned>(MATRIX_SAFE_PROOF_COL),
                 static_cast<unsigned>(MATRIX_SAFE_PROOF_ON_US),
                 static_cast<unsigned>(MATRIX_SAFE_PROOF_PERIOD_US),
                 static_cast<unsigned>(MATRIX_SAFE_PROOF_DURATION_MS));
    }

    if (MATRIX_DIAG_GUARDED_BOOT_TEST) {
        diagBootTestActive = true;
        diagBootTestEndUs = static_cast<uint64_t>(esp_timer_get_time()) +
                            static_cast<uint64_t>(MATRIX_DIAG_BOOT_TEST_DURATION_MS) * 1000ULL;
        matrixSystemEnabled = true;
        matrixOutputEnabled = true;
        lampPulseWidthLevel = MATRIX_DIAG_BOOT_TEST_PULSE_LEVEL;
        ESP_LOGW(TAG,
                 "guarded boot test active: row=%u col=%u pulse=%u duration_ms=%u",
                 static_cast<unsigned>(MATRIX_DIAG_BOOT_TEST_ROW),
                 static_cast<unsigned>(MATRIX_DIAG_BOOT_TEST_COL),
                 static_cast<unsigned>(MATRIX_DIAG_BOOT_TEST_PULSE_LEVEL),
                 static_cast<unsigned>(MATRIX_DIAG_BOOT_TEST_DURATION_MS));
    }

    if (MATRIX_DIAG_FORCE_SINGLE_LAMP) {
        matrixSystemEnabled = true;
        matrixOutputEnabled = true;
        lampPulseWidthLevel = MATRIX_DIAG_FORCE_PULSE_LEVEL;
    }

    // I2C is serviced once per full 8-row scan cycle rather than before every row
    // step. i2c_slave_read_buffer() acquires a mutex and copies the ring buffer;
    // calling it 8x per cycle adds variable overhead that shifts row timing and
    // shows as brightness variation across rows (flicker).
    uint8_t i2cServiceRowCounter = 0;

    while (true) {
        // Service I2C once per complete scan cycle (every CAPTAIN_SWITCH_ROWS steps).
        if (i2cServiceRowCounter == 0) {
            serviceI2C();
        }
        if (++i2cServiceRowCounter >= CAPTAIN_SWITCH_ROWS) {
            i2cServiceRowCounter = 0;
        }

        if (safeProofActive) {
            const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());
            if (nowUs >= safeProofEndUs) {
                safeProofActive = false;
                matrixOutputEnabled = false;
                writeShiftRegister16(composeShiftFrame(0x00, 0x00));
                ESP_LOGW(TAG, "safe proof complete: outputs forced off");
            } else {
                matrixSystemEnabled = true;
                matrixOutputEnabled = true;
            }
        }

        if (diagBootTestActive) {
            const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());
            if (nowUs >= diagBootTestEndUs) {
                diagBootTestActive = false;
                matrixOutputEnabled = false;
                writeShiftRegister16(composeShiftFrame(0x00, 0x00));
                ESP_LOGW(TAG, "guarded boot test complete: outputs forced off");
            } else {
                matrixSystemEnabled = true;
                matrixOutputEnabled = true;
                lampPulseWidthLevel = MATRIX_DIAG_BOOT_TEST_PULSE_LEVEL;
            }
        }

        if (MATRIX_DIAG_FORCE_SINGLE_LAMP) {
            matrixSystemEnabled = true;
            matrixOutputEnabled = true;
            lampPulseWidthLevel = MATRIX_DIAG_FORCE_PULSE_LEVEL;
        }

        if (!MATRIX_DIAG_SKIP_SWITCH_SCAN && !diagBootTestActive && !safeProofActive) {
            scanSwitchMatrix();
        }

        refreshLampMatrixStep();
        updateOledStatus();
        logLinkHeartbeat();
        // Yield to IDLE on a wall-clock cadence so watchdog feeding does not depend
        // on scan workload (lit-row count changes loop timing).
        {
            static uint64_t lastIdleYieldUs = 0;
            const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());
            if ((nowUs - lastIdleYieldUs) >= 2000000ULL) {
                lastIdleYieldUs = nowUs;
                vTaskDelay(1);
            }
        }
    }
}

#endif  // CAPTAIN_MATRIX_BAREBONES
