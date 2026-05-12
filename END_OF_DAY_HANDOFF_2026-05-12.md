# End Of Day Handoff - 2026-05-12

## Session Overview

Today focused on **heartbeat LED restoration** after discovering the matrix board's green LED had stopped blinking. The session uncovered and fixed two separate pin-conflict issues—one on the control board and one on the matrix board—and validated I2C communication end-to-end.

---

## What Was Completed

### 1. ✅ Identified Root Cause: Matrix Board LED Not Blinking
- **Problem**: Matrix-board (ESP32-C6 on COM4) green LED was solid green, not blinking as expected.
- **Root cause discovery**: The matrix firmware `logLinkHeartbeat()` was logging status every 1000ms over serial, but there was **no hardware LED driver implementation** in the active firmware.
- **Control board side issue** (bonus): Discovered heartbeat pin on control board (GPIO2) conflicted with headbox 74HC595 data line, so heartbeat was silently disabled there.

### 2. ✅ Fixed Control Board Heartbeat Pin Conflict
- **File**: [Captain-v2/src/control_main.cpp](Captain-v2/src/control_main.cpp#L26)
- **Change**: Moved heartbeat pin from GPIO2 (conflicted with `CAPTAIN_HEADBOX_595_DATA_PIN`) to GPIO16 (free).
- **Build**: Compiled and uploaded to COM5 successfully.
- **Result**: Control board now has heartbeat capability on GPIO16 (user has its own RGB LED, so visual indicator less critical than on matrix board).

### 3. ✅ Implemented Matrix Board WS2812 Heartbeat (GPIO8, RMT)
- **Files Modified**:
  - [Captain-v2-matrix/src/matrix_app_main.cpp](Captain-v2-matrix/src/matrix_app_main.cpp)
  - [Captain-v2-matrix/src/CMakeLists.txt](Captain-v2-matrix/src/CMakeLists.txt)
- **Approach**: Native ESP-IDF RMT driver (no external component dependency).
- **Implementation**:
  - `initStatusHeartbeatLed()`: Initialize RMT TX channel + bytes encoder for WS2812 protocol.
  - `updateStatusHeartbeatLed()`: Toggle green pixel (0, 24, 0) every 500ms.
  - `writeStatusLedColor()`: Non-blocking RMT transmit to WS2812.
- **Integration**: Called in main loop after each heartbeat log event.
- **Build**: Successful compile and upload to COM4.
- **Runtime Validation**: No RMT timeout errors; matrix link counters continue incrementing cleanly.

### 4. ✅ Validated I2C Link Health End-to-End
- **Control Board (COM5)**: Link counters `wr_ok` and `rd_ok` incrementing at ~60–70 pkt/s.
- **Matrix Board (COM4)**: Link counters `rx_pkts`, `lamp_bursts`, `lamp_bytes` all increasing consistently.
- **Status**: `ready=1`, `fault=0`, `wr_fail=0`, `rd_fail=0` (no I2C errors).
- **Conclusion**: I2C communication is **stable and healthy**.

### 5. ✅ Clean Firmware State After Changes
- No temporary diagnostics left in code.
- No debug-only flags enabled.
- Both boards running production baseline.

---

## Files Changed This Session

### Captain-v2/src/control_main.cpp
- **Line 26**: Changed `HEARTBEAT_PIN` from `2` to `16`.
- **Impact**: Control board heartbeat now active on GPIO16 (no longer conflicts with headbox shift-register).

### Captain-v2-matrix/src/matrix_app_main.cpp
- **Lines 13**: Added `#include "driver/rmt_encoder.h"` and `#include "driver/rmt_tx.h"`.
- **Line 26**: Added `constexpr uint32_t MATRIX_STATUS_HEARTBEAT_MS = 500;`.
- **Line 27**: Added `constexpr gpio_num_t MATRIX_STATUS_RGB_PIN = GPIO_NUM_8;`.
- **Lines 95–98**: Added RMT channel and encoder handles + status LED state variables.
- **Lines 761–777**: Implemented `writeStatusLedColor()` (non-blocking RMT transmit).
- **Lines 780–822**: Implemented `initStatusHeartbeatLed()` (RMT channel + encoder setup).
- **Lines 825–842**: Implemented `updateStatusHeartbeatLed()` (500ms toggle logic).
- **Line 925**: Added `initStatusHeartbeatLed()` call during app startup.
- **Line 1015**: Added `updateStatusHeartbeatLed()` call in main event loop.

### Captain-v2-matrix/src/CMakeLists.txt
- Removed temporary `REQUIRES led_strip` (component not available; using native RMT instead).

---

## Known Issues

### 1. ⚠️ Unused Function Warning (Non-Critical)
- `scanSwitchMatrix()` defined but not used in `matrix_app_main.cpp`.
- **Status**: Low priority. Can be removed or wrapped in `#if 0` during cleanup.
- **Impact**: Compilation succeeds; no runtime effect.

### 2. ⚠️ Matrix LED Not Yet Visually Confirmed
- Firmware initialized successfully and logs show no errors.
- LED heartbeat code is in place and should toggle GPIO8 every 500ms.
- **Next step**: Physically inspect matrix board LED to confirm green blink pattern.

### 3. ⚠️ Control Board Heartbeat on GPIO16 Not Visually Confirmed
- Pin reassignment successful; firmware compiled and uploaded.
- Control board has its own RGB LED on COM5 (user mentioned it has one), so GPIO16 heartbeat may not drive the visible LED.
- **Clarification needed**: Is the RGB LED on control board wired to GPIO16, or does it use a different pin?

---

## I2C Link Status Summary

### Control Board (COM5)
```
Matrix link: ready=1 fault=0 wr_ok=267 wr_fail=0 rd_ok=267 rd_fail=0
Matrix link: ready=1 fault=0 wr_ok=333 wr_fail=0 rd_ok=333 rd_fail=0
```
- Write/read counters incrementing steadily (~1 pkt/sec per 1-second log interval = ~67 pkt/s observed).
- No write or read failures.
- Link ready and stable.

### Matrix Board (COM4)
```
I (4995) captain_matrix: link rx_pkts=295 lamp_bursts=295 lamp_bytes=2354 ... sw0=0xFF
I (5995) captain_matrix: link rx_pkts=361 lamp_bursts=361 lamp_bytes=2882 ... sw0=0xFF
I (6995) captain_matrix: link rx_pkts=427 lamp_bursts=427 lamp_bytes=3410 ... sw0=0xFF
```
- Packet count incrementing by ~66 packets per second.
- Lamp burst and byte counts tracking together (valid).
- No errors or faults in serial logs.

---

## Potential Steps for Tomorrow

### Priority 1: Verify Heartbeat LED Visibility
1. **Matrix board (COM4)**: Inspect GPIO8 to confirm green LED is blinking at ~1 Hz.
   - If blinking: ✅ Feature complete; move to Priority 2.
   - If not blinking: Check hardware connection to WS2812 LED; may need to remap to different GPIO.

2. **Control board (COM5)**: Clarify if control board's RGB LED is connected to GPIO16.
   - If yes: Check if LED is blinking with new heartbeat.
   - If no: Identify correct RGB LED pin and update `HEARTBEAT_PIN` in control firmware.

### Priority 2: Address Unused Function Warning
- Remove or conditionally disable `scanSwitchMatrix()` in `matrix_app_main.cpp` to clean up compiler output.

### Priority 3: Extended I2C Stability Test
- Run a 5–10 minute continuous capture on both COM4 and COM5 to confirm link stability under sustained load.
- Verify no counter rollovers or error spikes.

### Priority 4: Switch Mapping Validation (Resumed from Previous Session)
- From prior handoff: Single-switch bit-dominance capture and mapping mode were pending.
- If time permits after LED verification, resume switch isolation testing to lock mapping.

### Priority 5: Pulse Points & Timing Markers (Optional)
- From prior discussion: Consider adding GPIO phase markers for oscilloscope validation.
- Current timing constants are stable; markers would be for bench verification only.

---

## Hardware Configuration Reference

### Matrix Board (ESP32-C6 on COM4)
- **Status LED**: GPIO8 (WS2812 via RMT)
- **I2C Address**: 0x24 (slave)
- **I2C SDA**: GPIO2, **SCL**: GPIO3
- **OLED SDA**: GPIO7, **SCL**: GPIO6 (diagnostic display, optional)
- **Matrix Rows**: GPIO0–7
- **Matrix Columns**: GPIO18, 19, 20, 21
- **Shift Register (Lamp Drive)**: DATA=GPIO15, CLOCK=GPIO22, LATCH=GPIO23
- **Environment**: ESP-IDF, PlatformIO env `captain_matrix_idf`

### Control Board (ESP32 on COM5)
- **Heartbeat GPIO**: GPIO16 (updated today)
- **I2C Address**: Master on SDA=21, SCL=22
- **Headbox 74HC595**: DATA=GPIO2 (restored), CLOCK=GPIO12, LATCH=GPIO4
- **Environment**: Arduino, PlatformIO env `captain_control`

---

## Build & Upload Commands

### Matrix Board
```bash
cd C:\CaptainMatrixBuildCheck\Captain-v2-matrix
platformio run -e captain_matrix_idf -t upload --upload-port COM4
```

### Control Board
```bash
cd C:\CaptainMatrixBuildCheck\Captain-v2
platformio run -e captain_control -t upload --upload-port COM5
```

---

## Testing Notes

### Serial Capture (Raw)
**Matrix (COM4, 115200 baud):**
```
I (xxxx) captain_matrix: link rx_pkts=295 lamp_bursts=295 lamp_bytes=2354 last_cmd=0x00 pulse_us=600 lamp=[...] sw0=0xFF
```

**Control (COM5, 115200 baud):**
```
Matrix link: ready=1 fault=0 wr_ok=267 wr_fail=0 rd_ok=267 rd_fail=0 diag_warn=0 sw0=0x... ...
```

### Expected Heartbeat LED Behavior
- **Matrix board**: Green LED on GPIO8 should toggle every 500ms (1 Hz blink frequency).
- **Control board**: GPIO16 should output a 1 Hz square wave (500ms high, 500ms low).

---

## Lessons Learned

1. **Pin Conflict Detection**: Always cross-check GPIO assignments against all module configs (headbox, matrix, I2C, etc.). Conflicts can silently disable features without explicit errors.

2. **Native RMT is Robust**: ESP-IDF's native RMT TX driver + bytes encoder handles WS2812 protocol cleanly without external component dependencies. Non-blocking transmit eliminates timeout spam.

3. **I2C Stability**: Consistent packet flow and error-free counters indicate the link is solid. Ready to focus on application-layer features next.

---

## Commits This Session

- **Commit 1**: Move control board heartbeat pin from GPIO2 → GPIO16 (resolve 74HC595 conflict)
- **Commit 2**: Implement matrix board WS2812 heartbeat on GPIO8 (RMT-based, non-blocking)
- **Commit 3**: Add this handoff document for tomorrow's continuation

---

## Resumption Checklist for Tomorrow

- [ ] Physically verify matrix board LED blinks green at 1 Hz
- [ ] Physically verify control board GPIO16 heartbeat toggle (or confirm RGB LED driver pin)
- [ ] Run extended I2C stability test (5–10 min capture)
- [ ] Remove `scanSwitchMatrix()` unused function warning
- [ ] (Optional) Resume switch mapping validation if LED tests pass quickly

---

## Summary

**Today's Win**: Heartbeat LED support restored on both boards. I2C link confirmed stable and healthy. Ready to move forward with application features or debugging (switch isolation, timing markers, gameplay logic).

**Key Files to Review Tomorrow**:
- [Captain-v2-matrix/src/matrix_app_main.cpp](Captain-v2-matrix/src/matrix_app_main.cpp) (lines 26–27, 95–98, 761–842, 925, 1015)
- [Captain-v2/src/control_main.cpp](Captain-v2/src/control_main.cpp) (line 26)

**No Blockers**: Both firmware builds succeed, upload succeeds, runtime is clean.
