# Development Workflow & Documentation Discipline

**Established**: May 12, 2026  
**Applies to**: `Captain-Fantastic-home-edition` matrix firmware and control firmware

---

## Overview

This document establishes a sustainable workflow for matrix firmware development, focusing on:
1. **Baseline traceability**: Know exactly what was working and when
2. **Daily backups**: Prevent loss of known-good states
3. **Resource tracking**: Audit timing constants and GPIO usage across handoffs
4. **Acceptance criteria**: Define "working" before starting new changes

---

## Part 1: Baseline Management

### Restore & Validate Workflow

When starting a new session:

1. **Review the latest handoff** document (e.g., `END_OF_DAY_HANDOFF_2026-05-12.rmd`)
2. **Identify the last-known-good firmware file**:
   - Will be named `matrix_app_main.cpp.bak-YYYY-MM-DD` (archived version)
   - Or reference in handoff: "restored from May 5 baseline"
3. **Restore to active build**:
   ```bash
   copy "Captain-v2-matrix\src\matrix_app_main.cpp.bak-2026-05-05" \
        "Captain-v2-matrix\src\matrix_app_main.cpp"
   ```
4. **Build and upload** to COM4 (matrix):
   ```bash
   cd Captain-v2-matrix
   platformio run -e captain_matrix_idf --target upload --upload-port COM4
   ```
5. **Run acceptance test** (30-second serial capture on COM5):
   - Listen for `Matrix link:` output
   - Verify `sw0, sw1, sw2, sw3` bytes toggle (not stuck at 0x00 or 0xFF)
   - Confirm `ready=1, fault=0`
   - Check `wr_ok, rd_ok` counters incrementing (~60–80 pkt/sec)
6. **Document results** in session handoff:
   ```markdown
   ## Baseline Validation (Date)
   - Restored from: matrix_app_main.cpp.bak-2026-05-05
   - Upload: ✅ SUCCESS to COM4
   - Acceptance test: ✅ PASS (sw0..sw3 toggling, link stable)
   - Ready to proceed with [next phase]
   ```

### When Baseline Fails Acceptance Test

If the baseline fails:
1. Check handoff notes for known issues
2. Verify hardware connections (I2C, shift register pins, power)
3. If hardware is good, escalate to prior session's logs for debugging context
4. **Do not proceed with new changes** until baseline passes

---

## Part 2: Daily Backup Discipline

### Create a Backup

Whenever you make intentional changes to matrix firmware:

1. **Build and test** your changes locally
2. **Document your test results** (what you tested, what passed/failed)
3. **Create a dated backup** of your version:
   ```bash
   copy "Captain-v2-matrix\src\matrix_app_main.cpp" \
        "Captain-v2-matrix\src\matrix_app_main.cpp.bak-2026-05-12"
   ```
4. **Git commit** with a descriptive message:
   ```bash
   git add Captain-v2-matrix/src/matrix_app_main.cpp
   git commit -m "2026-05-12: Fix [issue]. Tests: [what passed]. Timing: ROW_SETTLE=50us, DEBOUNCE=4"
   ```
5. **Update RESOURCE_TABLE.csv** with your new row:
   ```csv
   2026-05-12,matrix_app_main.cpp,captain_matrix_idf,GPIO18/19/20/21,...,PASS,"Description of changes"
   ```

### Backup Naming Convention

```
matrix_app_main.cpp.bak-YYYY-MM-DD
```

- One backup **per date** (maximum one per calendar day)
- If multiple versions on the same day, use the latest build
- Older backups kept in repo for archaeology, but not used for restore

### Archive Old Backups

When backups accumulate (> 20 old files), move them to a date-stamped archive:
```
archive/backups/2026-04/ (April backups)
archive/backups/2026-05/ (May backups, etc.)
```

---

## Part 3: Resource Tracking

### RESOURCE_TABLE.csv

Located at: `Captain-Fantastic-home-edition/docs/RESOURCE_TABLE.csv`

**Columns**:
- `Handoff Date`: YYYY-MM-DD
- `Firmware File`: filename (or git commit hash for reference)
- `Build Env`: platformio environment used
- `GPIO Rows/Cols`: Column assignment for inputs
- `SR Clock/Data/Latch`: Shift register pins
- `I2C Addr`: Slave address (hex)
- `ROW_SETTLE_US`: Timing constant
- `SWITCH_DEBOUNCE_TICKS`: Debounce threshold
- `LAMP_PULSE_MIN_US`: Baseline lamp on-time
- `LAMP_PULSE_STEP_US`: Brightness step increment
- `Test Status`: PASS, FAIL, or PARTIAL
- `Notes`: What worked, what didn't, key findings

