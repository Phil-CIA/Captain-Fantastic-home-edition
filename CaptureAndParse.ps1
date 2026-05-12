param(
    [string]$Port = "COM5",
    [int]$Baud = 115200,
    [int]$PrepTime = 8,
    [int]$CaptureTime = 25
)

Write-Host "PREP NOTICE: Starting in $PrepTime seconds..."
Start-Sleep -Seconds $PrepTime
Write-Host "PRESS SAME TARGET SWITCH REPEATEDLY NOW"

$lines = New-Object System.Collections.Generic.List[string]
$startTime = Get-Date

$portObj = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, One
$portObj.ReadTimeout = 500
$portObj.Open()

while ((Get-Date) -lt $startTime.AddSeconds($CaptureTime)) {
    try {
        $line = $portObj.ReadLine()
        if ($line -match "Matrix link:") {
            $lines.Add($line)
        }
    } catch { }
}
$portObj.Close()

$sw0Values = New-Object System.Collections.Generic.List[int]
$edgesReported = New-Object System.Collections.Generic.List[int]
foreach ($line in $lines) {
    if ($line -match "sw0=0x([0-9A-Fa-f]+)") {
        $sw0Values.Add([Convert]::ToInt32($matches[1], 16))
    }
    if ($line -match "sw_edges=([0-9]+)") {
        $edgesReported.Add([int]$matches[1])
    }
}

$risingCounts = @(0,0,0,0,0,0,0,0)
$lastSw0 = -1
$swTransitions = 0

foreach ($val in $sw0Values) {
    if ($lastSw0 -ne -1 -and $val -ne $lastSw0) {
        $swTransitions++
        for ($i = 0; $i -lt 8; $i++) {
            $bit = 1 -shl $i
            if (($val -band $bit) -and -not ($lastSw0 -band $bit)) {
                $risingCounts[$i]++
            }
        }
    }
    $lastSw0 = $val
}

$uniqueSw0_vals = ($sw0Values | Select-Object -Unique)
$activeBit = -1
for($i=0; $i -lt 8; $i++) {
    if ($risingCounts[$i] -ge 5) { $activeBit = $i; break }
}

$uniqueCount = if ($uniqueSw0_vals -is [array]) { $uniqueSw0_vals.Count } elseif ($null -ne $uniqueSw0_vals) { 1 } else { 0 }
$maxSwEdges = if ($edgesReported.Count -gt 0) { ($edgesReported | Measure-Object -Maximum).Maximum } else { 0 }
$sortedCounts = $risingCounts | Sort-Object -Descending
$top = $sortedCounts[0]
$second = $sortedCounts[1]
$isDominant = ($top -ge 5 -and ($second -eq 0 -or $top -ge (2 * $second)))

Write-Host "Summary:"
Write-Host "Lines Captured: $($lines.Count)"
Write-Host "Unique sw0: $uniqueCount"
Write-Host "Max sw_edges: $maxSwEdges"
Write-Host "Rising Counts: $($risingCounts -join ', ')"
Write-Host "Dominant Bit: $isDominant"
