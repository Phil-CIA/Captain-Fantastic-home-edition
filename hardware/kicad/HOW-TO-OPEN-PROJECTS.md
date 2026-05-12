# How to Open KiCad Projects from This Repo

After the KiCad recents reset, here are your three options for opening projects:

---

## **OPTION 1: Use the Launcher (Recommended - Fastest)**

This ensures you always open repo files, never stale local copies.

### Windows Users:
**Double-click** `Open-KiCad.bat` in the repo root:
```
c:\Users\user\Esp32 projects VScode\Captain-Fantastic-home-edition\Open-KiCad.bat
```

You'll see a menu:
```
Select which KiCad project to open:
1. manager  — KiCad Project Manager
2. matrix   — Pinball matrix board
3. control  — Pinball Control board
4. hat      — Hat board (legacy)
5. 7seg     — 7 Seg project
```

Type the number or name and press Enter. KiCad launches with that project.

### PowerShell Users (advanced):
```powershell
& ".\Open-KiCad.ps1" -board matrix
# or: control, hat, 7seg, manager
```

---

## **OPTION 2: Direct in KiCad UI (No Scripts Required)**

If the launcher has issues or you prefer manual navigation:

1. **Start KiCad normally** (or File → Open Project in existing KiCad window)
2. Navigate to:
   ```
   C:\Users\user\Esp32 projects VScode\Captain-Fantastic-home-edition\hardware\kicad\
   ```
3. Open one of these folders and select the `.kicad_pro` file:

   | Board | Folder | File |
   |-------|--------|------|
   | **Matrix** | `captain_matrix/` | `Pinball matrix board.kicad_pro` |
   | **Control** | `captain_control/` | `Pinball Control board.kicad_pro` |
   | **Hat (Legacy)** | `legacy/` | `Hat board.kicad_pro` |
   | **7 Seg** | `7seg_display/` | `7 Seg project.kicad_pro` |

---

## **OPTION 3: Create Desktop Shortcuts (Optional - One-Click Access)**

For faster access, create Windows shortcuts on your desktop that launch individual boards:

### Via PowerShell:
```powershell
# Run this once to create all shortcuts
$RepoRoot = "C:\Users\user\Esp32 projects VScode\Captain-Fantastic-home-edition"
$Shell = New-Object -ComObject WScript.Shell
$DesktopPath = [Environment]::GetFolderPath("Desktop")

$boards = @("matrix", "control", "hat", "7seg")
foreach ($board in $boards) {
    $Shortcut = $Shell.CreateShortcut("$DesktopPath\KiCad - $board.lnk")
    $Shortcut.TargetPath = "C:\Windows\System32\cmd.exe"
    $Shortcut.Arguments = "/c `"cd /d `"$RepoRoot`" && start Open-KiCad.bat $board`""
    $Shortcut.WorkingDirectory = $RepoRoot
    $Shortcut.Save()
    Write-Host "Created shortcut: KiCad - $board.lnk"
}
```

Then double-click `KiCad - matrix.lnk`, `KiCad - control.lnk`, etc., on your desktop.

---

## **Current Project Locations (for reference)**

All **current** projects are in this repo:
```
hardware/kicad/
├── captain_matrix/Pinball matrix board.kicad_pro       (Fab date: 2026-04-09)
├── captain_control/Pinball Control board.kicad_pro     (Fab date: 2026-04-11)
├── 7seg_display/7 Seg project.kicad_pro
└── legacy/Hat board.kicad_pro                          (Historical; superceded)
```

For machine-readable project metadata, see: **[CURRENT_PROJECTS.yaml](CURRENT_PROJECTS.yaml)**

---

## **Troubleshooting**

### "The script cannot be executed because running scripts is disabled"
If `Open-KiCad.ps1` won't run, try:
1. Right-click `Open-KiCad.bat` → **Run as Administrator**
2. Or use **Option 2** (direct File → Open in KiCad UI)

### "I keep opening the wrong board version"
- Make sure you're using **Option 1** (launcher) or **Option 2** (repo path)
- Never use File → Recent Projects (those point to old local paths)
- The KiCad recents have been reset; stale entries won't reappear unless you manually open non-repo files again

### "I'm not sure which board is current"
- **Matrix**: Fab date 2026-04-09
- **Control**: Fab date 2026-04-11
- **Hat**: Legacy (in `legacy/` folder—not current)

See [SOURCE_OF_TRUTH.md](SOURCE_OF_TRUTH.md) for full project status.
