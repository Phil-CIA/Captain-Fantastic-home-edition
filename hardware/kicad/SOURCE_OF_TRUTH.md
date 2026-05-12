# KiCad Source Of Truth

This file is the canonical map for which KiCad project files are current in this repo.

## ⚡ Quick Start: How to Open Projects

**→ See [HOW-TO-OPEN-PROJECTS.md](HOW-TO-OPEN-PROJECTS.md) for step-by-step instructions**

(TL;DR: Use `Open-KiCad.bat` launcher or manually navigate to `hardware/kicad/` in KiCad's File → Open Project)

## Machine-Readable Data

Machine-readable companion file for agents/tools:
- `hardware/kicad/CURRENT_PROJECTS.yaml`

## Current Board Projects

1. Matrix board (active)
- Project: `hardware/kicad/captain_matrix/Pinball matrix board.kicad_pro`
- Fab anchor: Matrix board production set dated 2026-04-09.

2. Control board (active)
- Project: `hardware/kicad/captain_control/Pinball Control board.kicad_pro`
- Fab anchor: Control board production set dated 2026-04-11.

3. Hat board (historical reference)
- Project: `hardware/kicad/legacy/Hat board.kicad_pro`
- Fab anchor: Hat board production set dated 2026-03-16.

## Sequestered Historical Areas

Do not use these as active source locations:

- `archive/`
- `hardware/kicad/legacy/` (except when intentionally referencing old Hat design)
- `hardware/kicad/esp32vroom/`
- `hardware/gerbers/` (fabrication outputs only)

## Opening Policy

1. Launch KiCad from repo root with `Open-KiCad.ps1`.
2. If KiCad keeps opening old local paths, run `Reset-KiCad-Recents-ToRepo.ps1` once.
3. For current development edits, open only the project files listed in "Current Board Projects" above.

## Archive Policy

When superseding a board revision:

1. Keep the newest active revision under `hardware/kicad/<board_name>/`.
2. Move superseded revisions into an archive-labeled folder.
3. Do not reuse archive folders for active design work.
4. Update this file so future sessions can identify active files immediately.
