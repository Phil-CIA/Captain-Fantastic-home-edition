#ifndef OTA_HTTP_SERVER_H
#define OTA_HTTP_SERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include "external_flash_ota.h"

class OTAHttpServer {
public:
    OTAHttpServer(ExternalFlashOTA* ota, uint16_t port = 80);
    
    void begin();
    void handleClient();
    
private:
    WebServer _server;
    ExternalFlashOTA* _ota;
    
    // HTTP handlers
    void handleRoot();
    void handleStatus();
    void handleUpload();
    void handleUploadData();
    void handleFlash();
    void handleCancel();
    void handleNotFound();
    
    String getStatusPage();
    String getUploadPage();
};

#endif // OTA_HTTP_SERVER_H
