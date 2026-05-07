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
constexpr uint32_t MATRIX_LINK_LOG_MS = 5000;
// Baseline lamp on-time. Raised to improve perceived brightness with current
// scan cadence even when control sends conservative pulse levels.
constexpr uint16_t MATRIX_LAMP_PULSE_MIN_US = 300;
constexpr uint16_t MATRIX_LAMP_PULSE_STEP_US = 100;
constexpr uint16_t MATRIX_ROW_BLANK_US = 50;
constexpr uint16_t MATRIX_ROW_SETTLE_US = 150;
constexpr uint16_t MATRIX_ROW_POST_HOLD_US = 50;
constexpr uint16_t MATRIX_SWITCH_ROW_RELEASE_US = 400;
// Lamp row release: how long to hold all-off after the lamp row Phase D fires before
// scanSwitchMatrix() begins. Must be >= MATRIX_SWITCH_ROW_RELEASE_US and long enough
// for the lamp row transistor to turn off even with a loaded (lit) row bus.
constexpr uint16_t MATRIX_LAMP_ROW_RELEASE_US = 1500;
// Optional scope markers for phase timing diagnostics.
// Set enabled=true and assign free GPIOs to observe scan phase boundaries:
// - switch marker high during scanSwitchMatrix()
// - lamp marker high during refreshLampMatrixStep()
constexpr bool MATRIX_TIMING_MARKERS_ENABLED = true;
constexpr int MATRIX_TIMING_MARKER_SWITCH_GPIO = 12;
constexpr int MATRIX_TIMING_MARKER_LAMP_GPIO = 13;
constexpr uint16_t MATRIX_SWITCH_SAMPLE_GAP_US = 80;
constexpr uint8_t MATRIX_SWITCH_DEBOUNCE_TICKS = 6;
constexpr uint8_t MATRIX_ACTIVE_SWITCH_ROWS = 6;
constexpr uint8_t MATRIX_ACTIVE_LAMP_ROWS = 6;
static_assert(MATRIX_ACTIVE_SWITCH_ROWS > 0 && MATRIX_ACTIVE_SWITCH_ROWS <= CAPTAIN_SWITCH_ROWS,
              "MATRIX_ACTIVE_SWITCH_ROWS must be within protocol row bounds");
static_assert(MATRIX_ACTIVE_LAMP_ROWS > 0 && MATRIX_ACTIVE_LAMP_ROWS <= CAPTAIN_LAMP_ROWS,
              "MATRIX_ACTIVE_LAMP_ROWS must be within protocol row bounds");
// Shift-register drive mapping (set from bench results).
constexpr bool MATRIX_SR_CHAIN_IS_COL_THEN_ROW = true;
constexpr bool MATRIX_SR_ROW_ACTIVE_LOW = false;
constexpr bool MATRIX_SR_COL_ACTIVE_LOW = false;
constexpr i2c_port_t MATRIX_I2C_PORT = I2C_NUM_0;
constexpr size_t MATRIX_I2C_RX_BUFFER = 128;
constexpr size_t MATRIX_I2C_TX_BUFFER = 128;

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
uint32_t matrixLampWriteRejectCount = 0;
uint32_t matrixLampChecksumFailCount = 0;
uint32_t matrixLampChecksumOkCount = 0;
uint32_t matrixLampDeferredCount = 0;
bool matrixLampChecksumMode = false;
bool pendingLampFrameValid = false;
uint8_t pendingLampRows[CAPTAIN_LAMP_ROWS] = {};
uint32_t matrixLastLinkLogMs = 0;

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

inline bool timingMarkerConfigured(int pin) {
    return MATRIX_TIMING_MARKERS_ENABLED && pin >= 0;
}

inline void setTimingMarker(int pin, bool high) {
    if (!timingMarkerConfigured(pin)) {
        return;
    }
    gpio_set_level(static_cast<gpio_num_t>(pin), high ? 1 : 0);
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
    for (uint8_t col = 0; col < CAPTAIN_SWITCH_COLS; col++) {
        configureInputPin(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SWITCH_COL_PINS[col]));
    }

    configureOutputPin(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SR_DATA_PIN), 0);
    configureOutputPin(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SR_CLOCK_PIN), 0);
    configureOutputPin(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SR_LATCH_PIN), 0);
    // OE# is active-low. Set LOW (0) once at boot to enable shift-register outputs, leave alone forever.
    configureOutputPin(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SR_OE_N_PIN), 0);

    if (timingMarkerConfigured(MATRIX_TIMING_MARKER_SWITCH_GPIO)) {
        configureOutputPin(static_cast<gpio_num_t>(MATRIX_TIMING_MARKER_SWITCH_GPIO), 0);
    }
    if (timingMarkerConfigured(MATRIX_TIMING_MARKER_LAMP_GPIO)) {
        configureOutputPin(static_cast<gpio_num_t>(MATRIX_TIMING_MARKER_LAMP_GPIO), 0);
    }
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

