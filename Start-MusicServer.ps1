# Captain Fantastic Music Server (PowerShell)
# Simple HTTP server to stream MP3s to ESP32

$port = 8000
$musicDir = Join-Path $PSScriptRoot "music"

# Get local IP
$localIP = (Get-NetIPAddress -AddressFamily IPv4 | Where-Object {$_.IPAddress -like "192.168.*"} | Select-Object -First 1).IPAddress

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "  CAPTAIN FANTASTIC - MUSIC SERVER" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

# Check music folder
if (-not (Test-Path $musicDir)) {
    Write-Host "ERROR: Music folder not found!" -ForegroundColor Red
    Write-Host "Creating folder: $musicDir`n"
    New-Item -ItemType Directory -Path $musicDir | Out-Null
}

# List MP3 files
$mp3Files = Get-ChildItem -Path $musicDir -Filter "*.mp3"
if ($mp3Files.Count -eq 0) {
    Write-Host "WARNING: No MP3 files found in music folder!" -ForegroundColor Yellow
} else {
    Write-Host "Found $($mp3Files.Count) MP3 file(s):" -ForegroundColor Green
    foreach ($file in $mp3Files | Sort-Object Name) {
        $sizeKB = [math]::Round($file.Length / 1KB, 1)
        Write-Host "  - $($file.Name) ($sizeKB KB)"
    }
}

Write-Host "`nServer Configuration:" -ForegroundColor Cyan
Write-Host "  Local IP:    $localIP"
Write-Host "  Port:        $port"
Write-Host "  URL:         http://${localIP}:${port}/"
Write-Host "  Music Path:  $musicDir"

Write-Host "`nESP32 will stream from:" -ForegroundColor Green
Write-Host "  http://${localIP}:${port}/music/pinball_wizard.mp3"

Write-Host "`nPress Ctrl+C to stop server`n" -ForegroundColor Yellow
Write-Host "========================================`n" -ForegroundColor Cyan

# Create HTTP listener
$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add("http://+:$port/")
$listener.Start()

Write-Host "[$(Get-Date -Format 'HH:mm:ss')] Server started successfully!" -ForegroundColor Green

try {
    while ($listener.IsListening) {
        # Wait for request
        $context = $listener.GetContext()
        $request = $context.Request
        $response = $context.Response
        
        $timestamp = Get-Date -Format 'HH:mm:ss'
        $clientIP = $request.RemoteEndPoint.Address
        $url = $request.Url.LocalPath
        
        Write-Host "[$timestamp] $clientIP - GET $url" -ForegroundColor Gray
        
        # Parse request path
        if ($url -eq "/") {
            # Root - show file list
            $html = @"
<html><head><title>Captain Fantastic Music Server</title></head>
<body style='font-family: monospace; padding: 20px;'>
<h2>Captain Fantastic - Music Server</h2>
<p>Available MP3 files:</p>
<ul>
"@
            foreach ($file in $mp3Files | Sort-Object Name) {
                $sizeKB = [math]::Round($file.Length / 1KB, 1)
                $html += "<li><a href='/music/$($file.Name)'>$($file.Name)</a> ($sizeKB KB)</li>`n"
            }
            $html += "</ul></body></html>"
            
            $buffer = [System.Text.Encoding]::UTF8.GetBytes($html)
            $response.ContentType = "text/html"
            $response.ContentLength64 = $buffer.Length
            $response.OutputStream.Write($buffer, 0, $buffer.Length)
            
        } elseif ($url -match "^/music/(.+\.mp3)$") {
            # Serve MP3 file
            $filename = $matches[1]
            $filePath = Join-Path $musicDir $filename
            
            if (Test-Path $filePath) {
                $fileBytes = [System.IO.File]::ReadAllBytes($filePath)
                $response.ContentType = "audio/mpeg"
                $response.ContentLength64 = $fileBytes.Length
                $response.AddHeader("Accept-Ranges", "bytes")
                $response.AddHeader("Cache-Control", "no-cache")
                $response.OutputStream.Write($fileBytes, 0, $fileBytes.Length)
                Write-Host "[$timestamp] Served: $filename ($([math]::Round($fileBytes.Length/1KB,1)) KB)" -ForegroundColor Green
            } else {
                $response.StatusCode = 404
                $error = "File not found: $filename"
                $buffer = [System.Text.Encoding]::UTF8.GetBytes($error)
                $response.OutputStream.Write($buffer, 0, $buffer.Length)
                Write-Host "[$timestamp] ERROR: File not found - $filename" -ForegroundColor Red
            }
        } else {
            # Unknown path
            $response.StatusCode = 404
            $buffer = [System.Text.Encoding]::UTF8.GetBytes("Not Found")
            $response.OutputStream.Write($buffer, 0, $buffer.Length)
        }
        
        $response.Close()
    }
} finally {
    $listener.Stop()
    Write-Host "`n[$(Get-Date -Format 'HH:mm:ss')] Server stopped" -ForegroundColor Yellow
}