### When to Update

Update the table **immediately after**:
- Restoring a baseline and running acceptance test
- Making changes and testing your firmware
- Moving to next handoff (final validation pass)

### Example Row

```csv
2026-05-12,matrix_app_main.cpp.bak-2026-05-12,captain_matrix_idf,GPIO18/19/20/21,GPIO18/19/20/21,GPIO22,GPIO15,GPIO23,0x24,50,4,100,100,PASS,"Baseline revalidated; sw0..sw3 toggling; link stable; ready for scope measurements"
```

---

## Part 4: Timing Contract (Acceptance Criteria)

Located at: `Captain-Fantastic-home-edition/docs/TIMING_CONTRACT_2026-05-12.md`

**Key role**: Define what "working" means in measurable terms.

**Review before starting work**:
1. Read section: "Acceptance Criteria (Known-Good State)"
2. Run acceptance test per that spec
3. If you fail any criterion, investigate before proceeding

**Update timing contract when**:
- Changing timing constants intentionally (justify in commit message + handoff)
- Scope measurements reveal different values than code (update code, then contract)
- New hardware variant requires adjusted timing

---

## Part 5: Handoff Template

At end of session, create a handoff document (e.g., `END_OF_DAY_HANDOFF_2026-05-12.rmd`):

```markdown
# End-of-Day Handoff — May 12, 2026

## Session Summary
- Objective: [what you tried to accomplish]
- Outcome: [what succeeded, what failed]
- Time spent: [duration]

## Firmware State
- **Current file**: `Captain-v2-matrix/src/matrix_app_main.cpp`
- **Last backup**: `matrix_app_main.cpp.bak-2026-05-12` ✅
- **Baseline status**: PASS (May 5 baseline re-validated)

## Acceptance Test Results
- Restored from: `.bak-2026-05-05`
- Upload: ✅ SUCCESS
- Test results:
  - sw0..sw3 toggling: ✅ YES
  - Link stable: ✅ YES (ready=1, fault=0)
  - Checksum errors: ✅ NONE
  - Packet rate: ~63 pkt/sec (expected ~60–80)

## Documentation
- ✅ Created: `docs/TIMING_CONTRACT_2026-05-12.md`
- ✅ Created: `docs/RESOURCE_TABLE.csv`
- ✅ Backup created: `.bak-2026-05-12`

## Next Session Should
1. Read `TIMING_CONTRACT_2026-05-12.md` for acceptance criteria
2. Review `RESOURCE_TABLE.csv` for resource audit
3. [Your specific next steps here]

## Known Issues
- [Any outstanding issues, blockers, or tech debt noted]

## Files Changed
- `Captain-v2-matrix/src/matrix_app_main.cpp` (restored from .bak-2026-05-05)
- `docs/TIMING_CONTRACT_2026-05-12.md` (NEW)
- `docs/RESOURCE_TABLE.csv` (NEW)
```

---

## Quick Reference

### "I'm starting a new session"
1. Read latest handoff doc
2. Restore baseline: `copy .bak-2026-05-05 matrix_app_main.cpp`
3. Build + upload to COM4
4. Run 30-sec acceptance test on COM5
5. If PASS → proceed; if FAIL → debug before continuing

### "I want to commit my changes"
1. Test your firmware (build, upload, verify)
2. Create dated backup: `copy matrix_app_main.cpp matrix_app_main.cpp.bak-2026-05-12`
3. `git add` + `git commit` with descriptive message
4. Update RESOURCE_TABLE.csv with new row
5. Document in handoff

### "I'm handing off to next session"
1. Ensure your current state is backed up (`.bak-YYYY-MM-DD`)
2. Run full acceptance test and document results
3. Create handoff doc with clear "next steps"
4. Update RESOURCE_TABLE.csv and TIMING_CONTRACT if you changed constants

---

## See Also

- [TIMING_CONTRACT_2026-05-12.md](TIMING_CONTRACT_2026-05-12.md) — Timing spec + acceptance criteria
- [RESOURCE_TABLE.csv](RESOURCE_TABLE.csv) — Timeline of firmware versions and constants
- [GPIO_PINOUT.md](GPIO_PINOUT.md) — Hardware pin assignments (for reference)
- [END_OF_DAY_HANDOFF_2026-05-12.rmd](../END_OF_DAY_HANDOFF_2026-05-12.rmd) — Latest session handoff
