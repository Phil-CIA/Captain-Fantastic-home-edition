# Captain-v2 C6 USB/UART hardware notes (2026-04-01)

## Schematic conclusions
- ESP32-C6 native USB signals are on GPIO12 (D-) and GPIO13 (D+).
- Matrix board routing uses GPIO12/GPIO13 for lamp column signals.
- Because these pins are reused for matrix drive, native USB-Serial/JTAG is not reliable during normal matrix firmware operation.
- External UART bridge (CP2102 on UART0 TXD/RXD) is the correct debug/programming path for this board revision.

## Practical workflow
- Use `upload_port = COM8` and `monitor_port = COM8` for `captain_matrix_c6_idf`.
- Keep BOOT (GPIO9) released for normal run mode.
- If ROM says `waiting for download`, BOOT is held low or auto-download signaling is active.

## Critical mapping correction (2026-04-01)
- Root cause found during bring-up: connector pin numbering was interpreted upside-down.
- On this connector, numbering runs with pin 16 at the bottom and pin 30 at the top (reverse of earlier assumption).
- Because of that reversal, the net believed to be GPIO9 was actually a different pin location during probing.
- Always verify both net label and physical connector orientation before concluding strap-pin behavior.

## Confirmed boot strap conflict (2026-04-01)
- True GPIO9 is tied into `Sw_Col_3` switch input network through a 2.2k pull-down path.
- This is strong enough to hold the boot strap low at reset and force ROM download mode on-carrier.
- Practical impact: GPIO9 cannot be used as a normal switch column net on this board revision.
- Bring-up safeguard in firmware now excludes GPIO9 from active matrix column reads.

## Validated workaround (2026-04-01)
- Removing the resistor path on `Sw_Col_3` that pulled GPIO9 low restores normal `SPI_FAST_FLASH_BOOT` behavior on-carrier.
- After removal, runtime logs are stable and show continuous `captain_idf` alive ticks.
- This is a workable interim fix until full remap is implemented.

## Screenshot archival
- Dev board screenshots should be added under a folder such as `Captain-v2/docs/dev-board-schematics/` when available as image files.
- This note records the electrical conclusion even before images are committed.
