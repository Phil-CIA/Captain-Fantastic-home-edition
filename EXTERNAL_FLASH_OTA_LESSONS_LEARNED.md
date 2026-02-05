# External Flash OTA - Lessons Learned

## Project Overview
Successfully implemented wireless Over-The-Air (OTA) firmware updates for ESP32 pinball controller using external W25Q64 8MB SPI flash memory. This allows 850KB+ firmware updates without requiring large internal flash partitions.

---

## Critical Issues Discovered & Solutions

### 1. **W25Q64 Power Brownout During Bulk Writes**
**Problem:** Initial W25Q64 chip failed catastrophically - JEDEC ID changed from 0xEF4017 to 0x000000 (dead chip). Write verification showed bit 7 failures (0xE9 → 0x69), indicating power brownout during high-current write operations.

**Root Cause:** W25Q64 current spikes during page program operations (up to 25mA) exceeded what the AMS1117 LDO + decoupling caps could deliver stably at ~200mA total load.

**Solution:** 
- Added **100µF 16V capacitor directly to W25Q64 VCC pin**
- Replaced damaged W25Q64 chip
- Added 5ms inter-page delays in write operations for power stability
- Result: New chip works perfectly, all diagnostics PASS

**Lesson:** External SPI flash requires **dedicated bulk decoupling** (100µF+) near the chip, not just PCB-level decoupling. Power supply must handle peak currents during write cycles.

---

### 2. **Sector Erase Race Condition**
**Problem:** Firmware writes succeeded for first ~131KB, then all data became 0xFF. The corruption occurred exactly at sector boundary 0x020000 (sector 32).

**Root Cause:** Original implementation erased sectors "on-demand" during HTTP upload using `eraseNextSectorIfNeeded()`. This created a race condition:
- HTTP chunks arrived faster than sector erases could complete
- Code attempted to write to non-erased sectors
- W25Q64 can only flip 1→0 bits; writing to non-0xFF data silently fails
- Writes appeared successful (no errors) but data was corrupted

**Failed Approach:** Tried erasing ahead incrementally during `writeChunk()` calls - still raced with incoming HTTP data.

**Solution:** **Pre-erase ALL sectors before accepting any data**:
```cpp
// In startDownload() - BEFORE any HTTP data arrives:
uint32_t sectorsNeeded = (totalSize + W25Q64_SECTOR_SIZE - 1) / W25Q64_SECTOR_SIZE;
for (uint32_t i = 0; i < sectorsNeeded; i++) {
    _flash->eraseSector(W25Q64_FIRMWARE_STAGING_ADDR + (i * W25Q64_SECTOR_SIZE));
}
// NOW all sectors are guaranteed erased before writes begin
```

**Lesson:** Flash memory requires **synchronous erase-before-write** - never assume erase will complete in time when racing with asynchronous data arrival. Pre-erase everything upfront.

---

### 3. **Browser Cache Breaking OTA Updates**
**Problem:** After rebuilding firmware, uploading via web interface repeatedly showed same corrupted checksum (0xF470A4FD) despite fresh compilation. Progress display showed 650% (incorrect percentage calculation).

**Root Cause:** 
- Browser cached both file selection AND file contents
- Even though firmware.bin was rebuilt, browser served old cached data
- Progress calculation used `firmwareSize` which was updated by image parser AFTER download

**Solution:**
1. Copy fresh firmware.bin to different location with new name before upload
2. Added separate `expectedSize` field for progress calculation:
```cpp
struct OTAMetadata {
    uint32_t firmwareSize;   // Actual validated size (for flashing)
    uint32_t expectedSize;   // Expected size from Content-Length (for progress)
    // ...
};
```

**Lesson:** Web development best practice - **always force cache invalidation** when testing updated binaries. Use unique filenames or cache-busting headers.

---

### 4. **ESP32 Image Structure Parsing**
**Problem:** Initial flash validation failed with "invalid segment length 0xffffffff" even though download completed. Later, checksum validation failed with "Calculated 0x7d read 0xff" - last byte missing.

