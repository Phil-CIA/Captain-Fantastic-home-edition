#ifndef DISPLAY_LOCAL_CONFIG_H
#define DISPLAY_LOCAL_CONFIG_H

// =============================================================================
// display-firmware/include/display_local_config.h
//
// Pin configuration for all LOCAL peripherals on the ESP32-C6 display board:
//   - TFT (ST7796S) driven by the ESP32-C6 as SPI MASTER
//   - Touch controller (XPT2046) on the same SPI bus
//   - SD card on the same SPI bus (separate CS)
//   - Backlight PWM
//
// The host-link SPI slave (GPIO4–7) is defined in host_link_config.h.
// Change pins in THIS file only; all driver source files include it.
// =============================================================================

#include <Arduino.h>

// -----------------------------------------------------------------------------
// LOCAL SPI BUS — display board master (TFT + touch + SD)
//
// SPI2 (HSPI on classic ESP32; GPSPI2 on ESP32-C6) is used for local
// peripherals.  The host-link slave uses a separate peripheral (SPI3/VSPI or
// SPI2 in slave mode — see platformio.ini and host_link.cpp for details).
//
// All three devices share MOSI, MISO, and SCK; CS lines are independent.
// -----------------------------------------------------------------------------
constexpr uint8_t DISP_LOCAL_SPI_MOSI_PIN = 19;
constexpr uint8_t DISP_LOCAL_SPI_MISO_PIN = 20;
constexpr uint8_t DISP_LOCAL_SPI_SCK_PIN  = 21;

// -----------------------------------------------------------------------------
// TFT — ST7796S (SPI master, 3.5" 480×320)
// -----------------------------------------------------------------------------
constexpr uint8_t DISP_TFT_CS_PIN  = 10;   // Chip select  (active-LOW)
constexpr uint8_t DISP_TFT_DC_PIN  = 11;   // Data/command (HIGH=data, LOW=cmd)
constexpr uint8_t DISP_TFT_RST_PIN = 14;   // Reset        (active-LOW, tie HIGH if unused)

constexpr uint32_t DISP_TFT_SPI_FREQ_HZ = 40000000UL;  // 40 MHz; reduce to 20 MHz if artifacts

// TFT_eSPI User_Setup.h equivalent constants (used if TFT_eSPI reads this file).
// When using TFT_eSPI, copy these values into User_Setup.h:
//   #define TFT_MOSI  DISP_LOCAL_SPI_MOSI_PIN
//   #define TFT_MISO  DISP_LOCAL_SPI_MISO_PIN
//   #define TFT_SCLK  DISP_LOCAL_SPI_SCK_PIN
//   #define TFT_CS    DISP_TFT_CS_PIN
//   #define TFT_DC    DISP_TFT_DC_PIN
//   #define TFT_RST   DISP_TFT_RST_PIN
//   #define ST7796_DRIVER
//   #define SPI_FREQUENCY  DISP_TFT_SPI_FREQ_HZ

// -----------------------------------------------------------------------------
// TOUCH — XPT2046 (SPI master, shares bus with TFT)
// -----------------------------------------------------------------------------
constexpr uint8_t DISP_TOUCH_CS_PIN  = 15;   // Chip select  (active-LOW)
// GPIO22 is used for touch IRQ to keep GPIO16 and GPIO17 free for UART0
// (the fallback UART flashing path on ESP32-C6 uses GPIO16=RX, GPIO17=TX).
constexpr uint8_t DISP_TOUCH_IRQ_PIN = 22;   // Touch IRQ    (active-LOW, optional)

constexpr uint32_t DISP_TOUCH_SPI_FREQ_HZ = 2500000UL;  // 2.5 MHz (XPT2046 max is ~2.5 MHz)

// -----------------------------------------------------------------------------
// SD CARD — shared SPI bus, dedicated CS
// -----------------------------------------------------------------------------
// GPIO23 is used for SD CS to keep GPIO17 free for UART0 TX (UART flash path).
constexpr uint8_t DISP_SD_CS_PIN = 23;

constexpr uint32_t DISP_SD_SPI_FREQ_HZ = 25000000UL;  // 25 MHz

// -----------------------------------------------------------------------------
// BACKLIGHT PWM
// -----------------------------------------------------------------------------
constexpr uint8_t  DISP_BACKLIGHT_PIN      = 18;
constexpr uint8_t  DISP_BACKLIGHT_CHANNEL  =  0;   // LEDC channel 0
constexpr uint32_t DISP_BACKLIGHT_FREQ_HZ  = 5000; // 5 kHz (above audible range)
constexpr uint8_t  DISP_BACKLIGHT_BITS     =  8;   // 8-bit resolution (0–255)
constexpr uint8_t  DISP_BACKLIGHT_DEFAULT  = 200;  // ~78% brightness on startup

// -----------------------------------------------------------------------------
// SCREEN GEOMETRY
// -----------------------------------------------------------------------------
constexpr uint16_t DISP_SCREEN_WIDTH  = 480;
constexpr uint16_t DISP_SCREEN_HEIGHT = 320;
constexpr uint8_t  DISP_ROTATION      = 1;    // 0=portrait, 1=landscape, 2/3=flipped

#endif // DISPLAY_LOCAL_CONFIG_H
