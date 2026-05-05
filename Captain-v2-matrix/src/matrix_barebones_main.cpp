#ifdef CAPTAIN_MATRIX_BAREBONES

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "matrix_lamp_driver_config.h"

namespace {
constexpr char TAG[] = "mx_bare";
constexpr char TEST_PROFILE_NAME[] = "phase6_playfield_lamps_only_2026_05_05";

enum class BarebonesTestMode : uint8_t {
    Column595Isolation,
    LegacyPhasedModel,
    SingleLampPulse,
    ControlDrivenScan,      // Phase 1: canonical runtime sequence, lamp state from lampRowRam[]
    TargetLampSequence,     // Phase 3: sequential single-lamp walk of specific target lamps
};

constexpr BarebonesTestMode TEST_MODE = BarebonesTestMode::TargetLampSequence;

// Master arm switch.
// Keep false while wiring/checking; set true to run active bench patterns.
constexpr bool TEST_ENABLE_OUTPUTS = true;

// Hold OE# low while enabled so the 595 outputs behave like steady latched "drum" outputs.
constexpr bool TEST_FORCE_OE_LOW_WHEN_ENABLED = true;

// Column 595 isolation test settings.
constexpr uint16_t COLUMN_TEST_HOLD_MS = 700;
constexpr bool COLUMN_TEST_INCLUDE_AA55 = true;
constexpr bool COLUMN_TEST_WALK_ALL_8_BITS = true;

// Legacy single-lamp pulse target.

// Legacy matrixTask-style phased model settings.
constexpr uint8_t LEGACY_MODEL_ROW_INDEX = 0;  // Force Row 1 for deterministic LA trigger tests.
constexpr bool LEGACY_MODEL_WALK_COLUMNS = false;
constexpr uint8_t LEGACY_MODEL_FIXED_COL_MASK = 0x01;
constexpr uint32_t LEGACY_MODEL_ROW_BLANK_US = 2000;
constexpr uint32_t LEGACY_MODEL_ROW_SETTLE_US = 8000;
constexpr uint32_t LEGACY_MODEL_COLUMN_ON_US = 12000;
constexpr uint32_t LEGACY_MODEL_ROW_OFF_DEADTIME_US = 8000;
constexpr uint32_t LEGACY_MODEL_INTER_STEP_MS = 20;

constexpr uint8_t TEST_ROW_INDEX = 1;  // Row 2 (0-based)
constexpr uint8_t TEST_COL_INDEX = 3;  // Col 4 / L19 path (0-based)
constexpr bool TEST_WALK_LAMPS = true;
constexpr uint8_t TEST_WALK_ROW_COUNT = 8;
constexpr uint8_t TEST_WALK_COL_COUNT = 4;
constexpr uint32_t TEST_WALK_BLANK_US = 2000;
constexpr uint32_t TEST_WALK_ROW_SETTLE_US = 20000;

// Single-lamp pulse settings.
constexpr uint32_t TEST_PERIOD_US = 1000000;  // 1.0 s per lamp step for faster troubleshooting
constexpr uint32_t TEST_ON_US = 180000;       // Reduced ON time to protect bulbs during stabilization
constexpr uint32_t TEST_BOOT_WINDOW_MS = 0;   // Run continuously

// ---------------------------------------------------------------------------
// Phase 3: TargetLampSequence — sequential single-lamp walk of named targets.
//
// Lamps are taken from captain_mapping.h CAPTAIN_LAMP_NAMES[row][col]:
//   L2  B              : row 2, col 1
//   L13 7K Bonus       : row 3, col 2
//   L22 Return Lane R  : row 0, col 4
//
// Each lamp gets a 4-phase cycle: blank → row settle → row+col dwell → all-off.
// The inter-lamp gap provides a clear separator on the logic analyzer.
// ---------------------------------------------------------------------------
constexpr uint32_t TARGET_SEQ_DWELL_MS       = 500;    // Lamp-on dwell per lamp (0.5 s)
constexpr uint32_t TARGET_SEQ_BLANK_US       = 2000;   // All-off blank before row step
constexpr uint32_t TARGET_SEQ_ROW_SETTLE_US  = 20000;  // Row-only settle before column
constexpr uint32_t TARGET_SEQ_INTER_LAMP_US  = 200000; // All-off gap between lamp steps

struct TargetLampEntry {
    uint8_t     row;
    uint8_t     col;
    const char* name;
};

// Target lamps for this test phase (row and col are 0-based indices into lamp RAM).
// Col 0 is unused/not wired (maps to physical 0x00 after remap), so all entries use col 1-4.
// Ball (B1-B5), Player (P1-P4), and Game Over are driven by the control board — excluded here.
constexpr TargetLampEntry kTargetLamps[] = {
    // Row 0
    {0, 2, "L12_8K_Bonus"},
    {0, 3, "L11_9K_Bonus"},
    {0, 4, "L22_Return_Lane_R"},
    // Row 1
    {1, 1, "L1_A"},
    {1, 2, "L14_6K_Bonus"},
    {1, 3, "L6_Target1"},
    {1, 4, "L19_1K_Bonus"},
    // Row 2
    {2, 1, "L2_B"},
    {2, 2, "L15_5K_Bonus"},
    {2, 3, "L7_Double_Bonus"},
    {2, 4, "L21_Return_Lane_L"},
    // Row 3
    {3, 1, "L3_C"},
    {3, 2, "L13_7K_Bonus"},
    {3, 3, "L10_10K_Bonus"},
    {3, 4, "L18_2K_Bonus"},
    // Row 4
    {4, 1, "L4_D"},
    {4, 2, "L17_3K_Bonus"},
    {4, 3, "L9_Target2"},
    {4, 4, "L20_Same_Player"},
    // Row 5 (B5 excluded — control board)
    {5, 1, "L5_Target3"},
    {5, 2, "L16_4K_Bonus"},
    {5, 3, "L8_Triple_Bonus"},
};

// ControlDrivenScan mode: canonical runtime sequence from lampRowRam[]
// Uses proven bring-up polarity (SR_CHAIN_IS_COL_THEN_ROW=true, active-high).
// Row scan: blank -> row settle -> row+col pulse -> all-off -> next row.
// Pulse width steps match the production formula (MIN + LEVEL * STEP).
constexpr uint8_t CDS_LAMP_ROWS = 8;
constexpr uint16_t CDS_ROW_BLANK_US  = 100;    // Fast scan blanking for persistence-of-vision
constexpr uint16_t CDS_ROW_SETTLE_US = 100;    // Fast row settle for persistence-of-vision
constexpr uint16_t CDS_PULSE_MIN_US  = 50;
constexpr uint16_t CDS_PULSE_STEP_US = 50;
constexpr uint8_t  CDS_PULSE_LEVEL   = 4;      // Default level 4 => 250 µs pulse (matches production default)
constexpr uint16_t CDS_COLUMN_ON_US = 700;     // Slightly brighter fast multiplex on-time
constexpr uint16_t CDS_ROW_POST_HOLD_US = 100; // Small row hold for clean phase separation
constexpr bool CDS_ENABLE_SERIAL_CONTROL = false;
constexpr uint32_t CDS_SERIAL_STATUS_LOG_MS = 1000;
constexpr int CDS_UART_RX_BUFFER_SIZE = 256;
constexpr bool CDS_ENABLE_SCRIPT_CONTROL = true;
// Matrix pattern source: mirrors control_main.cpp::writeMatrixCommand().
constexpr bool CDS_USE_MATRIX_CONTROL_PATTERN = true;
constexpr uint32_t CDS_MATRIX_PATTERN_BLINK_MS = 350;
// Contract emulator: model a row-byte register window at 0x00..0x07.
// Scripted sources write here first; scan consumes lampRam copied from this window.
constexpr bool CDS_ENABLE_CONTRACT_EMULATOR = true;
constexpr uint32_t CDS_CONTRACT_WRITE_PERIOD_MS = 20;
// Optional fallback column-chase source.
constexpr uint32_t CDS_ATTRACT_LOOP_PERIOD_MS = 1000;
constexpr uint32_t CDS_ATTRACT_MIN_STEP_MS = 20;
constexpr uint8_t CDS_ATTRACT_CHASE_LAPS = 1;
// Initial test pattern: column 0 lit on every row so all rows show a visible lamp.
// Set to 0 for fully dark; change per-row to test specific positions.
constexpr uint8_t CDS_INITIAL_LAMP_RAM[CDS_LAMP_ROWS] = {
    0x01,  // row 0: col 0 on
    0x01,  // row 1: col 0 on
    0x01,  // row 2: col 0 on
    0x01,  // row 3: col 0 on
    0x01,  // row 4: col 0 on
    0x01,  // row 5: col 0 on
    0x01,  // row 6: col 0 on
    0x01,  // row 7: col 0 on
};

void applyAttractFrame(uint32_t nowMs, uint8_t* lampRam) {
    if (lampRam == nullptr) {
        return;
    }

    constexpr uint8_t kCols = 5;
    const uint8_t laps = (CDS_ATTRACT_CHASE_LAPS == 0) ? 1 : CDS_ATTRACT_CHASE_LAPS;
    const uint32_t totalSlots = static_cast<uint32_t>(kCols) * laps;
    const uint32_t slotMsRaw = CDS_ATTRACT_LOOP_PERIOD_MS / totalSlots;
    const uint32_t slotMs = (slotMsRaw < CDS_ATTRACT_MIN_STEP_MS) ? CDS_ATTRACT_MIN_STEP_MS : slotMsRaw;
    const uint32_t loopMs = slotMs * totalSlots;

    const uint32_t phaseMs = nowMs % loopMs;
    const uint32_t slotIndex = phaseMs / slotMs;
    const uint32_t inSlotMs = phaseMs % slotMs;

    (void)inSlotMs;
    const uint8_t targetCol = static_cast<uint8_t>(slotIndex % kCols);
    const uint8_t colMask = static_cast<uint8_t>(1u << targetCol);

    // Drive one column across all rows so column activity is obvious on LA
    // while still exercising multiplex row scanning at full speed.
    for (uint8_t row = 0; row < CDS_LAMP_ROWS; row++) {
        lampRam[row] = colMask;
    }
}

void applyMatrixControlPattern(uint32_t nowMs, uint8_t* lampRam) {
    if (lampRam == nullptr) {
        return;
    }

    for (uint8_t row = 0; row < CDS_LAMP_ROWS; row++) {
        lampRam[row] = 0x00;
    }

    // Phase 3 target lamps from captain_mapping.h:
    //   L2  B              : row 2, col 1  (static)
    //   L13 7K Bonus       : row 3, col 2  (static)
    //   L22 Return Lane R  : row 0, col 4  (static)
    lampRam[2] |= static_cast<uint8_t>(1u << 1);  // L2  B
    lampRam[3] |= static_cast<uint8_t>(1u << 2);  // L13 7K Bonus
    lampRam[0] |= static_cast<uint8_t>(1u << 4);  // L22 Return Lane R

    // Blink L13 and L22 so they are easy to identify on a live lamp rail.
    const bool blink = ((nowMs / CDS_MATRIX_PATTERN_BLINK_MS) % 2u) != 0u;
    if (!blink) {
        lampRam[3] &= static_cast<uint8_t>(~(1u << 2));  // L13 off
        lampRam[0] &= static_cast<uint8_t>(~(1u << 4));  // L22 off
    }
}

struct MatrixContractWindow {
    uint8_t pointer;
    uint8_t rows[CDS_LAMP_ROWS];
};

void initContractWindow(MatrixContractWindow* window, const uint8_t* seedRows) {
    if (window == nullptr) {
        return;
    }

    window->pointer = 0;
    for (uint8_t i = 0; i < CDS_LAMP_ROWS; i++) {
        window->rows[i] = (seedRows != nullptr) ? seedRows[i] : 0x00;
    }
}

void contractSetPointer(MatrixContractWindow* window, uint8_t regAddress) {
    if (window == nullptr) {
        return;
    }

    window->pointer = static_cast<uint8_t>(regAddress & 0x07u);
}

void contractWriteSequential(MatrixContractWindow* window, const uint8_t* data, size_t len) {
    if (window == nullptr || data == nullptr || len == 0) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        window->rows[window->pointer] = static_cast<uint8_t>(data[i] & 0x1Fu);
        window->pointer = static_cast<uint8_t>((window->pointer + 1u) & 0x07u);
    }
}

