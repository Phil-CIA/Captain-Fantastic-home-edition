# Switch Latency Tuning: Reduce Control Confirm Polls 5 -> 2 (Phase 1)

## Summary
Switch response feels sluggish during rapid back-to-back events (especially spinner-like behavior). The current working hypothesis is that control-side confirmation depth is adding too much delay.

## Current Behavior
- Control confirm polls are 5 in Captain-v2/src/control_main.cpp.
- Control poll cadence is 30 ms in Captain-v2/src/control_main.cpp.
- Matrix debounce is 4 ticks in Captain-v2-matrix/src/matrix_app_main.cpp.

## Proposed Change (Phase 1 Only)
- Change control confirm polls from 5 to 2.
- Do not lower control confirm below 2 in this phase.
- Keep matrix debounce unchanged for this first pass.

## Rationale
- Targets the largest control-side latency contributor first.
- Avoids matrix-side noise risk while validating responsiveness.
- Keeps false-trigger risk bounded by not going below 2 control polls.

## Out Of Scope (Phase 1)
- No matrix debounce changes yet.
- No unrelated refactors.
- No protocol redesign.

## Optional Phase 2 (Only If Needed)
- Consider matrix debounce reduction by one tick only (4 -> 3).
- Do not reduce matrix debounce to 2 due to false-trigger risk.

## Validation Plan
1. Build and upload with guarded workflow.
2. Run spinner/back-to-back switch stress checks.
3. Run switch-health monitor for 120 seconds.
4. Compare before/after: feel, missed hits, false triggers, monitor counters.

## Safety And Process Constraints
- Keep COM12 blocked.
- Use guarded flash for uploads.
- Run monitor on each validation pass.

## Suggested Commands
- powershell -ExecutionPolicy Bypass -File scripts/guarded-flash.ps1 -Target control -Action upload
- powershell -ExecutionPolicy Bypass -File scripts/Monitor-SwitchHealth.ps1 -Port COM5 -DurationSeconds 120

## Acceptance Criteria
- Noticeably improved rapid-hit responsiveness.
- No meaningful increase in false triggers.
- No regression in link/fault counters during monitor run.