uint16_t composeLampColumnShiftValue(uint8_t row, const uint8_t* lampRows = lampRowRam) {
    return static_cast<uint16_t>(lampRows[row] & 0x1Fu);
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
    setTimingMarker(MATRIX_TIMING_MARKER_LAMP_GPIO, true);
    static uint8_t row = 0;
    static uint8_t latchedLampRows[CAPTAIN_LAMP_ROWS] = {};

    // Enforce an explicit all-off phase before every row transition.
    writeShiftRegister16(composeShiftFrame(0x00, 0x00));
    delayWallUs(MATRIX_ROW_BLANK_US);

    if (!matrixSystemEnabled || !matrixOutputEnabled) {
        row = static_cast<uint8_t>((row + 1) % MATRIX_ACTIVE_LAMP_ROWS);
        setTimingMarker(MATRIX_TIMING_MARKER_LAMP_GPIO, false);
        return;
    }

    if (row == 0) {
        memcpy(latchedLampRows, lampRowRam, sizeof(latchedLampRows));
    }

    const uint8_t rowMask = static_cast<uint8_t>(1u << row);
    const uint8_t colMask = static_cast<uint8_t>(composeLampColumnShiftValue(row, latchedLampRows));

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

    // Phase D: the full MATRIX_LAMP_ROW_RELEASE_US guard is only needed on the
    // last row of a frame (row 7→0) — that is the transition directly before
    // scanSwitchMatrix() runs. Between consecutive lamp rows inside a frame a
    // short blank is sufficient because no switch scan runs between them.
    const uint8_t nextRow = static_cast<uint8_t>((row + 1) % MATRIX_ACTIVE_LAMP_ROWS);
    const uint32_t releaseUs = (nextRow == 0) ? MATRIX_LAMP_ROW_RELEASE_US : MATRIX_ROW_BLANK_US;
    writeShiftRegister16(composeShiftFrame(0x00, 0x00));
    delayWallUs(releaseUs);

    row = nextRow;
    setTimingMarker(MATRIX_TIMING_MARKER_LAMP_GPIO, false);
}

