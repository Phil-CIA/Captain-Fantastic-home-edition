#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "driver/i2c.h"

#include "captain_mapping.h"
#include "captain_protocol.h"
#include "i2c_bus_config.h"
#include "matrix_lamp_driver_config.h"

namespace {
constexpr char TAG[] = "captain_matrix";
constexpr uint32_t LOOP_DELAY_US = 5000;
constexpr uint16_t MATRIX_LAMP_PULSE_MIN_US = 50;
constexpr uint16_t MATRIX_LAMP_PULSE_STEP_US = 50;
constexpr uint8_t MATRIX_SWITCH_DEBOUNCE_TICKS = 4;
constexpr i2c_port_t MATRIX_I2C_PORT = I2C_NUM_0;
constexpr size_t MATRIX_I2C_RX_BUFFER = 128;
constexpr size_t MATRIX_I2C_TX_BUFFER = 128;

uint8_t lampRowRam[CAPTAIN_LAMP_ROWS] = {};
uint8_t switchStateBytes[CAPTAIN_SWITCH_BYTES] = {};
uint8_t debounceCandidateBits[CAPTAIN_SWITCH_BYTES] = {};
uint8_t debounceTickCounters[CAPTAIN_SWITCH_ROWS * CAPTAIN_SWITCH_COLS] = {};
uint8_t registerPointer = CAPTAIN_MATRIX_REG_SWITCH_BASE;
uint8_t lampPulseWidthLevel = CAPTAIN_MATRIX_DEFAULT_PULSE_WIDTH_LEVEL;
bool matrixSystemEnabled = false;
bool matrixOutputEnabled = false;

uint16_t appliedLampPulseWidthUs() {
    return static_cast<uint16_t>(MATRIX_LAMP_PULSE_MIN_US +
                                 static_cast<uint16_t>(lampPulseWidthLevel) * MATRIX_LAMP_PULSE_STEP_US);
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
    uint16_t value = 0;
    const uint8_t rowValue = lampRowRam[row];
    const bool col0 = (rowValue & captainMatrixLampRowMask(0)) != 0;
    const bool col1 = (rowValue & captainMatrixLampRowMask(1)) != 0;
    const bool col2 = (rowValue & captainMatrixLampRowMask(2)) != 0;
    const bool col3 = (rowValue & captainMatrixLampRowMask(3)) != 0;
    const bool col4 = (rowValue & captainMatrixLampRowMask(4)) != 0;

    if (col0) value |= static_cast<uint16_t>(1u << CAPTAIN_MATRIX_SR_BIT_LAMP_COL0);
    if (col1) value |= static_cast<uint16_t>(1u << CAPTAIN_MATRIX_SR_BIT_LAMP_COL1);
    if (col2) value |= static_cast<uint16_t>(1u << CAPTAIN_MATRIX_SR_BIT_LAMP_COL2);
    if (col3) value |= static_cast<uint16_t>(1u << CAPTAIN_MATRIX_SR_BIT_LAMP_COL3);
    if (col4) value |= static_cast<uint16_t>(1u << CAPTAIN_MATRIX_SR_BIT_LAMP_COL4);

    return value;
}

void refreshLampMatrixStep() {
    static uint8_t row = 0;

    for (uint8_t r = 0; r < CAPTAIN_SWITCH_ROWS; r++) {
        gpio_set_level(static_cast<gpio_num_t>(CAPTAIN_MATRIX_ROW_PINS[r]), 1);
    }

    if (!matrixSystemEnabled || !matrixOutputEnabled) {
        writeShiftRegister16(0);
        row = static_cast<uint8_t>((row + 1) % CAPTAIN_SWITCH_ROWS);
        return;
    }

    const uint16_t shiftValue = composeLampColumnShiftValue(row);
    writeShiftRegister16(shiftValue);

    gpio_set_level(static_cast<gpio_num_t>(CAPTAIN_MATRIX_ROW_PINS[row]), 0);
    esp_rom_delay_us(appliedLampPulseWidthUs());
    gpio_set_level(static_cast<gpio_num_t>(CAPTAIN_MATRIX_ROW_PINS[row]), 1);

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
        for (uint8_t r = 0; r < CAPTAIN_SWITCH_ROWS; r++) {
            gpio_set_level(static_cast<gpio_num_t>(CAPTAIN_MATRIX_ROW_PINS[r]), 1);
        }

        gpio_set_level(static_cast<gpio_num_t>(CAPTAIN_MATRIX_ROW_PINS[row]), 0);
        esp_rom_delay_us(10);

        for (uint8_t col = 0; col < CAPTAIN_SWITCH_COLS; col++) {
            const int level = gpio_get_level(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SWITCH_COL_PINS[col]));
            const bool closed = (level == 0);
            captainSetBit(sampleBits, captainSwitchBitIndex(row, col), closed);
        }
    }

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
    const size_t payloadLength = length - 1;

    if ((command & 0xFEu) == CAPTAIN_MATRIX_CMD_SYSTEM_SETUP && payloadLength == 0) {
        matrixSystemEnabled = (command & CAPTAIN_MATRIX_CMD_SYSTEM_ENABLE) != 0;
        if (!matrixSystemEnabled) {
            writeShiftRegister16(0);
        }
        queueI2CResponse();
        return;
    }

    if ((command & 0xFEu) == CAPTAIN_MATRIX_CMD_OUTPUT_SETUP && payloadLength == 0) {
        matrixOutputEnabled = (command & CAPTAIN_MATRIX_CMD_OUTPUT_ENABLE) != 0;
        if (!matrixOutputEnabled) {
            writeShiftRegister16(0);
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
        uint8_t target = command;
        for (size_t index = 1; index < length && captainMatrixLampRegister(target); index++, target++) {
            lampRowRam[target - CAPTAIN_MATRIX_REG_LAMP_BASE] = static_cast<uint8_t>(packet[index]) & 0x1Fu;
        }
    }

    queueI2CResponse();
}

void serviceI2C() {
    uint8_t rxPacket[32] = {};
    const int bytesRead = i2c_slave_read_buffer(MATRIX_I2C_PORT, rxPacket, sizeof(rxPacket), 0);
    if (bytesRead > 0) {
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
}

void logLoopHeartbeat() {
    static uint32_t loopCounter = 0;
    loopCounter++;
    if ((loopCounter % 200) == 0) {
        ESP_LOGI(TAG,
                 "alive loops=%" PRIu32 " system=%u output=%u pulse=%u lamp0=0x%02X sw0=0x%02X",
                 loopCounter,
                 matrixSystemEnabled ? 1u : 0u,
                 matrixOutputEnabled ? 1u : 0u,
                 static_cast<unsigned>(appliedLampPulseWidthUs()),
                 lampRowRam[0],
                 switchStateBytes[0]);
    }
}
}

extern "C" void app_main(void) {
    memset(lampRowRam, 0, sizeof(lampRowRam));
    memset(switchStateBytes, 0, sizeof(switchStateBytes));

    initMatrixPins();
    writeShiftRegister16(0);
    initI2CSlave();
    logBootSummary();

    while (true) {
        serviceI2C();
        scanSwitchMatrix();
        refreshLampMatrixStep();
        logLoopHeartbeat();
        esp_rom_delay_us(LOOP_DELAY_US);
    }
}