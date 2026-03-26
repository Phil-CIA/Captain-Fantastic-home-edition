#ifndef OTA_CONFIG_H
#define OTA_CONFIG_H

#include <Arduino.h>

constexpr bool CAPTAIN_OTA_ENABLED = true;
constexpr const char* CAPTAIN_WIFI_SSID = "Forche main 2.4";
constexpr const char* CAPTAIN_WIFI_PASSWORD = "gizmoa22";
constexpr const char* CAPTAIN_OTA_HOSTNAME = "captain-v2-control";
constexpr const char* CAPTAIN_OTA_PASSWORD = "pinball2026";
constexpr uint32_t CAPTAIN_WIFI_CONNECT_TIMEOUT_MS = 15000;

#endif