void copyContractRowsToLampRam(const MatrixContractWindow* window, uint8_t* lampRam) {
    if (window == nullptr || lampRam == nullptr) {
        return;
    }

    for (uint8_t i = 0; i < CDS_LAMP_ROWS; i++) {
        lampRam[i] = static_cast<uint8_t>(window->rows[i] & 0x1Fu);
    }
}

// Hardware mapping switches for quick A/B.
constexpr bool SR_CHAIN_IS_COL_THEN_ROW = true;
constexpr bool SR_ROW_ACTIVE_LOW = false;
constexpr bool SR_COL_ACTIVE_LOW = false;

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

void delayUsCooperative(uint32_t delayUs) {
    constexpr uint32_t kMaxChunkUs = 50000;

    while (delayUs > 0) {
        if (delayUs < 1000) {
            esp_rom_delay_us(delayUs);
            break;
        }

        uint32_t chunkUs = (delayUs > kMaxChunkUs) ? kMaxChunkUs : delayUs;
        TickType_t ticks = pdMS_TO_TICKS(chunkUs / 1000U);
        if (ticks == 0) {
            ticks = 1;
        }

        vTaskDelay(ticks);

        const uint32_t sleptUs = static_cast<uint32_t>(ticks) * portTICK_PERIOD_MS * 1000U;
        if (sleptUs >= delayUs) {
            delayUs = 0;
        } else {
            delayUs -= sleptUs;
        }
    }
}

