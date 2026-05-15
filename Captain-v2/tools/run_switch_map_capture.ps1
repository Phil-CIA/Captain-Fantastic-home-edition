param(
    [string]$PortName = "COM5",
    [int]$BaudRate = 115200,
    [int]$BootSettleSeconds = 8,
    [int]$CaptureSeconds = 30,
    [string]$OutputFile = "map_capture_sync.txt"
)

Add-Type -AssemblyName System.IO.Ports

function Write-Stamped([string]$msg) {
    $ts = Get-Date -Format "HH:mm:ss"
    Write-Host "[$ts] $msg"
}

$port = New-Object System.IO.Ports.SerialPort($PortName, $BaudRate, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$port.ReadTimeout = 100
$port.NewLine = "`n"

try {
    Write-Stamped "Opening $PortName @ $BaudRate..."
    $port.Open()

    Write-Stamped "Resetting board (DTR/RTS pulse)..."
    $port.DtrEnable = $true
    $port.RtsEnable = $true
    Start-Sleep -Milliseconds 200
    $port.DtrEnable = $false
    $port.RtsEnable = $false

    if ($BootSettleSeconds -gt 0) {
        Write-Stamped "Boot settle countdown started. Do NOT press switches yet."
        for ($s = $BootSettleSeconds; $s -ge 1; $s--) {
            Write-Host ("  {0}..." -f $s)
            Start-Sleep -Seconds 1
        }
    }

    [console]::Beep(880, 180)
    [console]::Beep(1175, 220)
    Write-Stamped "START NOW: press one switch at a time (hold ~1 second, then release)."
    Write-Stamped "Capture window: $CaptureSeconds seconds"

    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $raw = New-Object System.Text.StringBuilder
    $lineBuffer = ""
    $mapLines = New-Object System.Collections.Generic.List[string]

    while ($stopwatch.Elapsed.TotalSeconds -lt $CaptureSeconds) {
        if ($port.BytesToRead -gt 0) {
            $chunk = $port.ReadExisting()
            [void]$raw.Append($chunk)
            $lineBuffer += $chunk

            while ($lineBuffer.Contains("`n")) {
                $idx = $lineBuffer.IndexOf("`n")
                $line = $lineBuffer.Substring(0, $idx).Trim("`r")
                $lineBuffer = $lineBuffer.Substring($idx + 1)
                if ($line -match "\[MAP\]") {
                    $mapLines.Add($line)
                    Write-Host $line
                }
            }
        } else {
            Start-Sleep -Milliseconds 25
        }
    }

    [System.IO.File]::WriteAllText($OutputFile, $raw.ToString())
    Write-Stamped "Capture ended. Raw log saved: $OutputFile"

    if ($mapLines.Count -eq 0) {
        Write-Stamped "No [MAP] events captured."
        exit 0
    }

    $tuples = $mapLines | ForEach-Object {
        if ($_ -match "row=(\d+) col=(\d+) bit=(\d+)") {
            "{0},{1},{2}" -f $Matches[1], $Matches[2], $Matches[3]
        }
    } | Sort-Object -Unique

    Write-Stamped ("[MAP] events: {0}" -f $mapLines.Count)
    Write-Stamped "Unique row,col,bit tuples:"
    $tuples | ForEach-Object { Write-Host "  $_" }
}
catch {
    Write-Error $_.Exception.Message
    exit 1
}
finally {
    if ($port -and $port.IsOpen) {
        $port.Close()
    }
}
