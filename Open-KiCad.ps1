param(
    [ValidateSet("manager", "matrix", "control", "hat", "7seg")]
    [string]$Board = "manager"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSCommandPath
$kicadRoot = Join-Path $repoRoot "hardware\kicad"

$projectMap = @{
    matrix = Join-Path $kicadRoot "captain_matrix\Pinball matrix board.kicad_pro"
    control = Join-Path $kicadRoot "captain_control\Pinball Control board.kicad_pro"
    hat = Join-Path $kicadRoot "legacy\Hat board.kicad_pro"
    "7seg" = Join-Path $kicadRoot "7seg_display\7 Seg  project.kicad_pro"
}

function Find-KiCadExe {
    $candidates = @(
        "C:\Program Files\KiCad\9.0\bin\kicad.exe",
        "C:\Program Files\KiCad\8.0\bin\kicad.exe",
        "C:\Program Files\KiCad\7.0\bin\kicad.exe"
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    $cmd = Get-Command kicad.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    throw "KiCad executable not found. Install KiCad or add kicad.exe to PATH."
}

$kicadExe = Find-KiCadExe

if ($Board -eq "manager") {
    $target = $kicadRoot
} else {
    $target = $projectMap[$Board]
    if (-not (Test-Path $target)) {
        throw "Target project not found: $target"
    }
}

Write-Host "Launching KiCad target:" $target
Start-Process -FilePath $kicadExe -ArgumentList @($target)