// Logical col0 is unused/not wired. Shift logical cols 1..4 down to physical bits 0..3.
static inline uint8_t remapLogicalToPhysicalCols(uint8_t logicalColMask) {
    return static_cast<uint8_t>((logicalColMask >> 1) & 0x0Fu);
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
    // OE# is active-low. Keep high in lockout; force low when actively testing.
    const int oeLevel = (TEST_ENABLE_OUTPUTS && TEST_FORCE_OE_LOW_WHEN_ENABLED) ? 0 : 1;
    configureOutputPin(static_cast<gpio_num_t>(CAPTAIN_MATRIX_SR_OE_N_PIN), oeLevel);
}

void logConfig() {
    ESP_LOGI(TAG, "test_profile=%s", TEST_PROFILE_NAME);
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
    ESP_LOGI(TAG,
             "mode=%s oe_forced_low_when_enabled=%u column_hold_ms=%u walk8=%u aa55=%u",
             TEST_MODE == BarebonesTestMode::Column595Isolation
                 ? "column_595_isolation"
                 : (TEST_MODE == BarebonesTestMode::LegacyPhasedModel
                        ? "legacy_phased_model"
                        : (TEST_MODE == BarebonesTestMode::ControlDrivenScan
                               ? "control_driven_scan"
                               : (TEST_MODE == BarebonesTestMode::TargetLampSequence
                                      ? "target_lamp_sequence"
                                      : "single_lamp_pulse"))),
             TEST_FORCE_OE_LOW_WHEN_ENABLED ? 1u : 0u,
             static_cast<unsigned>(COLUMN_TEST_HOLD_MS),
             COLUMN_TEST_WALK_ALL_8_BITS ? 1u : 0u,
             COLUMN_TEST_INCLUDE_AA55 ? 1u : 0u);
    ESP_LOGI(TAG,
             "legacy_model row=%u walk_columns=%u fixed_col_mask=0x%02X blank_us=%" PRIu32
             " settle_us=%" PRIu32 " on_us=%" PRIu32 " deadtime_us=%" PRIu32 " inter_step_ms=%" PRIu32,
             static_cast<unsigned>(LEGACY_MODEL_ROW_INDEX),
             LEGACY_MODEL_WALK_COLUMNS ? 1u : 0u,
             static_cast<unsigned>(LEGACY_MODEL_FIXED_COL_MASK),
             LEGACY_MODEL_ROW_BLANK_US,
             LEGACY_MODEL_ROW_SETTLE_US,
             LEGACY_MODEL_COLUMN_ON_US,
             LEGACY_MODEL_ROW_OFF_DEADTIME_US,
             LEGACY_MODEL_INTER_STEP_MS);
    ESP_LOGI(TAG,
             "cds script=%u matrix_pattern=%u contract_emu=%u contract_period_ms=%" PRIu32,
             CDS_ENABLE_SCRIPT_CONTROL ? 1u : 0u,
             CDS_USE_MATRIX_CONTROL_PATTERN ? 1u : 0u,
             CDS_ENABLE_CONTRACT_EMULATOR ? 1u : 0u,
             CDS_CONTRACT_WRITE_PERIOD_MS);
    ESP_LOGW(TAG, "output_lockout=%u (set TEST_ENABLE_OUTPUTS=true only after fuse-safe checks)",
             TEST_ENABLE_OUTPUTS ? 0u : 1u);
}

