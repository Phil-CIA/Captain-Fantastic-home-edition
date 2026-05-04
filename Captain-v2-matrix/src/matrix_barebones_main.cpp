#ifdef CAPTAIN_MATRIX_BAREBONES

#include <inttypes.h>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "matrix_lamp_driver_config.h"

namespace {
constexpr char TAG[] = "mx_bare";
constexpr char TEST_PROFILE_NAME[] = "phase1_control_driven_scan_2026_05_04";

enum class BarebonesTestMode : uint8_t {
    Column595Isolation,
    LegacyPhasedModel,
    SingleLampPulse,
    ControlDrivenScan,  // Phase 1: canonical runtime sequence, lamp state from lampRowRam[]
};

constexpr BarebonesTestMode TEST_MODE = BarebonesTestMode::ControlDrivenScan;

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

// ControlDrivenScan mode: canonical runtime sequence from lampRowRam[]
// Uses proven bring-up polarity (SR_CHAIN_IS_COL_THEN_ROW=true, active-high).
// Row scan: blank -> row settle -> row+col pulse -> all-off -> next row.
// Pulse width steps match the production formula (MIN + LEVEL * STEP).
constexpr uint8_t CDS_LAMP_ROWS = 8;
constexpr uint16_t CDS_ROW_BLANK_US  = 2000;   // Proven safe from bring-up baseline
constexpr uint16_t CDS_ROW_SETTLE_US = 20000;  // Proven safe from bring-up baseline
constexpr uint16_t CDS_PULSE_MIN_US  = 50;
constexpr uint16_t CDS_PULSE_STEP_US = 50;
constexpr uint8_t  CDS_PULSE_LEVEL   = 4;      // Default level 4 => 250 µs pulse (matches production default)
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
                        : (TEST_MODE == BarebonesTestMode::ControlDrivenScan ? "control_driven_scan"
                                                                              : "single_lamp_pulse")),
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

    const uint16_t pulseUs = static_cast<uint16_t>(CDS_PULSE_MIN_US + CDS_PULSE_LEVEL * CDS_PULSE_STEP_US);
    ESP_LOGI(TAG, "cds_start rows=%u blank_us=%u settle_us=%u pulse_us=%u",
             static_cast<unsigned>(CDS_LAMP_ROWS),
             static_cast<unsigned>(CDS_ROW_BLANK_US),
             static_cast<unsigned>(CDS_ROW_SETTLE_US),
             static_cast<unsigned>(pulseUs));

    uint8_t row = 0;
    while (true) {
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
            delayUsCooperative(pulseUs);
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
    } else {
        runSingleLampPulseLoop();
    }
}

#endif  // CAPTAIN_MATRIX_BAREBONES
