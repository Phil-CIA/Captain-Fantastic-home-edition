#ifndef DISPLAY_LOCAL_CONFIG_H
#define DISPLAY_LOCAL_CONFIG_H

// =============================================================================
// Display-Board Local Peripheral GPIO Configuration
// Target: ESP32-C6 DevKitM (Amazon clone, "C6 Mini")
// =============================================================================
//
// GPIO0-1  are reserved for handshake lines (see host_link_config.h).
// GPIO4-7  are reserved for host-link SPI slave (MISO/CS/MOSI/CLK).
// GPIO2-3  are available for additional local use (not used by host-link).
// GPIO8/9  are strapping pins — keep free of external load (see FLASHING_NOTES.md).
// GPIO10+  are available for local peripherals (TFT, touch controller, SD card).
//
// Adjust pin numbers below to match your specific wiring.  The values here are
// reasonable defaults based on the physical header layout of the C6 DevKitM.
//
// =============================================================================

#include <Arduino.h>

// ─── Local TFT / display controller ─────────────────────────────────────────
constexpr uint8_t DISP_TFT_CS_PIN    = 10;
constexpr uint8_t DISP_TFT_DC_PIN    = 11;   // Data/Command select
constexpr uint8_t DISP_TFT_RST_PIN   = 12;   // Active-LOW reset (can share with touch)
constexpr uint8_t DISP_TFT_BL_PIN    = 13;   // Backlight PWM (optional)

// Local SPI bus for TFT + touch + SD (separate from host-link SPI on GPIO4-7):
constexpr uint8_t DISP_LOCAL_SPI_MOSI_PIN = 20;
constexpr uint8_t DISP_LOCAL_SPI_MISO_PIN = 21;
constexpr uint8_t DISP_LOCAL_SPI_CLK_PIN  = 22;

// ─── Touch controller (e.g., XPT2046) ───────────────────────────────────────
constexpr uint8_t DISP_TOUCH_CS_PIN  = 14;
constexpr uint8_t DISP_TOUCH_IRQ_PIN = 15;   // Active-LOW interrupt (input, with pull-up)

// ─── SD card (optional) ──────────────────────────────────────────────────────
constexpr uint8_t DISP_SD_CS_PIN     = 23;

// ─── Local SPI speed limits ───────────────────────────────────────────────────
constexpr uint32_t DISP_TFT_SPI_FREQ_HZ   = 40000000UL;   // 40 MHz for ST7796
constexpr uint32_t DISP_TOUCH_SPI_FREQ_HZ =  2500000UL;   //  2.5 MHz for XPT2046
constexpr uint32_t DISP_SD_SPI_FREQ_HZ    = 25000000UL;   // 25 MHz for SD

#endif // DISPLAY_LOCAL_CONFIG_H
