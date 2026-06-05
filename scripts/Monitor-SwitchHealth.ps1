param(
    [string]$Port = "COM5",
    [int]$Baud = 115200,
    [int]$DurationSeconds = 60,
    [double]$WarnRdFailPerSec = 0.50,
    [double]$WarnWrFailPerSec = 0.50,
    [double]$WarnSuppressedLampEchoPerSec = 5.00
)

Write-Host "Switch health monitor starting on $Port @ $Baud for $DurationSeconds s" -ForegroundColor Cyan
Write-Host "Watching: ready/fault, wr_fail, rd_fail, sup_le, sw_edges" -ForegroundColor Yellow

$serial = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, One
$serial.ReadTimeout = 400
$serial.Open()

$start = Get-Date
$lastWrFail = $null
$lastRdFail = $null
$lastSupLe = $null

$sampleCount = 0
$warnCount = 0

try {
    while ((Get-Date) -lt $start.AddSeconds($DurationSeconds)) {
        try {
            $line = $serial.ReadLine()
        } catch {
            continue
        }

        if ($line -notmatch "Matrix link:") {
            continue
        }

        $sampleCount++

        $ready = 0
        $fault = 0
        $wrFail = 0
        $rdFail = 0
        $supLe = 0
        $swEdges = 0

        if ($line -match "ready=([0-9]+)") { $ready = [int]$matches[1] }
        if ($line -match "fault=([0-9]+)") { $fault = [int]$matches[1] }
        if ($line -match "wr_fail=([0-9]+)") { $wrFail = [int]$matches[1] }
        if ($line -match "rd_fail=([0-9]+)") { $rdFail = [int]$matches[1] }
        if ($line -match "sup_le=([0-9]+)") { $supLe = [int]$matches[1] }
        if ($line -match "sw_edges=([0-9]+)") { $swEdges = [int]$matches[1] }

        $elapsed = ((Get-Date) - $start).TotalSeconds
        if ($elapsed -lt 1) { $elapsed = 1 }

        $wrFailRate = 0.0
        $rdFailRate = 0.0
        $supLeRate = 0.0

        if ($lastWrFail -ne $null) { $wrFailRate = [Math]::Max(0.0, ($wrFail - $lastWrFail)) }
        if ($lastRdFail -ne $null) { $rdFailRate = [Math]::Max(0.0, ($rdFail - $lastRdFail)) }
        if ($lastSupLe -ne $null) { $supLeRate = [Math]::Max(0.0, ($supLe - $lastSupLe)) }

        $lastWrFail = $wrFail
        $lastRdFail = $rdFail
        $lastSupLe = $supLe

        $warn = $false
        if ($ready -eq 0 -or $fault -ne 0) { $warn = $true }
        if ($wrFailRate -gt $WarnWrFailPerSec) { $warn = $true }
        if ($rdFailRate -gt $WarnRdFailPerSec) { $warn = $true }
        if ($supLeRate -gt $WarnSuppressedLampEchoPerSec) { $warn = $true }

        $ts = (Get-Date).ToString("HH:mm:ss")
        $summary = "[$ts] ready=$ready fault=$fault wr_fail=$wrFail rd_fail=$rdFail sup_le=$supLe sw_edges=$swEdges"

        if ($warn) {
            $warnCount++
            Write-Host "$summary  <-- WARN" -ForegroundColor Red
        } else {
            Write-Host $summary -ForegroundColor Green
        }
    }
}
finally {
    if ($serial.IsOpen) { $serial.Close() }
}

Write-Host "\nMonitor complete. Samples=$sampleCount warnings=$warnCount" -ForegroundColor Cyan
if ($warnCount -gt 0) {
    Write-Host "Review WARN lines for regression while LED routines were active." -ForegroundColor Yellow
}
