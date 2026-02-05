#include "ota_http_server.h"

OTAHttpServer::OTAHttpServer(ExternalFlashOTA* ota, uint16_t port) 
    : _server(port), _ota(ota) {
}

void OTAHttpServer::begin() {
    _server.on("/", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/status", HTTP_GET, [this]() { handleStatus(); });
    _server.on("/upload", HTTP_GET, [this]() { handleUpload(); });
    _server.on("/upload", HTTP_POST, 
        [this]() { _server.send(200); },
        [this]() { handleUploadData(); }
    );
    _server.on("/flash", HTTP_POST, [this]() { handleFlash(); });
    _server.on("/cancel", HTTP_POST, [this]() { handleCancel(); });
    _server.onNotFound([this]() { handleNotFound(); });
    
    _server.begin();
    Serial.println("[OTA HTTP] Server started on port 80");
    Serial.println("[OTA HTTP] Navigate to http://192.168.0.198/ to upload firmware");
}

void OTAHttpServer::handleClient() {
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 10000) {
        Serial.println("[OTA HTTP] handleClient() is being called");
        lastDebug = millis();
    }
    _server.handleClient();
}

void OTAHttpServer::handleRoot() {
    Serial.println("[OTA HTTP] Request received for /");
    _server.send(200, "text/html", getStatusPage());
}

void OTAHttpServer::handleStatus() {
    String json = "{";
    json += "\"stage\":\"" + String((int)_ota->getStage()) + "\",";
    json += "\"bytesWritten\":" + String(_ota->getBytesWritten()) + ",";
    json += "\"totalSize\":" + String(_ota->getTotalSize()) + ",";
    json += "\"progress\":" + String(_ota->getProgress(), 1) + ",";
    json += "\"hasPending\":" + String(_ota->hasPendingFirmware() ? "true" : "false");
    json += "}";
    
    _server.send(200, "application/json", json);
}

void OTAHttpServer::handleUpload() {
    _server.send(200, "text/html", getUploadPage());
}

void OTAHttpServer::handleUploadData() {
    static bool uploadStarted = false;
    static size_t totalReceived = 0;
    static size_t expectedSize = 0;
    
    HTTPUpload& upload = _server.upload();
    
    if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("[OTA HTTP] Upload started: %s\n", upload.filename.c_str());
        
        // Get file size from Content-Length header (multipart doesn't populate upload.totalSize)
        expectedSize = 0;
        if (_server.hasHeader("Content-Length")) {
            String contentLength = _server.header("Content-Length");
            expectedSize = contentLength.toInt();
            // Subtract multipart overhead (approx 300-500 bytes, use 1KB to be safe)
            if (expectedSize > 1024) {
                expectedSize -= 1024;
            }
            Serial.printf("[OTA HTTP] Content-Length: %u, Expected firmware size: ~%u bytes\n", 
                         contentLength.toInt(), expectedSize);
        }
        
        // Use expected size or fallback to a large value (2MB max firmware partition)
        size_t downloadSize = (expectedSize > 0) ? expectedSize : (2 * 1024 * 1024);
        uploadStarted = _ota->startDownload(downloadSize, upload.filename.c_str());
        totalReceived = 0;
        
        if (!uploadStarted) {
            Serial.println("[OTA HTTP] ERROR: Failed to start download");
        }
        
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadStarted) {
            if (_ota->writeChunk(upload.buf, upload.currentSize)) {
                totalReceived += upload.currentSize;
            } else {
                Serial.println("[OTA HTTP] ERROR: Failed to write chunk");
                uploadStarted = false;
            }
        }
        
    } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadStarted) {
            Serial.printf("[OTA HTTP] Upload finished: %u bytes\n", totalReceived);
            if (_ota->finishDownload()) {
                Serial.println("[OTA HTTP] Firmware ready!");
            } else {
                Serial.println("[OTA HTTP] ERROR: Failed to finalize download");
            }
        }
        uploadStarted = false;
        totalReceived = 0;
        
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Serial.println("[OTA HTTP] Upload aborted");
        if (uploadStarted) {
            _ota->cancelDownload();
        }
        uploadStarted = false;
        totalReceived = 0;
    }
}

void OTAHttpServer::handleFlash() {
    if (_ota->hasPendingFirmware()) {
        _server.send(200, "text/plain", "Flashing firmware... Device will reboot.");
        delay(1000);
        _ota->flashFromExternalFlash();
    } else {
        _server.send(400, "text/plain", "No firmware ready to flash");
    }
}

void OTAHttpServer::handleCancel() {
    _ota->clearPending();
    _server.send(200, "text/plain", "OTA cancelled");
}

void OTAHttpServer::handleNotFound() {
    Serial.printf("[OTA HTTP] 404 Not Found: %s\n", _server.uri().c_str());
    _server.send(404, "text/plain", "Not Found");
}

