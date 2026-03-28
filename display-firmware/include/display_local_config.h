#ifndef DISPLAY_LOCAL_CONFIG_H
#define DISPLAY_LOCAL_CONFIG_H

/*
 * display_local_config.h
 * ─────────────────────────────────────────────────────────────────────────────
 * Pin assignments for the display-board peripherals: TFT (ST7796S), touch
 * controller (XPT2046), and SD card.  All three share the local SPI bus on
 * the display ESP32-C6 board.
 *
 * Board: ESP32-C6 DevKitM Amazon clone ("C6 Mini" / "SuperMini")
 *
 * This is a SEPARATE SPI bus from the host-link (GPIO4-7).  The local
 * peripheral bus uses GPIO10-14 so that display rendering and the host SPI
 * slave can run concurrently without contention.
 *
 * UART0 flash path
 * ─────────────────────────────────────────────────────────────────────────────
 * On the ESP32-C6, UART0 defaults to GPIO16 (TXD) and GPIO17 (RXD).  These
 * are kept free (not assigned here) to avoid conflicts with serial logging or
 * alternative flash paths via a USB-serial adapter.  The primary flash path
 * on these Amazon clone boards is USB-C → built-in USB-Serial/JTAG.
 *
 * GPIO12 and GPIO13 are the USB D- / D+ lines for the built-in USB-JTAG
 * controller.  Do not use them for external peripherals.
 */

#include <Arduino.h>

// ── Local SPI bus (shared by TFT, touch, SD) ─────────────────────────────────
constexpr uint8_t DISP_LOCAL_SCLK_PIN = 10;  // GPIO10 – SPI clock
constexpr uint8_t DISP_LOCAL_MOSI_PIN = 11;  // GPIO11 – MOSI to TFT/touch/SD
constexpr uint8_t DISP_LOCAL_MISO_PIN = 14;  // GPIO14 – MISO from touch/SD

// ── TFT (ST7796S) ─────────────────────────────────────────────────────────────
constexpr uint8_t DISP_TFT_CS_PIN  = 15;  // GPIO15 – TFT chip select  (active LOW)
constexpr uint8_t DISP_TFT_DC_PIN  = 20;  // GPIO20 – TFT data/command
constexpr uint8_t DISP_TFT_RST_PIN = 21;  // GPIO21 – TFT reset         (active LOW)
constexpr uint8_t DISP_TFT_BL_PIN  = 22;  // GPIO22 – Backlight PWM     (active HIGH)

// ── Touch controller (XPT2046) ────────────────────────────────────────────────
constexpr uint8_t DISP_TOUCH_CS_PIN  = 23;  // GPIO23 – Touch chip select (active LOW)
constexpr uint8_t DISP_TOUCH_IRQ_PIN = 18;  // GPIO18 – Touch IRQ         (active LOW)

// ── SD card ───────────────────────────────────────────────────────────────────
constexpr uint8_t DISP_SD_CS_PIN = 19;  // GPIO19 – SD chip select    (active LOW)

// ── Reserved / do not use ─────────────────────────────────────────────────────
// GPIO0  – HOST_REQ handshake (see host_link_config.h)   [strapping pin]
// GPIO1  – DISP_READY handshake (see host_link_config.h)
// GPIO4  – HOST_LINK_SPI_SCLK
// GPIO5  – HOST_LINK_SPI_MOSI
// GPIO6  – HOST_LINK_SPI_MISO
// GPIO7  – HOST_LINK_SPI_CS
// GPIO12 – USB D-  (built-in USB-JTAG, do not use)
// GPIO13 – USB D+  (built-in USB-JTAG, do not use)
// GPIO16 – UART0 TXD (reserved for serial log / alternate flash path)
// GPIO17 – UART0 RXD (reserved for serial log / alternate flash path)

// ── TFT SPI frequency ─────────────────────────────────────────────────────────
constexpr uint32_t DISP_TFT_SPI_FREQ_HZ   = 40000000;  // 40 MHz
constexpr uint32_t DISP_TOUCH_SPI_FREQ_HZ =  2500000;  //  2.5 MHz (XPT2046 max)
constexpr uint32_t DISP_SD_SPI_FREQ_HZ    = 25000000;  // 25 MHz

#endif  // DISPLAY_LOCAL_CONFIG_H
