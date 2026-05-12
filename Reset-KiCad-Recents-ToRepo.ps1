$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSCommandPath
$repoKiCadRoot = Join-Path $repoRoot "hardware\kicad"

if (-not (Test-Path $repoKiCadRoot)) {
    throw "Repo KiCad root not found: $repoKiCadRoot"
}

$kicadConfigRoot = Join-Path $env:APPDATA "KiCad\9.0"
$kicadJsonPath = Join-Path $kicadConfigRoot "kicad.json"
$kicadCommonPath = Join-Path $kicadConfigRoot "kicad_common.json"

function Backup-File([string]$path) {
    if (-not (Test-Path $path)) {
        return $null
    }

    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $backupPath = "$path.bak-$timestamp"
    Copy-Item -Path $path -Destination $backupPath -Force
    return $backupPath
}

function Add-ObjectPropertyIfMissing([object]$obj, [string]$name) {
    if (-not ($obj.PSObject.Properties.Name -contains $name)) {
        $obj | Add-Member -NotePropertyName $name -NotePropertyValue ([pscustomobject]@{})
    }
}

function Set-ObjectPropertyValue([object]$obj, [string]$name, $value) {
    if ($obj.PSObject.Properties.Name -contains $name) {
        $obj.$name = $value
    } else {
        $obj | Add-Member -NotePropertyName $name -NotePropertyValue $value
    }
}

if (-not (Test-Path $kicadConfigRoot)) {
    throw "KiCad config folder not found: $kicadConfigRoot"
}

$kicadBackup = Backup-File -path $kicadJsonPath
$commonBackup = Backup-File -path $kicadCommonPath

if (Test-Path $kicadJsonPath) {
    $kicadJson = Get-Content -Path $kicadJsonPath -Raw | ConvertFrom-Json
    Add-ObjectPropertyIfMissing -obj $kicadJson -name "system"

    Set-ObjectPropertyValue -obj $kicadJson.system -name "file_history" -value @()
    Set-ObjectPropertyValue -obj $kicadJson.system -name "open_projects" -value @()
    Set-ObjectPropertyValue -obj $kicadJson.system -name "working_dir" -value $repoKiCadRoot

    $kicadJson | ConvertTo-Json -Depth 100 | Set-Content -Path $kicadJsonPath -Encoding UTF8
}

if (Test-Path $kicadCommonPath) {
    $kicadCommon = Get-Content -Path $kicadCommonPath -Raw | ConvertFrom-Json
    Add-ObjectPropertyIfMissing -obj $kicadCommon -name "environment"
    Add-ObjectPropertyIfMissing -obj $kicadCommon.environment -name "vars"

    Set-ObjectPropertyValue -obj $kicadCommon.environment.vars -name "KICAD_PROJECT_DIR" -value $repoKiCadRoot

    $kicadCommon | ConvertTo-Json -Depth 100 | Set-Content -Path $kicadCommonPath -Encoding UTF8
}

Write-Host "KiCad repo-default reset complete."
Write-Host "Repo KiCad root:" $repoKiCadRoot
if ($kicadBackup) { Write-Host "Backup created:" $kicadBackup }
if ($commonBackup) { Write-Host "Backup created:" $commonBackup }
Write-Host "Next: restart KiCad and open projects via Open-KiCad.ps1"
