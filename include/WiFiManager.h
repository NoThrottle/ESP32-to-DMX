#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "ConfigManager.h"

// ============================================================
//  WiFiManager – STA / AP WiFi connection helper
// ============================================================
//  - If credentials are stored: tries STA, falls back to AP
//  - If no credentials: starts AP immediately
//  - Prints connection info to Serial on boot
// ============================================================

class WiFiManager {
public:
    WiFiManager();

    // Call in setup() after ConfigManager::load()
    // Returns true if STA connected, false if AP started
    bool begin(const DMXNodeConfig& cfg);

    // Reconnect with new credentials without rebooting (call after CONFIG change)
    bool reconnect(const DMXNodeConfig& cfg);

    bool isConnected()  const;
    bool isAPMode()     const { return _apMode; }
    IPAddress localIP() const { return WiFi.localIP(); }

private:
    bool _startSTA(const DMXNodeConfig& cfg);
    void _startAP();

    bool _apMode;
};
