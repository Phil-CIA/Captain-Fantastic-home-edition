# Simple Music Server - No Admin Required (using high port)
# Serves MP3 files from the music/ folder on port 8080

$port = 8080
$musicFolder = Join-Path $PSScriptRoot "music"

Write-Host "`n=== Captain Fantastic Music Server ===" -ForegroundColor Cyan
Write-Host "Music folder: $musicFolder" -ForegroundColor Yellow
Write-Host "Port: $port" -ForegroundColor Yellow

# Get local IP
$localIP = (Get-NetIPAddress -AddressFamily IPv4 | Where-Object {$_.IPAddress -like "192.168.*"})[0].IPAddress
Write-Host "Server URL: http://$localIP`:$port/" -ForegroundColor Green
Write-Host "Press Ctrl+C to stop`n" -ForegroundColor Gray

# Create HTTP listener on high port (doesn't require admin)
$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add("http://+:$port/")

try {
    $listener.Start()
    Write-Host "✓ Server started successfully!" -ForegroundColor Green
    Write-Host "`nAvailable songs:" -ForegroundColor Cyan
    Get-ChildItem "$musicFolder\*.mp3" | ForEach-Object {
        Write-Host "  - http://$localIP`:$port/music/$($_.Name)" -ForegroundColor White
    }
    Write-Host ""
    
    while ($listener.IsListening) {
        $context = $listener.GetContext()
        $request = $context.Request
        $response = $context.Response
        
        $timestamp = Get-Date -Format "HH:mm:ss"
        Write-Host "[$timestamp] $($request.HttpMethod) $($request.Url.LocalPath)" -ForegroundColor Yellow
        
        # Parse URL
        $path = $request.Url.LocalPath
        
        # Serve MP3 file
        if ($path -match '^/music/(.+\.mp3)$') {
            $filename = $matches[1]
            $filePath = Join-Path $musicFolder $filename
            
            if (Test-Path $filePath) {
                Write-Host "  → Streaming: $filename" -ForegroundColor Green
                
                $fileBytes = [System.IO.File]::ReadAllBytes($filePath)
                $response.ContentType = "audio/mpeg"
                $response.ContentLength64 = $fileBytes.Length
                $response.AddHeader("Access-Control-Allow-Origin", "*")
                $response.AddHeader("Accept-Ranges", "bytes")
                $response.StatusCode = 200
                $response.OutputStream.Write($fileBytes, 0, $fileBytes.Length)
            } else {
                Write-Host "  → File not found: $filename" -ForegroundColor Red
                $response.StatusCode = 404
                $buffer = [System.Text.Encoding]::UTF8.GetBytes("404 - File Not Found")
                $response.OutputStream.Write($buffer, 0, $buffer.Length)
            }
        }
        # List files
        elseif ($path -eq "/" -or $path -eq "/music" -or $path -eq "/music/") {
            Write-Host "  → Listing files" -ForegroundColor Cyan
            
            $html = @"
<html><head><title>Music Server</title></head>
<body style="font-family: Arial; padding: 20px;">
<h1>Captain Fantastic Music Server</h1>
<h2>Available MP3 Files:</h2>
<ul>
"@
            Get-ChildItem "$musicFolder\*.mp3" | ForEach-Object {
                $html += "<li><a href='/music/$($_.Name)'>$($_.Name)</a> ($([math]::Round($_.Length/1KB, 1)) KB)</li>`n"
            }
            $html += @"
</ul>
<p><small>Server running on port $port</small></p>
</body></html>
"@
            $buffer = [System.Text.Encoding]::UTF8.GetBytes($html)
            $response.ContentType = "text/html"
            $response.ContentLength64 = $buffer.Length
            $response.StatusCode = 200
            $response.OutputStream.Write($buffer, 0, $buffer.Length)
        }
        else {
            $response.StatusCode = 404
            $buffer = [System.Text.Encoding]::UTF8.GetBytes("404 - Not Found")
            $response.OutputStream.Write($buffer, 0, $buffer.Length)
        }
        
        $response.OutputStream.Close()
    }
}
catch {
    Write-Host "`nERROR: $_" -ForegroundColor Red
    if ($_.Exception.InnerException) {
        Write-Host "Inner Exception: $($_.Exception.InnerException.Message)" -ForegroundColor Red
    }
}
finally {
    if ($listener.IsListening) {
        $listener.Stop()
    }
    $listener.Close()
    Write-Host "`nServer stopped." -ForegroundColor Yellow
}
