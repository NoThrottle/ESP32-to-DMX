#include "WiFiManager.h"
#include "config.h"
#include <WiFi.h>

WiFiManager::WiFiManager() : _apMode(false) {}

bool WiFiManager::begin(const DMXNodeConfig& cfg) {
    WiFi.mode(WIFI_STA);

    if (cfg.ssid[0] != '\0') {
        if (_startSTA(cfg)) return true;
        Serial0.println("[WiFi] STA failed, falling back to AP mode");
    } else {
        Serial0.println("[WiFi] No credentials stored, starting AP mode");
    }
    _startAP();
    return false;
}

bool WiFiManager::_startSTA(const DMXNodeConfig& cfg) {
    // Apply static IP if configured
    if (cfg.ip[0] != '\0') {
        IPAddress ip, gw, sn;
        if (ip.fromString(cfg.ip) && gw.fromString(cfg.gw) && sn.fromString(cfg.subnet)) {
            WiFi.config(ip, gw, sn);
        }
    }

    WiFi.begin(cfg.ssid, cfg.pass);
    Serial0.printf("[WiFi] Connecting to %s", cfg.ssid);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > 10000) {
            Serial0.println(" TIMEOUT");
            return false;
        }
        delay(250);
        Serial0.print('.');
    }

    Serial0.printf("\n[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
    _apMode = false;
    return true;
}

void WiFiManager::_startAP() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    Serial0.printf("[WiFi] AP started. SSID: %s  IP: %s\n",
                  WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
    _apMode = true;
}

bool WiFiManager::isConnected() const {
    return _apMode ? true : (WiFi.status() == WL_CONNECTED);
}
