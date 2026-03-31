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

## Notes

- This document is the canonical redesign parking lot for future board revisions.
- Add new items here as they are found during bench testing, firmware bring-up, or schematic review.