**Root Cause:** Firmware binary structure wasn't properly understood:
- ESP32 images have: header (24 bytes) → segments (header + data each) → padding → checksum byte
- Image parser tried to calculate exact size by summing segments + 1 byte
- This excluded alignment padding between last segment and checksum

**Solution:** Use actual `bytesWritten` as firmware size instead of calculating from segment table:
```cpp
// Simple and correct - flash the entire downloaded file
_metadata.firmwareSize = _metadata.bytesWritten;
```

**Lesson:** Don't over-engineer binary format parsing when you have the complete file size. The bootloader validates structure - your job is just to deliver all bytes.

---

### 5. **Multipart Upload Content-Length Mismatch**
**Problem:** HTTP Content-Length reported 2097152 bytes (2MB) but actual firmware was 853KB, causing over-allocation and confusing size displays.

**Root Cause:** Multipart form-data includes boundary markers, headers, and padding that inflate Content-Length beyond actual file size.

**Solution:** Accept size mismatch and validate against actual data received:
```cpp
// Don't trust Content-Length for multipart - use actual bytesWritten
if (_metadata.bytesWritten + len > 2 * 1024 * 1024) {  // Hard limit only
    return false;
}
```

**Lesson:** For multipart uploads, Content-Length is **approximate**. Track actual bytes received and use that for all size-dependent logic.

---

## Architecture Decisions That Worked

### 1. **Two-Stage Download-Then-Flash Design**
Instead of streaming directly to internal flash:
1. Download entire firmware to W25Q64 external flash
2. Verify integrity completely
3. Then flash from W25Q64 → internal partition

**Benefits:**
- Can retry flashing without re-downloading
- External flash survives failed flash attempts
- Better error handling and recovery
- Allows firmware validation before committing

### 2. **ESP-IDF Native OTA API**
Used `esp_ota_begin()` / `esp_ota_write()` / `esp_ota_end()` instead of Arduino Update library.

**Benefits:**
- Direct control over partition selection
- Better error reporting (ESP_ERR codes)
- Automatic bootloader integration
- Handles partition table management

### 3. **Metadata Persistence with Preferences**
Saved download state to NVS (Non-Volatile Storage) via Preferences library.

**Benefits:**
- Survives reboots during download
- Enables resume capability (future)
- Tracks OTA history
- Simple key-value API

---

## Performance Characteristics

### Timing Measurements
- **Sector Erase:** ~40ms per 4KB sector
- **Page Write:** ~3ms per 256-byte page + 5ms stability delay
- **Full Pre-Erase:** ~20 seconds for 512 sectors (2MB)
- **Download:** ~30 seconds for 853KB @ WiFi speeds
- **Flash to Internal:** ~15 seconds for 853KB
- **Total OTA Time:** ~65 seconds end-to-end

### Resource Usage
- **RAM:** 48KB used (14.8% of 327KB) - no increase from OTA code
- **Flash:** 847KB program size (86.2% of 983KB partition)
- **External Flash:** 853KB staging area @ 0x000000-0x0D0000
- **SPI Speed:** 1MHz (conservative for reliability)

---

## Hardware Requirements Validated

### Minimum Power Supply
- **3.3V Rail:** Must handle 200mA base + 25mA flash write spikes
- **Decoupling:** 100µF bulk capacitor at W25Q64 VCC (mandatory)
- **LDO:** AMS1117 3.3V confirmed sufficient with proper decoupling

### SPI Configuration
- **Clock:** 1MHz (reliable), could increase to 10MHz if tested
- **Mode:** SPI_MODE0 (CPOL=0, CPHA=0)
- **Wiring:** CS=GPIO32, CLK=33, MOSI=26, MISO=27
- **Pull-ups:** Not required (chip has internal pulls)

---

## Code Quality Improvements Made

