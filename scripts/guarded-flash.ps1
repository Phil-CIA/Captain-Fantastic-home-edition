param(
  [Parameter(Mandatory = $true)]
  [ValidateSet("matrix", "control")]
  [string]$Target,

  [Parameter(Mandatory = $true)]
  [ValidateSet("build", "upload")]
  [string]$Action,

  [switch]$ForceMacMismatch
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$script:RepoRoot = Split-Path $PSScriptRoot -Parent
$script:PioExe = "C:/Users/user/.platformio/penv/Scripts/platformio.exe"
$script:EsptoolExe = "C:/Users/user/.platformio/penv/Scripts/esptool.exe"
$script:IdentityConfigPath = Join-Path $PSScriptRoot "flash-targets.json"
$script:HardBlockedPorts = @("COM12")

if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
  $PSNativeCommandUseErrorActionPreference = $false
}

function Write-PrecheckFailure {
  param([string]$Message, [int]$Code = 2)

  Write-Host ""
  Write-Host "FLASH PRECHECK FAILED" -ForegroundColor Red
  Write-Host $Message -ForegroundColor Yellow
  Write-Host "No flash was performed." -ForegroundColor Yellow
  exit $Code
}

function Get-NormalizedMac {
  param([string]$Mac)

  if ([string]::IsNullOrWhiteSpace($Mac)) {
    return ""
  }

  return ($Mac.Trim().ToUpperInvariant())
}

function Get-TargetConfig {
  param([string]$Name)

  switch ($Name) {
    "matrix" {
      return [pscustomobject]@{
        DisplayName  = "Captain Matrix"
        WorkingDir   = "Captain-v2-matrix"
        EnvName      = "captain_matrix_idf"
        ExpectedPort = "COM4"
      }
    }
    "control" {
      return [pscustomobject]@{
        DisplayName  = "Captain Control"
        WorkingDir   = "Captain-v2"
        EnvName      = "captain_control"
        ExpectedPort = "COM5"
      }
    }
    default {
      throw "Unknown target: $Name"
    }
  }
}

function Get-IdentityConfig {
  if (!(Test-Path $script:IdentityConfigPath)) {
    Write-PrecheckFailure "Identity config not found: $($script:IdentityConfigPath)"
  }

  return Get-Content $script:IdentityConfigPath -Raw | ConvertFrom-Json
}

function Get-ExpectedMac {
  param([string]$TargetName)

  $config = Get-IdentityConfig
  $entryProp = $config.PSObject.Properties[$TargetName]
  if ($null -eq $entryProp -or $null -eq $entryProp.Value) {
    return ""
  }

  return Get-NormalizedMac ([string]$entryProp.Value.expectedMac)
}

function Get-DetectedPorts {
  $ports = New-Object System.Collections.ArrayList
  $pnpPorts = @(Get-PnpDevice -Class Ports -ErrorAction SilentlyContinue)

  foreach ($p in $pnpPorts) {
    if ($null -eq $p.FriendlyName) { continue }
    $m = [regex]::Match([string]$p.FriendlyName, 'COM\d+')
    if ($m.Success) {
      [void]$ports.Add([string]$m.Value.ToUpperInvariant())
    }
  }

  return @($ports | Sort-Object -Unique)
}

function Invoke-Esptool {
  param(
    [string]$Port,
    [string]$CommandName
  )

  $stdoutFile = [System.IO.Path]::GetTempFileName()
  $stderrFile = [System.IO.Path]::GetTempFileName()

  try {
    $proc = Start-Process -FilePath $script:EsptoolExe `
      -ArgumentList @("--port", $Port, $CommandName) `
      -NoNewWindow -PassThru -Wait `
      -RedirectStandardOutput $stdoutFile `
      -RedirectStandardError $stderrFile

    $stdout = if (Test-Path $stdoutFile) { Get-Content $stdoutFile -Raw } else { "" }
    $stderr = if (Test-Path $stderrFile) { Get-Content $stderrFile -Raw } else { "" }

    return [pscustomobject]@{
      ExitCode = $proc.ExitCode
      Text     = ($stdout + "`n" + $stderr)
    }
  }
  finally {
    Remove-Item $stdoutFile -ErrorAction SilentlyContinue
    Remove-Item $stderrFile -ErrorAction SilentlyContinue
  }
}

function Get-PortMac {
  param([string]$Port)

  if (!(Test-Path $script:EsptoolExe)) {
    Write-PrecheckFailure "esptool executable not found at $($script:EsptoolExe)"
  }

  $probe = Invoke-Esptool -Port $Port -CommandName "read_mac"
  if ($probe.ExitCode -ne 0) {
    Write-PrecheckFailure "Could not read immutable MAC from $Port. Output:`n$($probe.Text)"
  }

  $macMatch = [regex]::Match($probe.Text, 'MAC:\s+([0-9A-Fa-f:]{17,23})')
  if (!$macMatch.Success) {
    Write-PrecheckFailure "MAC parse failed for $Port. Output:`n$($probe.Text)"
  }

  return Get-NormalizedMac $macMatch.Groups[1].Value
}

