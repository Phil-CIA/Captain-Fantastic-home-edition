#include "w25q64.h"

void W25Q64::begin() {
    pinMode(W25Q64_CS, OUTPUT);
    digitalWrite(W25Q64_CS, HIGH);
    delay(10); // Allow flash to power up (tVSL spec = 10ms typical)
    
    SPI.begin(W25Q64_SCK, W25Q64_MISO, W25Q64_MOSI, W25Q64_CS);
    SPI.setFrequency(1000000); // 1MHz - SLOW speed to rule out signal integrity
    SPI.setDataMode(SPI_MODE0);
    SPI.setBitOrder(MSBFIRST);
    
    delay(1); // Additional stabilization after SPI init
    
    // Read JEDEC ID to verify communication
    digitalWrite(W25Q64_CS, LOW);
    SPI.transfer(0x9F); // Read JEDEC ID
    uint8_t mfg = SPI.transfer(0x00);
    uint8_t type = SPI.transfer(0x00);
    uint8_t capacity = SPI.transfer(0x00);
    digitalWrite(W25Q64_CS, HIGH);
    Serial.printf("[W25Q64] JEDEC ID: 0x%02X%02X%02X (expect 0xEF4017 for W25Q64)\n", mfg, type, capacity);
    
    // AGGRESSIVE: Clear ALL protection bits in BOTH status registers
    // Try combined write (some chips support writing SR1+SR2 together)
    writeEnable();
    digitalWrite(W25Q64_CS, LOW);
    SPI.transfer(0x01); // Write Status Register
    SPI.transfer(0x00); // SR1: Clear all bits
    SPI.transfer(0x00); // SR2: Clear all bits (if supported)
    digitalWrite(W25Q64_CS, HIGH);
    delay(50); // Longer wait for dual-register write
    
    uint8_t status = readStatus();
    Serial.printf("[W25Q64] Status after init: 0x%02X (expect 0x00)\n", status);
    
    // Read Status Register-2 for additional info
    digitalWrite(W25Q64_CS, LOW);
    SPI.transfer(0x35); // Read Status Register-2
    uint8_t status2 = SPI.transfer(0x00);
    digitalWrite(W25Q64_CS, HIGH);
    Serial.printf("[W25Q64] Status-2: 0x%02X\n", status2);
}

uint8_t W25Q64::readStatus() {
    digitalWrite(W25Q64_CS, LOW);
    delayMicroseconds(1); // tSLCH timing
    SPI.transfer(0x05); // Read Status Register-1
    delayMicroseconds(1); // tV timing - output valid delay
    uint8_t status = SPI.transfer(0x00);
    digitalWrite(W25Q64_CS, HIGH);
    delayMicroseconds(1); // tSHCL timing
    return status;
}

void W25Q64::writeEnable() {
    digitalWrite(W25Q64_CS, LOW);
    delayMicroseconds(1); // tSLCH timing
    SPI.transfer(0x06); // Write Enable
    digitalWrite(W25Q64_CS, HIGH);
    delayMicroseconds(1); // tSHCL timing
    
    // Verify WEL bit is set
    uint8_t status = readStatus();
    if ((status & 0x02) == 0) {
        Serial.printf("[W25Q64] WARNING: Write Enable failed! Status: 0x%02X (WEL bit not set)\n", status);
    }
}

void W25Q64::chipErase() {
    writeEnable();
    digitalWrite(W25Q64_CS, LOW);
    SPI.transfer(0xC7); // Chip Erase
    digitalWrite(W25Q64_CS, HIGH);
    delay(100);
}

void W25Q64::writeByte(uint32_t addr, uint8_t data) {
    writeEnable();
    digitalWrite(W25Q64_CS, LOW);
    SPI.transfer(0x02); // Page Program
    SPI.transfer((addr >> 16) & 0xFF);
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    SPI.transfer(data);
    digitalWrite(W25Q64_CS, HIGH);
    delay(5);
}

uint8_t W25Q64::readByte(uint32_t addr) {
    waitBusy(); // Ensure any previous operation is complete
    
    digitalWrite(W25Q64_CS, LOW);
    delayMicroseconds(1); // tSLCH timing
    SPI.transfer(0x03); // Read Data
    SPI.transfer((addr >> 16) & 0xFF);
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    delayMicroseconds(1); // tV timing - output valid delay
    uint8_t data = SPI.transfer(0x00);
    digitalWrite(W25Q64_CS, HIGH);
    delayMicroseconds(1); // tSHCL timing
    return data;
}

void W25Q64::waitBusy() {
    while (readStatus() & 0x01) {
        delay(1);
    }
}

void W25Q64::eraseSector(uint32_t addr) {
    writeEnable();
    waitBusy();
    digitalWrite(W25Q64_CS, LOW);
    SPI.transfer(0x20); // Sector Erase (4KB)
    SPI.transfer((addr >> 16) & 0xFF);
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    digitalWrite(W25Q64_CS, HIGH);
    waitBusy();
}

void W25Q64::writeBytes(uint32_t addr, const uint8_t* data, size_t len) {
    
    size_t written = 0;
    
    while (written < len) {
        // Calculate bytes remaining in current page
        size_t pageOffset = addr % W25Q64_PAGE_SIZE;
        size_t pageRemaining = W25Q64_PAGE_SIZE - pageOffset;
        size_t toWrite = min(pageRemaining, len - written);
        
        writeEnable();
        waitBusy();
        
        digitalWrite(W25Q64_CS, LOW);
        SPI.transfer(0x02); // Page Program
        SPI.transfer((addr >> 16) & 0xFF);
        SPI.transfer((addr >> 8) & 0xFF);
        SPI.transfer(addr & 0xFF);
        
        for (size_t i = 0; i < toWrite; i++) {
            SPI.transfer(data[written + i]);
        }
        
        digitalWrite(W25Q64_CS, HIGH);
        waitBusy();
        
        // Small delay for power stability (100µF capacitor installed on VCC)
        delay(5);
        
        written += toWrite;
        addr += toWrite;
    }
}

void W25Q64::readBytes(uint32_t addr, uint8_t* data, size_t len) {
    waitBusy(); // Ensure any previous operation is complete
    
    digitalWrite(W25Q64_CS, LOW);
    delayMicroseconds(1); // tSLCH timing
    SPI.transfer(0x03); // Read Data
    SPI.transfer((addr >> 16) & 0xFF);
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    delayMicroseconds(1); // tV timing - output valid delay
    
    for (size_t i = 0; i < len; i++) {
        data[i] = SPI.transfer(0x00);
    }
    
    digitalWrite(W25Q64_CS, HIGH);
    delayMicroseconds(1); // tSHCL timing
}
