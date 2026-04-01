# Next Iteration Recommendations (Parking Lot)

Purpose:
- Track board-redesign items that should be revisited in the next hardware iteration.
- Capture what was observed, why it matters, and the recommended direction.
- Keep this list updated as new issues are discovered.

How to use this file:
- Add a new entry whenever we find something we do not like, something risky, or something that should be changed.
- Keep entries short and practical.
- Link supporting docs or schematics when available.
- Mark status as `Parking Lot`, `Selected`, `In Progress`, or `Done`.

## Active Parking Lot

| ID | Topic | Observation | Recommendation | Status | References |
|----|-------|-------------|----------------|--------|------------|
| HW-001 | High-side rail voltage and gate-drive margin | Original TIP125 path tolerated large drop (`VCE` around 4 V). Current MOSFET path has low drop (`VDS` typically < 0.2 V at 5 A), and measured rail excursions near 25.5 V increase stress risk for the BSS84/BSS138 pre-driver stage and turn-off reliability. | Lower the high-side rail voltage first in the next board revision, then re-validate gate-source voltages for BSS84/BSS138 and power MOSFET ON/OFF states before choosing any transistor substitutions. | Parking Lot | `Captain-v2-matrix/README.md` section 11, `README.md`, `docs/LAMP_VOLTAGE_NOTES.md` |
| HW-002 | GPIO2/GPIO4 pin sharing between row drivers and 74HC595 SR signals | GPIO2 is shared by matrix row 1 and the 74HC595 DS (DATA) pin. GPIO4 is shared by matrix row 7 and the 74HC595 STCP (LATCH) pin. Every shift-register write briefly asserts LATCH LOW (activating row 7) before latching new column data. This produces a short pre-pulse on row 7 carrying the previous frame's column data. Visible ghosting on row 7 is possible if the pre-pulse is long enough relative to the 250 us row dwell. | Verify on bench whether row 7 shows visible ghosting. If so, consider reassigning the 74HC595 DATA and LATCH pins to GPIOs not shared with row drivers in the next board revision. In firmware, mitigation is possible by blanking columns (SR write of 0) before activating any row, at a cost of an extra SR write per step. | Parking Lot | `Captain-v2-matrix/src/matrix_app_main.cpp` `writeShiftRegister16`, `refreshLampMatrixStep`; `Captain-v2/include/matrix_lamp_driver_config.h`; `Captain-v2/include/captain_mapping.h` |

## Notes

- This document is the canonical redesign parking lot for future board revisions.
- Add new items here as they are found during bench testing, firmware bring-up, or schematic review.