void scanSwitchMatrix() {
    setTimingMarker(MATRIX_TIMING_MARKER_SWITCH_GPIO, true);
    uint8_t sampleBits[CAPTAIN_SWITCH_BYTES] = {};

    if (!matrixSystemEnabled) {
        memset(switchStateBytes, 0, sizeof(switchStateBytes));
        memset(debounceCandidateBits, 0, sizeof(debounceCandidateBits));
        memset(debounceTickCounters, 0, sizeof(debounceTickCounters));
        setTimingMarker(MATRIX_TIMING_MARKER_SWITCH_GPIO, false);
        return;
    }

    for (uint8_t row = 0; row < MATRIX_ACTIVE_SWITCH_ROWS; row++) {
        writeShiftRegister16(composeShiftFrame(0x00, 0x00));
        delayWallUs(MATRIX_ROW_BLANK_US);

        const uint8_t rowMask = static_cast<uint8_t>(1u << row);
        writeShiftRegister16(composeShiftFrame(rowMask, 0x00));
        delayWallUs(MATRIX_ROW_SETTLE_US);

        for (uint8_t col = 0; col < CAPTAIN_SWITCH_COLS; col++) {
            const gpio_num_t pin = static_cast<gpio_num_t>(CAPTAIN_MATRIX_SWITCH_COL_PINS[col]);
            const int levelA = gpio_get_level(pin);
            delayWallUs(MATRIX_SWITCH_SAMPLE_GAP_US);
            const int levelB = gpio_get_level(pin);
            const bool closed = (levelA == 0) && (levelB == 0);
            captainSetBit(sampleBits, captainSwitchBitIndex(row, col), closed);
        }

        // Force a distinct row-release phase so the next row does not sample residual
        // charge or comparator recovery from the previous active row.
        writeShiftRegister16(composeShiftFrame(0x00, 0x00));
        delayWallUs(MATRIX_SWITCH_ROW_RELEASE_US);
    }

    writeShiftRegister16(composeShiftFrame(0x00, 0x00));

    for (uint8_t row = 0; row < MATRIX_ACTIVE_SWITCH_ROWS; row++) {
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

    for (uint8_t row = MATRIX_ACTIVE_SWITCH_ROWS; row < CAPTAIN_SWITCH_ROWS; row++) {
        for (uint8_t col = 0; col < CAPTAIN_SWITCH_COLS; col++) {
            const size_t bitIndex = captainSwitchBitIndex(row, col);
            captainSetBit(switchStateBytes, bitIndex, false);
            captainSetBit(debounceCandidateBits, bitIndex, false);
            debounceTickCounters[bitIndex] = 0;
        }
    }
    setTimingMarker(MATRIX_TIMING_MARKER_SWITCH_GPIO, false);
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
        // Accept only a full-frame write from lamp base register to avoid torn/partial updates.
        // Use >= not == because the slave byte-stream may contain trailing bytes from the next
        // transaction (e.g. a switch-read pointer byte). We clamp to CAPTAIN_LAMP_ROWS so those
        // trailing bytes are ignored.  Partial frames (< CAPTAIN_LAMP_ROWS bytes) are still rejected.
        if (command == CAPTAIN_MATRIX_REG_LAMP_BASE && payloadLength >= CAPTAIN_LAMP_ROWS) {
            // Validate XOR checksum only for exact 9-byte lamp payloads (8 rows + checksum).
            // The RX stream may merge transactions; before checksum mode is proven, a 9th byte
            // may simply be the next command byte, so ignore mismatches until a valid checksum is seen.
            const bool hasDedicatedChecksum = (payloadLength == CAPTAIN_LAMP_ROWS + 1);
            if (hasDedicatedChecksum) {
                uint8_t xorAcc = 0;
                for (uint8_t i = 0; i < CAPTAIN_LAMP_ROWS; i++) {
                    xorAcc ^= packet[i + 1];
                }
                const bool checksumMatch = (xorAcc == packet[CAPTAIN_LAMP_ROWS + 1]);
                if (checksumMatch) {
                    matrixLampChecksumMode = true;
                    matrixLampChecksumOkCount++;
                } else if (matrixLampChecksumMode) {
                    matrixLampChecksumFailCount++;
                    matrixLampWriteRejectCount++;
                    queueI2CResponse();
                    return;
                }
            }
            uint8_t stagedLampRows[CAPTAIN_LAMP_ROWS] = {};
            for (uint8_t row = 0; row < CAPTAIN_LAMP_ROWS; row++) {
                if (row < MATRIX_ACTIVE_LAMP_ROWS) {
                    stagedLampRows[row] = static_cast<uint8_t>(packet[row + 1]) & 0x1Fu;
                } else {
                    stagedLampRows[row] = 0;
                }
            }

            // Suppress single-frame corruption: require one matching frame before
            // applying a changed lamp image. Stable frames still apply continuously.
            if (!pendingLampFrameValid || memcmp(pendingLampRows, stagedLampRows, sizeof(stagedLampRows)) != 0) {
                memcpy(pendingLampRows, stagedLampRows, sizeof(pendingLampRows));
                pendingLampFrameValid = true;
                matrixLampDeferredCount++;
            } else {
                memcpy(lampRowRam, stagedLampRows, sizeof(lampRowRam));
                matrixLampWriteBurstCount++;
                matrixLampWriteByteCount += CAPTAIN_LAMP_ROWS;
            }
        } else {
            matrixLampWriteRejectCount++;
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
             "rows=%u(active=%u) cols=%u lamp_rows=%u(active=%u) lamp_cols=%u",
             static_cast<unsigned>(CAPTAIN_SWITCH_ROWS),
             static_cast<unsigned>(MATRIX_ACTIVE_SWITCH_ROWS),
             static_cast<unsigned>(CAPTAIN_SWITCH_COLS),
             static_cast<unsigned>(CAPTAIN_LAMP_ROWS),
             static_cast<unsigned>(MATRIX_ACTIVE_LAMP_ROWS),
             static_cast<unsigned>(CAPTAIN_LAMP_COLS));
    ESP_LOGI(TAG,
             "timing_markers enabled=%u switch_gpio=%d lamp_gpio=%d",
             MATRIX_TIMING_MARKERS_ENABLED ? 1u : 0u,
             MATRIX_TIMING_MARKER_SWITCH_GPIO,
             MATRIX_TIMING_MARKER_LAMP_GPIO);
}

void logLinkHeartbeat() {
    const uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    if ((nowMs - matrixLastLinkLogMs) < MATRIX_LINK_LOG_MS) {
        return;
    }

    matrixLastLinkLogMs = nowMs;
    ESP_LOGI(TAG,
             "link rx_pkts=%" PRIu32 " lamp_bursts=%" PRIu32 " lamp_bytes=%" PRIu32 " lamp_reject=%" PRIu32 " lamp_chkfail=%" PRIu32 " lamp_chkok=%" PRIu32 " chk_mode=%u "
             "lamp_defer=%" PRIu32
             " pulse_us=%u lamp=[%02X,%02X,%02X,%02X,%02X] sw0=0x%02X",
             matrixI2CRxPacketCount,
             matrixLampWriteBurstCount,
             matrixLampWriteByteCount,
             matrixLampWriteRejectCount,
             matrixLampChecksumFailCount,
             matrixLampChecksumOkCount,
             matrixLampChecksumMode ? 1u : 0u,
             matrixLampDeferredCount,
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
    logBootSummary();

    while (true) {
        // Service I2C every loop iteration to keep the slave RX/TX queues fresh.
        // Slower switch-scan timings can otherwise starve service cadence and cause
        // stale or dropped transactions on the control-board polling path.
        serviceI2C();

        scanSwitchMatrix();

        for (uint8_t lr = 0; lr < MATRIX_ACTIVE_LAMP_ROWS; lr++) {
            refreshLampMatrixStep();
        }
        logLinkHeartbeat();
        taskYIELD();
    }
}

#endif  // CAPTAIN_MATRIX_BAREBONES