function Test-HardBlockedPort {
  param([string]$Port, [string]$Context)

  if ($script:HardBlockedPorts -contains $Port.ToUpperInvariant()) {
    Write-PrecheckFailure "$Context resolved to hard-blocked port $Port (CrowPanel deny-list)."
  }
}

function Get-FreeSubstDrive {
  $reserved = @{}
  foreach ($drive in (Get-PSDrive -PSProvider FileSystem)) {
    $reserved[$drive.Name.ToUpperInvariant()] = $true
  }

  foreach ($candidate in @("P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z")) {
    if (-not $reserved.ContainsKey($candidate)) {
      return "${candidate}:"
    }
  }

  return ""
}

$cfg = Get-TargetConfig -Name $Target
$expectedMac = Get-ExpectedMac -TargetName $Target

if (!(Test-Path $script:PioExe)) {
  Write-PrecheckFailure "PlatformIO executable not found at $($script:PioExe)"
}

Test-HardBlockedPort -Port $cfg.ExpectedPort -Context "Selected target '$Target'"

if ($Action -eq "upload") {
  $ports = @(Get-DetectedPorts)
  if ($ports -notcontains $cfg.ExpectedPort) {
    $seen = if ($ports.Count -gt 0) { $ports -join ", " } else { "none" }
    Write-PrecheckFailure "Expected $($cfg.DisplayName) on $($cfg.ExpectedPort), detected ports: $seen"
  }

  foreach ($blocked in $script:HardBlockedPorts) {
    if ($ports -contains $blocked) {
      Write-Host "Info: hard-blocked port detected and ignored: $blocked" -ForegroundColor DarkYellow
    }
  }

  if ([string]::IsNullOrWhiteSpace($expectedMac)) {
    Write-PrecheckFailure "Expected MAC for target '$Target' is missing in $($script:IdentityConfigPath)."
  }

  $detectedMac = Get-PortMac -Port $cfg.ExpectedPort
  Write-Host "Precheck: detected MAC $detectedMac on $($cfg.ExpectedPort)" -ForegroundColor Green
  Write-Host "Precheck: expected MAC $expectedMac for target '$Target'" -ForegroundColor Green

  if ($detectedMac -ne $expectedMac) {
    if (-not $ForceMacMismatch) {
      Write-PrecheckFailure "MAC mismatch on $($cfg.ExpectedPort). Expected $expectedMac but detected $detectedMac. Re-run with -ForceMacMismatch to accept responsibility and continue."
    }

    Write-Host "WARNING: MAC mismatch override accepted by operator for target '$Target'." -ForegroundColor Yellow
    Write-Host "WARNING: Expected $expectedMac, detected $detectedMac on $($cfg.ExpectedPort)." -ForegroundColor Yellow
  }
}

$projectDir = Join-Path $script:RepoRoot $cfg.WorkingDir
$substDrive = ""

try {
  if ($projectDir -match "\s") {
    $substDrive = Get-FreeSubstDrive
    if ([string]::IsNullOrWhiteSpace($substDrive)) {
      Write-PrecheckFailure "Could not allocate a temporary drive letter for whitespace-safe PlatformIO path handling."
    }

    & subst $substDrive "$script:RepoRoot"
    if ($LASTEXITCODE -ne 0) {
      Write-PrecheckFailure "Failed to create temporary subst mapping for '$script:RepoRoot'."
    }

    $projectDir = Join-Path ($substDrive + "\") $cfg.WorkingDir
  }

  $pioCommand = New-Object System.Collections.ArrayList
  [void]$pioCommand.Add("run")
  [void]$pioCommand.Add("-d")
  [void]$pioCommand.Add($projectDir)
  [void]$pioCommand.Add("-e")
  [void]$pioCommand.Add($cfg.EnvName)

  if ($Action -eq "upload") {
    [void]$pioCommand.Add("-t")
    [void]$pioCommand.Add("upload")
    [void]$pioCommand.Add("--upload-port")
    [void]$pioCommand.Add($cfg.ExpectedPort)
  }

  Write-Host "Running: platformio $($pioCommand -join ' ')" -ForegroundColor Cyan
  & $script:PioExe $pioCommand.ToArray()
  exit $LASTEXITCODE
}
finally {
  if (-not [string]::IsNullOrWhiteSpace($substDrive)) {
    & subst $substDrive /d | Out-Null
  }
}