### Debug Logging Strategy
**Original:** Verbose logging on every operation (100+ lines per OTA)
**Improved:** 
- Progress checkpoints every 128KB
- Erase progress every 256KB  
- Essential status messages only
- Result: ~15 lines total output for successful OTA

### Error Handling
- All W25Q64 operations verify WEL (Write Enable Latch) status
- First write verified byte-by-byte for hardware validation
- Image parser validates ESP32 binary structure
- Checksum verification before flashing
- Graceful fallback if OTA fails (stays on working partition)

---

## Testing Methodology That Worked

### Hardware Validation Sequence
1. JEDEC ID check (0xEF4017 = W25Q64)
2. Status register verification (WEL, BUSY bits)
3. Erase → Write → Read test at multiple addresses
4. Power-on self-test at boot

### OTA Testing Progression
1. Unit test W25Q64 driver in isolation
2. Test download without flashing
3. Test flash with known-good firmware from external flash
4. End-to-end OTA with serial monitoring
5. Power-cycle testing after successful OTA
6. Repeated OTA cycles to verify stability

---

## Future Improvements Identified

### Could Be Enhanced
1. **Resume Capability:** Save download progress and resume after power loss
2. **Compression:** GZIP firmware before storage (could save 40%+ space)
3. **Delta Updates:** Only flash changed sectors for faster updates
4. **Multiple Firmware Slots:** Store 2-3 firmware versions in 8MB flash
5. **Rollback on Failure:** Auto-revert if new firmware crashes
6. **Progress Websocket:** Real-time progress updates via websocket instead of polling

### Optimization Opportunities
- Increase SPI clock to 10MHz after stability testing
- Reduce erase scope if firmware < 1MB (currently pre-erases 2MB)
- Pipeline erase operations during download (complex but possible)
- Use DMA for SPI transfers (hardware permitting)

---

## Documentation Created
- `EXTERNAL_FLASH_OTA_LESSONS_LEARNED.md` (this file)
- `DISPLAY_BUFFER_REFERENCE.md` (pre-existing)
- `DISPLAY_IMPLEMENTATION_SUMMARY.md` (pre-existing)
- `OTA_SYSTEM_ARCHITECTURE.md` (recommended to create)

---

## Key Takeaways

1. **Hardware constraints are real** - inadequate power caused permanent chip damage
2. **Race conditions in embedded systems are subtle** - erase timing wasn't obvious until deep debugging
3. **Browser behavior matters** - cache invalidation is critical for testing
4. **Understand your binary formats** - ESP32 image structure has nuances
5. **Pre-erase is safer than on-demand erase** - synchronous operations beat async races
6. **Separate download from flash** - staging area gives flexibility and safety
7. **Good diagnostics save time** - hardware self-test caught issues early
8. **Clean code wins** - removing debug clutter made production code readable

---

## Team Knowledge Preservation

### Skills Required for Maintenance
- ESP32 partition table structure
- SPI flash memory timing characteristics  
- HTTP multipart form parsing
- ESP-IDF OTA API usage
- Binary image format validation

### Critical Files to Understand
- `src/external_flash_ota.cpp` - OTA state machine
- `src/w25q64.cpp` - SPI flash driver
- `src/ota_http_server.cpp` - Web interface
- `include/external_flash_ota.h` - Public API
- `partitions_custom.csv` - Partition layout

### Debugging Tips
- Monitor W25Q64 power rail with oscilloscope during writes
- Check JEDEC ID first if anything fails (0x000000 = dead chip)
- Look for 0xFF patterns in hex dumps to spot erase issues
- Compare firmware.bin checksums before/after upload
- Use `ESP.getFreeHeap()` to detect memory leaks

---

**Project Status:** ✅ **COMPLETE AND WORKING**  
**Date Completed:** February 5, 2026  
**Total Development Time:** ~6 hours (including debugging and chip replacement)  
**Lines of Code:** ~800 (OTA system) + ~200 (W25Q64 driver)
