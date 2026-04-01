# Captain v2 Matrix Board — Flashing Notes

Target hardware: **ESP32-C6 DevKitC-1**  
PlatformIO project: `Captain-v2-matrix/`  
Environment: `captain_matrix_idf`

---

## Before You Flash

### 1. Get the board appearing as a COM port

If the board does not show up as a COM port in Device Manager or PlatformIO,
follow the driver setup steps in:

```
docs/ESP32C6_USB_DRIVER_SETUP.md
```

Short version:

- **CP2102N chip (Espressif official boards):** install Silicon Labs CP210x driver.
- **CH340 chip (Amazon/AliExpress clones):** install WCH CH340 driver.
- Use a **data-capable USB-C cable** (not a charge-only cable).

### 2. Identify the correct COM port

Open **Device Manager → Ports (COM & LPT)** and note the COM number.  Unplug
and re-plug the cable if you are unsure which entry belongs to the board.

---

## Build and Upload

### Using PlatformIO IDE (VS Code)

1. Open the `Captain-v2-matrix/` folder as the active workspace.
2. Select the `captain_matrix_idf` environment in the PlatformIO toolbar.
3. Click **Upload** (right-arrow icon) or use **PlatformIO: Upload** from the
   command palette.

### Using PlatformIO CLI

```bash
cd Captain-v2-matrix
pio run -e captain_matrix_idf -t upload
```

If PlatformIO cannot auto-detect the port, add `--upload-port` explicitly:

```bash
pio run -e captain_matrix_idf -t upload --upload-port COM5
# Linux/macOS:
pio run -e captain_matrix_idf -t upload --upload-port /dev/ttyUSB0
```

---

## Putting the Board into Download Mode

If the upload fails with a connection error, force the board into download mode:

1. Hold the **BOOT** button on the DevKitC-1.
2. While holding BOOT, press and release the **EN** (reset) button.
3. Release BOOT.
4. Retry the upload — you should see `Connecting...` succeed.

The BOOT button connects to GPIO9 (a strapping pin).  Holding it LOW during
reset tells the chip to enter the ROM download bootloader.

---

## Verify the Firmware is Alive

Open the serial monitor immediately after upload (the board auto-resets):

```bash
pio device monitor -e captain_matrix_idf
# or directly:
pio device monitor -b 115200
```

Expected boot output:

```
I (xxx) captain_matrix: Matrix firmware starting
I (xxx) captain_matrix: I2C slave initialised at 0x24
I (xxx) captain_matrix: Matrix pins configured
I (xxx) captain_matrix: Ready
```

Press **EN** (reset) on the board if the monitor is already open and you missed
the boot messages.

---

## Strapping Pins — Do Not Drive Externally During Reset

The ESP32-C6 has two strapping pins that are sampled at reset:

| Pin    | Function | Safe level | Risk if held LOW externally |
|--------|----------|------------|-----------------------------|
| GPIO9  | Boot mode (HIGH = SPI-Flash / LOW = Download) | Must be HIGH | Board enters download mode on every reset; firmware never runs |
| GPIO8  | JTAG source (HIGH = internal USB-JTAG) | Normally HIGH | USB-JTAG interface may not work; debug interference |

The I2C lines on the matrix board are GPIO16 (SDA) and GPIO17 (SCL).  Neither
is a strapping pin, so the I2C bus being driven by the control board during
power-up is safe.

Row driver and shift-register GPIOs are also not strapping pins.

---

## I2C Address Verification

After flashing, confirm the device is visible on the I2C bus.

### Using a standalone I2C scanner sketch on the control board

Flash an I2C scanner to the control board (or a spare ESP32) and run it with
SDA on GPIO16 and SCL on GPIO17.  The matrix board should respond at
**address `0x24`**.

### Expected scanner output

```
Scanning I2C bus...
I2C device found at address 0x24
Scan complete.
```

If nothing is found:
1. Verify the matrix board has powered up and shows the boot log above.
2. Check SDA/SCL wiring between the two boards.
3. Confirm 4.7 kΩ pull-ups are present on both SDA and SCL.
4. Check that the I2C bus speed is 100 kHz (standard mode).

---

## Serial Test Commands

If the firmware is built with `CAPTAIN_MATRIX_TEST_SUPPORT=1`, the following
commands are available on the serial port at 115200 baud:

| Command | Action |
|---------|--------|
| `HELP` | List all commands |
| `STATUS` | Show system/output enable state and pulse-width level |
| `SYSTEM ON` / `SYSTEM OFF` | Enable / disable the system |
| `OUTPUT ON` / `OUTPUT OFF` | Enable / disable lamp output |
| `PULSE <0-15>` | Set global pulse-width level |
| `ALL ON` / `ALL OFF` | Turn all lamps on / off |
| `CLEAR` | Zero all lamp RAM |
| `ROW <0-7> <0-31>` | Write one lamp row directly |
| `LAMP <row> <col> ON\|OFF` | Toggle a single lamp |
| `WALK ON` / `WALK OFF` | Start / stop lamp walk pattern |
| `SWITCHES` | Print current switch byte snapshot |

Use these commands to verify row/column behavior before connecting the control
board.

---

## Quick Bench Verification Checklist

Reference: `Captain-v2-matrix/README.md` section 10.

- [ ] Board boots and prints startup log on serial.
- [ ] I2C scan finds device at `0x24`.
- [ ] Command `0x21` (SYSTEM ON) accepted — `STATUS` shows system enabled.
- [ ] Command `0x81` (OUTPUT ON) accepted — `STATUS` shows output enabled.
- [ ] Write lamp RAM bytes `0x00..0x07` — correct rows/columns light up.
- [ ] Physical switch closures change switch bytes `0x40..0x43`.
- [ ] `PULSE` sweep (`0xE0..0xEF`) produces visible brightness change.
- [ ] Diagnostics `0xF0..0xF3` return sensible values.
- [ ] No row/column inversions, polarity errors, or timing issues observed.

Document any mismatches in `docs/NEXT_ITERATION_RECOMMENDATIONS.md` before
editing firmware.

---

## Cross-References

- USB driver setup (COM port not appearing): `docs/ESP32C6_USB_DRIVER_SETUP.md`
- Matrix protocol specification: `Captain-v2-matrix/README.md`
- Hardware redesign parking lot: `docs/NEXT_ITERATION_RECOMMENDATIONS.md`
- Session handoff: `END_OF_DAY_HANDOFF_2026-03-31.rmd`
