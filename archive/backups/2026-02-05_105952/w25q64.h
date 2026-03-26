#ifndef W25Q64_H
#define W25Q64_H

#include <Arduino.h>
#include <SPI.h>

// W25Q32JVSS actual pins from hardware
#define W25Q64_CS   32  // GPIO32 - Chip Select
#define W25Q64_MOSI 26  // GPIO26 - DI (Data In)
#define W25Q64_MISO 27  // GPIO27 - DO (Data Out)
#define W25Q64_SCK  33  // GPIO33 - CLK

// W25Q64 has 8MB (8 * 1024 * 1024 bytes)
#define W25Q64_SIZE         (8 * 1024 * 1024)
#define W25Q64_PAGE_SIZE    256
#define W25Q64_SECTOR_SIZE  4096

// Memory map for OTA staging area
#define W25Q64_FIRMWARE_STAGING_ADDR    0x000000  // Start at beginning, reserve 2MB for firmware
#define W25Q64_FIRMWARE_MAX_SIZE        (2 * 1024 * 1024)
#define W25Q64_MP3_STORAGE_ADDR         0x200000  // 2MB offset, rest for MP3 files

class W25Q64 {
public:
    void begin();
    uint8_t readStatus();
    void writeEnable();
    void chipErase();
    void eraseSector(uint32_t addr);
    void writeByte(uint32_t addr, uint8_t data);
    uint8_t readByte(uint32_t addr);
    
    // Bulk operations for OTA
    void writeBytes(uint32_t addr, const uint8_t* data, size_t len);
    void readBytes(uint32_t addr, uint8_t* data, size_t len);
    void waitBusy();
};

#endif // W25Q64_H