String OTAHttpServer::getStatusPage() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<title>Captain Fantastic OTA</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body { font-family: Arial; margin: 20px; background: #222; color: #fff; }";
    html += "h1 { color: #4CAF50; }";
    html += ".status { background: #333; padding: 20px; border-radius: 5px; margin: 10px 0; }";
    html += ".button { background: #4CAF50; color: white; padding: 15px 30px; ";
    html += "border: none; border-radius: 5px; font-size: 16px; cursor: pointer; margin: 5px; }";
    html += ".button:hover { background: #45a049; }";
    html += ".danger { background: #f44336; }";
    html += ".danger:hover { background: #da190b; }";
    html += "</style>";
    html += "<script>";
    html += "function updateStatus() {";
    html += "  fetch('/status').then(r => r.json()).then(data => {";
    html += "    document.getElementById('progress').innerHTML = ";
    html += "      'Progress: ' + data.progress.toFixed(1) + '%<br>' +";
    html += "      'Bytes: ' + data.bytesWritten + ' / ' + data.totalSize;";
    html += "    document.getElementById('flashBtn').style.display = ";
    html += "      data.hasPending ? 'inline-block' : 'none';";
    html += "  });";
    html += "}";
    html += "setInterval(updateStatus, 2000);";
    html += "updateStatus();";
    html += "</script>";
    html += "</head><body>";
    html += "<h1>🎯 Captain Fantastic - OTA Update</h1>";
    html += "<div class='status'>";
    html += "<h2>Status</h2>";
    html += "<div id='progress'>Loading...</div>";
    html += "</div>";
    html += "<div style='margin: 20px 0;'>";
    html += "<button class='button' onclick=\"location.href='/upload'\">Upload Firmware</button>";
    html += "<button id='flashBtn' class='button' style='display:none' ";
    html += "onclick=\"if(confirm('Flash firmware now?'))fetch('/flash',{method:'POST'})\">Flash Now!</button>";
    html += "<button class='button danger' onclick=\"if(confirm('Cancel OTA?'))fetch('/cancel',{method:'POST'})\">Cancel OTA</button>";
    html += "</div>";
    html += "<div class='status'>";
    html += "<h3>IP Address</h3>";
    html += "<p>" + WiFi.localIP().toString() + "</p>";
    html += "<h3>Free Heap</h3>";
    html += "<p>" + String(ESP.getFreeHeap()) + " bytes</p>";
    html += "</div>";
    html += "</body></html>";
    
    return html;
}

String OTAHttpServer::getUploadPage() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<title>Upload Firmware</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body { font-family: Arial; margin: 20px; background: #222; color: #fff; }";
    html += "h1 { color: #4CAF50; }";
    html += ".upload-form { background: #333; padding: 30px; border-radius: 5px; max-width: 500px; }";
    html += "input[type=file] { margin: 20px 0; color: #fff; }";
    html += ".button { background: #4CAF50; color: white; padding: 15px 30px; ";
    html += "border: none; border-radius: 5px; font-size: 16px; cursor: pointer; }";
    html += ".button:hover { background: #45a049; }";
    html += "#progress { margin: 20px 0; display: none; }";
    html += ".progress-bar { width: 100%; height: 30px; background: #444; border-radius: 5px; }";
    html += ".progress-fill { height: 100%; background: #4CAF50; border-radius: 5px; width: 0%; }";
    html += "</style>";
    html += "<script>";
    html += "function uploadFile() {";
    html += "  var file = document.getElementById('firmware').files[0];";
    html += "  if (!file) { alert('Select a file first'); return; }";
    html += "  var formData = new FormData();";
    html += "  formData.append('firmware', file);";
    html += "  document.getElementById('progress').style.display = 'block';";
    html += "  var xhr = new XMLHttpRequest();";
    html += "  xhr.upload.addEventListener('progress', function(e) {";
    html += "    var percent = (e.loaded / e.total) * 100;";
    html += "    document.getElementById('progressFill').style.width = percent + '%';";
    html += "    document.getElementById('progressText').innerHTML = 'Uploading: ' + percent.toFixed(0) + '%';";
    html += "  });";
    html += "  xhr.addEventListener('load', function(e) {";
    html += "    document.getElementById('progressText').innerHTML = 'Upload complete! Redirecting...';";
    html += "    setTimeout(function() { location.href = '/'; }, 2000);";
    html += "  });";
    html += "  xhr.open('POST', '/upload');";
    html += "  xhr.send(formData);";
    html += "}";
    html += "</script>";
    html += "</head><body>";
    html += "<h1>📤 Upload Firmware</h1>";
    html += "<div class='upload-form'>";
    html += "<h2>Select firmware.bin file</h2>";
    html += "<input type='file' id='firmware' accept='.bin'>";
    html += "<br><button class='button' onclick='uploadFile()'>Upload</button>";
    html += "<div id='progress'>";
    html += "<p id='progressText'>Uploading...</p>";
    html += "<div class='progress-bar'><div id='progressFill' class='progress-fill'></div></div>";
    html += "</div>";
    html += "</div>";
    html += "<p><a href='/' style='color: #4CAF50;'>← Back to Status</a></p>";
    html += "</body></html>";
    
    return html;
}