void runColumn595IsolationLoop() {
    const uint8_t rowMaskAllOff = 0x00;
    static const uint8_t kPatterns[] = {
        0x00,
        0xAA,
        0x55,
        0x01,
        0x02,
        0x04,
        0x08,
        0x10,
        0x20,
        0x40,
        0x80,
        0x00,
    };

    while (true) {
        for (size_t i = 0; i < sizeof(kPatterns); i++) {
            const uint8_t colMask = kPatterns[i];
            if (!COLUMN_TEST_INCLUDE_AA55 && (colMask == 0xAA || colMask == 0x55)) {
                continue;
            }
            if (!COLUMN_TEST_WALK_ALL_8_BITS && colMask >= 0x20) {
                continue;
            }

            const uint16_t frame = composeShiftFrame(rowMaskAllOff, colMask);
            writeShiftRegister16(frame);
            ESP_LOGI(TAG, "column_595_latch col_mask=0x%02X frame=0x%04X", static_cast<unsigned>(colMask),
                     static_cast<unsigned>(frame));
            delayUsCooperative(static_cast<uint32_t>(COLUMN_TEST_HOLD_MS) * 1000U);
        }
    }
}

void runSingleLampPulseLoop() {
    const uint8_t rowMask = static_cast<uint8_t>(1u << TEST_ROW_INDEX);
    const uint8_t colMask = static_cast<uint8_t>(1u << TEST_COL_INDEX);
    const uint16_t onFrame = composeShiftFrame(rowMask, colMask);
    const uint16_t offFrame = composeShiftFrame(0x00, 0x00);
    const uint64_t startUs = static_cast<uint64_t>(esp_timer_get_time());

    while (true) {
        if (TEST_WALK_LAMPS) {
            for (uint8_t row = 0; row < TEST_WALK_ROW_COUNT; row++) {
                for (uint8_t col = 0; col < TEST_WALK_COL_COUNT; col++) {
                    const uint16_t rowOnlyFrame = composeShiftFrame(static_cast<uint8_t>(1u << row), 0x00);
                    const uint16_t walkOnFrame = composeShiftFrame(static_cast<uint8_t>(1u << row),
                                                                   static_cast<uint8_t>(1u << col));
                    ESP_LOGI(TAG, "lamp_walk row=%u col=%u frame=0x%04X", static_cast<unsigned>(row),
                             static_cast<unsigned>(col), static_cast<unsigned>(walkOnFrame));

                    // Phase A: explicit blanking before row/column step.
                    writeShiftRegister16(offFrame);
                    delayUsCooperative(TEST_WALK_BLANK_US);

                    // Phase B: row-only settle so slow row drivers are stable before enabling column.
                    writeShiftRegister16(rowOnlyFrame);
                    delayUsCooperative(TEST_WALK_ROW_SETTLE_US);

                    // Phase C: row+column on.
                    writeShiftRegister16(walkOnFrame);
                    delayUsCooperative(TEST_ON_US);

                    // Phase D: all off.
                    writeShiftRegister16(offFrame);
                    delayUsCooperative(TEST_PERIOD_US - TEST_ON_US);
                }
            }
            continue;
        }

        const uint64_t nowUs = static_cast<uint64_t>(esp_timer_get_time());
        if (TEST_BOOT_WINDOW_MS > 0) {
            const uint64_t elapsedMs = (nowUs - startUs) / 1000ULL;
            if (elapsedMs >= TEST_BOOT_WINDOW_MS) {
                writeShiftRegister16(composeShiftFrame(0x00, 0x00));
                delayUsCooperative(20000);
                continue;
            }
        }

        writeShiftRegister16(onFrame);
        delayUsCooperative(TEST_ON_US);

        writeShiftRegister16(offFrame);
        delayUsCooperative(TEST_PERIOD_US - TEST_ON_US);
    }
}

void runLegacyPhasedModelLoop() {
    const uint8_t rowMask = static_cast<uint8_t>(1u << LEGACY_MODEL_ROW_INDEX);
    static const uint8_t kColumnWalkMasks[] = {0x01, 0x02, 0x04, 0x08, 0x10};

    while (true) {
        for (size_t idx = 0; idx < sizeof(kColumnWalkMasks); idx++) {
            const uint8_t colMask = LEGACY_MODEL_WALK_COLUMNS ? kColumnWalkMasks[idx] : LEGACY_MODEL_FIXED_COL_MASK;

            // Phase 1: explicit all-off blanking before any row transition.
            writeShiftRegister16(composeShiftFrame(0x00, 0x00));
            delayUsCooperative(LEGACY_MODEL_ROW_BLANK_US);

            // Phase 2: row selected, columns off.
            writeShiftRegister16(composeShiftFrame(rowMask, 0x00));
            delayUsCooperative(LEGACY_MODEL_ROW_SETTLE_US);

            // Phase 3: same row, selected columns on.
            writeShiftRegister16(composeShiftFrame(rowMask, colMask));
            delayUsCooperative(LEGACY_MODEL_COLUMN_ON_US);

            // Phase 4: columns off while row still selected.
            writeShiftRegister16(composeShiftFrame(rowMask, 0x00));
            delayUsCooperative(LEGACY_MODEL_ROW_OFF_DEADTIME_US);

            // Phase 5: row off before next step.
            writeShiftRegister16(composeShiftFrame(0x00, 0x00));
            ESP_LOGI(TAG, "legacy_step row=%u col_mask=0x%02X", static_cast<unsigned>(LEGACY_MODEL_ROW_INDEX),
                     static_cast<unsigned>(colMask));
            delayUsCooperative(LEGACY_MODEL_INTER_STEP_MS * 1000U);

            if (!LEGACY_MODEL_WALK_COLUMNS) {
                break;
            }
        }
    }
}
// ---------------------------------------------------------------------------
// Phase 3: TargetLampSequence — sequential single-lamp walk.
//
// Iterates through kTargetLamps[], activating one lamp at a time with the
// proven 4-phase sequence so each can be verified individually:
//
//   Phase A  all-off blank        TARGET_SEQ_BLANK_US
//   Phase B  row-only settle      TARGET_SEQ_ROW_SETTLE_US
//   Phase C  row + column dwell   TARGET_SEQ_DWELL_MS
//   Phase D  all-off release      TARGET_SEQ_INTER_LAMP_US
//
// Serial log lines use "target_seq" tag for easy grep/scope trigger.
// ---------------------------------------------------------------------------
void runTargetLampSequenceLoop() {
    constexpr size_t kLampCount = sizeof(kTargetLamps) / sizeof(kTargetLamps[0]);
    const uint16_t offFrame = composeShiftFrame(0x00, 0x00);

    ESP_LOGI(TAG,
             "target_seq_start lamp_count=%u dwell_ms=%" PRIu32 " blank_us=%" PRIu32
             " settle_us=%" PRIu32 " inter_us=%" PRIu32,
             static_cast<unsigned>(kLampCount),
             TARGET_SEQ_DWELL_MS,
             TARGET_SEQ_BLANK_US,
             TARGET_SEQ_ROW_SETTLE_US,
             TARGET_SEQ_INTER_LAMP_US);

    while (true) {
        for (size_t i = 0; i < kLampCount; i++) {
            const uint8_t rowMask        = static_cast<uint8_t>(1u << kTargetLamps[i].row);
            const uint8_t logicalColMask  = static_cast<uint8_t>(1u << kTargetLamps[i].col);
            const uint8_t colMask         = remapLogicalToPhysicalCols(logicalColMask);
            const uint16_t rowOnlyFrame = composeShiftFrame(rowMask, 0x00);
            const uint16_t lampFrame    = composeShiftFrame(rowMask, colMask);

            ESP_LOGI(TAG,
                     "target_seq lamp=%s row=%u col=%u row_frame=0x%04X lamp_frame=0x%04X",
                     kTargetLamps[i].name,
                     static_cast<unsigned>(kTargetLamps[i].row),
                     static_cast<unsigned>(kTargetLamps[i].col),
                     static_cast<unsigned>(rowOnlyFrame),
                     static_cast<unsigned>(lampFrame));

            // Phase A: all-off blank before row transition.
            writeShiftRegister16(offFrame);
            delayUsCooperative(TARGET_SEQ_BLANK_US);

            // Phase B: row selected, columns off — let row driver settle.
            writeShiftRegister16(rowOnlyFrame);
            delayUsCooperative(TARGET_SEQ_ROW_SETTLE_US);

            // Phase C: row + column on — lamp energised for dwell period.
            writeShiftRegister16(lampFrame);
            delayUsCooperative(TARGET_SEQ_DWELL_MS * 1000UL);

            // Phase D: all off — inter-lamp gap.
            writeShiftRegister16(offFrame);
            delayUsCooperative(TARGET_SEQ_INTER_LAMP_US);
        }

        // Yield to RTOS watchdog once per full lamp cycle.
        vTaskDelay(1);
    }
}

// ---------------------------------------------------------------------------
// Phase 1: ControlDrivenScan - canonical production sequence in barebones.
//
// Implements the authoritative row-scan loop outside app_main so it can be
// validated independently before any changes to the production runtime.
//
// State sequence per row (matches production refreshLampMatrixStep intent):
//   Phase A  all-off blank        CDS_ROW_BLANK_US
//   Phase B  row-only settle      CDS_ROW_SETTLE_US
//   Phase C  row + column pulse   CDS_PULSE_MIN_US + level * CDS_PULSE_STEP_US
//   Phase D  all-off release      (immediate, then advance row)
//
// Lamp state lives in lampRam[8]. In this first phase the array is pre-loaded
// with CDS_INITIAL_LAMP_RAM[]. A later phase will wire I2C or serial writes
// to this array so control-board intent drives the scan live.
//
// Polarity uses proven bring-up values (SR_CHAIN_IS_COL_THEN_ROW=true,
// active-high row and column). Reconciliation with app_main constants is a
// separate step after hardware validation here.
// ---------------------------------------------------------------------------
void runControlDrivenScanLoop() {
    uint8_t lampRam[CDS_LAMP_ROWS];
    for (uint8_t i = 0; i < CDS_LAMP_ROWS; i++) {
        lampRam[i] = CDS_INITIAL_LAMP_RAM[i];
    }
    MatrixContractWindow contractWindow = {};
    initContractWindow(&contractWindow, lampRam);

    auto trim = [](char* s) {
        size_t n = strlen(s);
        while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || isspace(static_cast<unsigned char>(s[n - 1])))) {
            s[n - 1] = '\0';
            n--;
        }
        size_t start = 0;
        while (s[start] != '\0' && isspace(static_cast<unsigned char>(s[start]))) {
            start++;
        }
        if (start > 0) {
            memmove(s, s + start, strlen(s + start) + 1);
        }
    };

    auto logLampRam = [&](const char* prefix) {
        ESP_LOGI(TAG,
                 "%s lamp_ram=[%02X %02X %02X %02X %02X %02X %02X %02X]",
                 prefix,
                 static_cast<unsigned>(lampRam[0]),
                 static_cast<unsigned>(lampRam[1]),
                 static_cast<unsigned>(lampRam[2]),
                 static_cast<unsigned>(lampRam[3]),
                 static_cast<unsigned>(lampRam[4]),
                 static_cast<unsigned>(lampRam[5]),
                 static_cast<unsigned>(lampRam[6]),
                 static_cast<unsigned>(lampRam[7]));
    };

    auto printHelp = [&]() {
        ESP_LOGI(TAG, "cds_cmds: SHOW | CLEAR | ALL <mask_hex> | ROW <row0_7> <mask_hex>");
    };

    auto parseMask = [](const char* token, uint8_t* outMask) -> bool {
        if (token == nullptr || outMask == nullptr || token[0] == '\0') {
            return false;
        }

        unsigned value = 0;
        if ((token[0] == '0') && (token[1] == 'x' || token[1] == 'X')) {
            if (sscanf(token, "%x", &value) != 1) {
                return false;
            }
        } else {
            // Treat unprefixed values as hex for convenience during bench work.
            if (sscanf(token, "%x", &value) != 1) {
                return false;
            }
        }

        value &= 0x1Fu;
        *outMask = static_cast<uint8_t>(value);
        return true;
    };

    uint16_t columnOnUs = CDS_COLUMN_ON_US;
    uint64_t lastStatusLogMs = 0;
    uint64_t lastContractWriteMs = 0;
    bool contractWritePrimed = false;
    uint8_t scriptRows[CDS_LAMP_ROWS] = {};
    char serialLine[96] = {};
    size_t serialLen = 0;
    bool serialControlActive = CDS_ENABLE_SERIAL_CONTROL;

    const uint16_t pulseUs = static_cast<uint16_t>(CDS_PULSE_MIN_US + CDS_PULSE_LEVEL * CDS_PULSE_STEP_US);
    ESP_LOGI(TAG, "cds_start rows=%u blank_us=%u settle_us=%u pulse_us=%u col_on_us=%u row_post_hold_us=%u",
             static_cast<unsigned>(CDS_LAMP_ROWS),
             static_cast<unsigned>(CDS_ROW_BLANK_US),
             static_cast<unsigned>(CDS_ROW_SETTLE_US),
             static_cast<unsigned>(pulseUs),
             static_cast<unsigned>(CDS_COLUMN_ON_US),
             static_cast<unsigned>(CDS_ROW_POST_HOLD_US));
    if (serialControlActive) {
        if (!uart_is_driver_installed(UART_NUM_0)) {
            const esp_err_t uartErr = uart_driver_install(UART_NUM_0, CDS_UART_RX_BUFFER_SIZE, 0, 0, nullptr, 0);
            if (uartErr != ESP_OK) {
                ESP_LOGW(TAG, "cds_serial disabled: uart_driver_install failed err=0x%x",
                         static_cast<unsigned>(uartErr));
                serialControlActive = false;
            }
        }
    }

    if (serialControlActive) {
        printHelp();
        logLampRam("cds_init");
    } else {
        ESP_LOGI(TAG, "cds_serial=disabled");
    }
    if (CDS_ENABLE_SCRIPT_CONTROL) {
        ESP_LOGI(TAG,
                 "cds_script enabled source=%s ingest=%s",
                 CDS_USE_MATRIX_CONTROL_PATTERN ? "matrix_control_pattern" : "column_chase_fallback",
                 CDS_ENABLE_CONTRACT_EMULATOR ? "contract_window_0x00_0x07" : "direct_lamp_ram");
        if (CDS_USE_MATRIX_CONTROL_PATTERN) {
            ESP_LOGI(TAG, "cds_matrix_pattern blink_ms=%u", static_cast<unsigned>(CDS_MATRIX_PATTERN_BLINK_MS));
        } else {
            ESP_LOGI(TAG,
                     "cds_attract loop_ms=%u min_step_ms=%u laps=%u",
                     static_cast<unsigned>(CDS_ATTRACT_LOOP_PERIOD_MS),
                     static_cast<unsigned>(CDS_ATTRACT_MIN_STEP_MS),
                     static_cast<unsigned>(CDS_ATTRACT_CHASE_LAPS));
        }
        logLampRam("cds_script_init");
    }

    uint8_t row = 0;
    while (true) {
        if (serialControlActive) {
            uint8_t rx = 0;
            while (true) {
                const int readResult = uart_read_bytes(UART_NUM_0, &rx, 1, 0);
                if (readResult < 0) {
                    ESP_LOGW(TAG, "cds_serial disabled: uart_read_bytes error=%d", readResult);
                    serialControlActive = false;
                    break;
                }
                if (readResult == 0) {
                    break;
                }

                if (rx == '\r' || rx == '\n') {
                    if (serialLen == 0) {
                        continue;
                    }

                    serialLine[serialLen] = '\0';
                    trim(serialLine);

                    if (serialLine[0] != '\0') {
                        if (strcasecmp(serialLine, "SHOW") == 0) {
                            logLampRam("cds_show");
                        } else if (strcasecmp(serialLine, "CLEAR") == 0) {
                            for (uint8_t i = 0; i < CDS_LAMP_ROWS; i++) {
                                lampRam[i] = 0x00;
                            }
                            logLampRam("cds_clear");
                        } else {
                            char cmd[16] = {};
                            char arg1[16] = {};
                            char arg2[16] = {};
                            const int tokenCount = sscanf(serialLine, "%15s %15s %15s", cmd, arg1, arg2);

                            if (tokenCount >= 2 && strcasecmp(cmd, "ALL") == 0) {
                                uint8_t mask = 0;
                                if (parseMask(arg1, &mask)) {
                                    for (uint8_t i = 0; i < CDS_LAMP_ROWS; i++) {
                                        lampRam[i] = mask;
                                    }
                                    logLampRam("cds_all");
                                } else {
                                    ESP_LOGW(TAG, "cds_cmd invalid mask: %s", arg1);
                                }
                            } else if (tokenCount >= 3 && strcasecmp(cmd, "ROW") == 0) {
                                unsigned rowIndex = 0;
                                uint8_t mask = 0;
                                if (sscanf(arg1, "%u", &rowIndex) == 1 && rowIndex < CDS_LAMP_ROWS &&
                                    parseMask(arg2, &mask)) {
                                    lampRam[rowIndex] = mask;
                                    ESP_LOGI(TAG, "cds_row_set row=%u mask=0x%02X", rowIndex,
                                             static_cast<unsigned>(mask));
                                } else {
                                    ESP_LOGW(TAG, "cds_cmd invalid ROW args: %s %s", arg1, arg2);
                                }
                            } else if (tokenCount >= 2 && strcasecmp(cmd, "ONUS") == 0) {
                                unsigned onUs = 0;
                                if (sscanf(arg1, "%u", &onUs) == 1 && onUs >= 500 && onUs <= 50000) {
                                    columnOnUs = static_cast<uint16_t>(onUs);
                                    ESP_LOGI(TAG, "cds_onus=%u", onUs);
                                } else {
                                    ESP_LOGW(TAG, "cds_cmd invalid ONUS: %s (allowed 500..50000)", arg1);
                                }
                            } else if (strcasecmp(serialLine, "HELP") == 0) {
                                printHelp();
                            } else {
                                ESP_LOGW(TAG, "cds_cmd unknown: %s", serialLine);
                                printHelp();
                            }
                        }
                    }

                    serialLen = 0;
                    continue;
                }

                if (serialLen < sizeof(serialLine) - 1) {
                    serialLine[serialLen++] = static_cast<char>(rx);
                } else {
                    serialLen = 0;
                }
            }

            const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
            if ((nowMs - lastStatusLogMs) >= CDS_SERIAL_STATUS_LOG_MS) {
                lastStatusLogMs = nowMs;
                ESP_LOGI(TAG, "cds_status row=%u on_us=%u", static_cast<unsigned>(row),
                         static_cast<unsigned>(columnOnUs));
            }
        }

        const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);

        if (CDS_ENABLE_SCRIPT_CONTROL) {
            if (CDS_USE_MATRIX_CONTROL_PATTERN) {
                applyMatrixControlPattern(static_cast<uint32_t>(nowMs), scriptRows);
            } else {
                applyAttractFrame(static_cast<uint32_t>(nowMs), scriptRows);
            }

            if (CDS_ENABLE_CONTRACT_EMULATOR) {
                if (!contractWritePrimed || (nowMs - lastContractWriteMs) >= CDS_CONTRACT_WRITE_PERIOD_MS) {
                    // Emulate a control-board style write burst to row-byte window 0x00..0x07.
                    contractSetPointer(&contractWindow, 0x00);
                    contractWriteSequential(&contractWindow, scriptRows, CDS_LAMP_ROWS);
                    lastContractWriteMs = nowMs;
                    contractWritePrimed = true;
                }
            } else {
                for (uint8_t i = 0; i < CDS_LAMP_ROWS; i++) {
                    lampRam[i] = scriptRows[i];
                }
            }
        }

        if (CDS_ENABLE_CONTRACT_EMULATOR) {
            copyContractRowsToLampRam(&contractWindow, lampRam);
        }

        // Phase A: all-off blank before every row transition.
        writeShiftRegister16(composeShiftFrame(0x00, 0x00));
        delayUsCooperative(CDS_ROW_BLANK_US);

        // Phase B: row-only settle so row drivers are stable before column enable.
        const uint8_t rowMask = static_cast<uint8_t>(1u << row);
        writeShiftRegister16(composeShiftFrame(rowMask, 0x00));
        delayUsCooperative(CDS_ROW_SETTLE_US);

        // Phase C: row + active columns from lamp RAM (lower 5 bits = cols 0-4).
        const uint8_t colMask = lampRam[row] & 0x1Fu;
        if (colMask != 0) {
            writeShiftRegister16(composeShiftFrame(rowMask, colMask));
            delayUsCooperative(columnOnUs);

            // Hold row after column pulse so LA clearly shows row does not drop
            // at the same instant as column activity.
            writeShiftRegister16(composeShiftFrame(rowMask, 0x00));
            delayUsCooperative(CDS_ROW_POST_HOLD_US);
        }

        // Phase D: all-off release after pulse.
        writeShiftRegister16(composeShiftFrame(0x00, 0x00));

        ESP_LOGD(TAG, "cds_row row=%u col_mask=0x%02X",
                 static_cast<unsigned>(row), static_cast<unsigned>(colMask));

        row = static_cast<uint8_t>((row + 1u) % CDS_LAMP_ROWS);

        // Yield to RTOS once per full 8-row cycle so watchdog stays fed.
        if (row == 0) {
            vTaskDelay(1);
        }
    }
}

} // namespace

extern "C" void app_main(void) {
    initPins();
    logConfig();

    if (!TEST_ENABLE_OUTPUTS) {
        writeShiftRegister16(composeShiftFrame(0x00, 0x00));
        while (true) {
            delayUsCooperative(50000);
        }
    }

    if (TEST_MODE == BarebonesTestMode::Column595Isolation) {
        runColumn595IsolationLoop();
    } else if (TEST_MODE == BarebonesTestMode::LegacyPhasedModel) {
        runLegacyPhasedModelLoop();
    } else if (TEST_MODE == BarebonesTestMode::ControlDrivenScan) {
        runControlDrivenScanLoop();
    } else if (TEST_MODE == BarebonesTestMode::TargetLampSequence) {
        runTargetLampSequenceLoop();
    } else {
        runSingleLampPulseLoop();
    }
}

#endif  // CAPTAIN_MATRIX_BAREBONES
