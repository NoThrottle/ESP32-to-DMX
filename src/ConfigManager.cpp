#include "ConfigManager.h"
#include "config.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <string.h>
#include <stdlib.h>

ConfigManager::ConfigManager() : _lineIdx(0) {
    memset(_lineBuf, 0, sizeof(_lineBuf));
}

void ConfigManager::load() {
    _prefs.begin(NVS_NAMESPACE, false);  // read-write: creates namespace on first boot

    _prefs.getString("ssid",   _cfg.ssid,   sizeof(_cfg.ssid));
    _prefs.getString("pass",   _cfg.pass,   sizeof(_cfg.pass));
    _prefs.getString("ip",     _cfg.ip,     sizeof(_cfg.ip));
    _prefs.getString("gw",     _cfg.gw,     sizeof(_cfg.gw));
    _prefs.getString("subnet", _cfg.subnet, sizeof(_cfg.subnet));

    _cfg.artnetPort = _prefs.getUShort("artnet_port", 6454);
    _cfg.u1Artnet   = _prefs.getUShort("u1_artnet",   0);
    _cfg.u2Artnet   = _prefs.getUShort("u2_artnet",   1);
    _cfg.u1Mode     = (DMXMode)_prefs.getUChar("u1_mode", (uint8_t)DMXMode::TX);
    _cfg.u2Mode     = (DMXMode)_prefs.getUChar("u2_mode", (uint8_t)DMXMode::TX);

    // Apply defaults if subnet not stored
    if (_cfg.subnet[0] == '\0') strncpy(_cfg.subnet, "255.255.255.0", sizeof(_cfg.subnet));

    _prefs.end();
}

void ConfigManager::save() {
    _prefs.begin(NVS_NAMESPACE, false);  // read-write

    _prefs.putString("ssid",   _cfg.ssid);
    _prefs.putString("pass",   _cfg.pass);
    _prefs.putString("ip",     _cfg.ip);
    _prefs.putString("gw",     _cfg.gw);
    _prefs.putString("subnet", _cfg.subnet);
    _prefs.putUShort("artnet_port", _cfg.artnetPort);
    _prefs.putUShort("u1_artnet",   _cfg.u1Artnet);
    _prefs.putUShort("u2_artnet",   _cfg.u2Artnet);
    _prefs.putUChar ("u1_mode",     (uint8_t)_cfg.u1Mode);
    _prefs.putUChar ("u2_mode",     (uint8_t)_cfg.u2Mode);

    _prefs.end();
}

void ConfigManager::reset() {
    memset(&_cfg, 0, sizeof(_cfg));
    strncpy(_cfg.subnet, "255.255.255.0", sizeof(_cfg.subnet));
    _cfg.artnetPort = 6454;
    _cfg.u1Artnet   = 0;
    _cfg.u2Artnet   = 1;
    _cfg.u1Mode     = DMXMode::TX;
    _cfg.u2Mode     = DMXMode::TX;
    save();
}

bool ConfigManager::hasWiFiCredentials() const {
    return _cfg.ssid[0] != '\0';
}

// ---------------------------------------------------------------------------
//  feedChar() — accept one byte from the config UART
//  Accumulates a line; processes when '\n' received.
// ---------------------------------------------------------------------------
bool ConfigManager::feedChar(char c) {
    if (c == '\r') return false;
    if (c == '\n') {
        _lineBuf[_lineIdx] = '\0';
        bool handled = _processLine(_lineBuf);
        _lineIdx = 0;
        return handled;
    }
    if (_lineIdx < sizeof(_lineBuf) - 1) {
        _lineBuf[_lineIdx++] = c;
    }
    return false;
}

bool ConfigManager::_processLine(const char* line) {
    // Skip whitespace
    while (*line == ' ') ++line;

    if (strncasecmp(line, "STATUS", 6) == 0) {
        printStatus(*_out);
        return true;
    }

    if (strncasecmp(line, "REBOOT", 6) == 0) {
        _out->println("{\"status\":\"rebooting\"}");
        delay(200);
        ESP.restart();
        return true;
    }

    if (strncasecmp(line, "RESET", 5) == 0) {
        reset();
        _out->println("{\"status\":\"factory reset, rebooting\"}");
        delay(200);
        ESP.restart();
        return true;
    }

    if (strncasecmp(line, "DMXMON ON", 9) == 0) {
        _dmxMonitor = true;
        _out->println("{\"status\":\"dmx monitor ON\"}");
        return true;
    }

    if (strncasecmp(line, "DMXMON OFF", 10) == 0) {
        _dmxMonitor = false;
        _out->println("{\"status\":\"dmx monitor OFF\"}");
        return true;
    }

    if (strncasecmp(line, "WIFISCAN", 8) == 0) {
        _scanAndPrintNetworks();
        return true;
    }

    if (strncasecmp(line, "HELP", 4) == 0) {
        _out->println(
            "Commands:\n"
            "  CONFIG {...}   - set config fields (JSON)\n"
            "  STATUS         - print current config\n"
            "  WIFISCAN       - scan for WiFi networks (returns JSON)\n"
            "  DMXMON ON|OFF  - toggle live DMX channel monitor\n"
            "  REBOOT         - restart device\n"
            "  RESET          - factory reset\n"
            "  HELP           - this message\n"
            "\nCONFIG fields:\n"
            "  ssid, pass, ip, gw, subnet\n"
            "  artnet_port, u1_artnet, u2_artnet\n"
            "  u1_mode, u2_mode  (TX|RX|PASS)\n"
            "\nFor DMX relay: set u1_mode=RX and u2_mode=TX (or vice versa),\n"
            "then reboot. Received frames are automatically forwarded.\n"
            "Use DMXMON ON to print live channel values."
        );
        return true;
    }

    if (strncasecmp(line, "CONFIG ", 7) == 0) {
        const char* json = line + 7;
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, json);
        if (err) {
            _out->printf("{\"error\":\"JSON parse failed: %s\"}\n", err.c_str());
            return false;
        }

        if (doc["ssid"].is<const char*>())   strncpy(_cfg.ssid,   doc["ssid"],   sizeof(_cfg.ssid)-1);
        if (doc["pass"].is<const char*>())   strncpy(_cfg.pass,   doc["pass"],   sizeof(_cfg.pass)-1);
        if (doc["ip"].is<const char*>())     strncpy(_cfg.ip,     doc["ip"],     sizeof(_cfg.ip)-1);
        if (doc["gw"].is<const char*>())     strncpy(_cfg.gw,     doc["gw"],     sizeof(_cfg.gw)-1);
        if (doc["subnet"].is<const char*>()) strncpy(_cfg.subnet, doc["subnet"], sizeof(_cfg.subnet)-1);

        if (doc["artnet_port"].is<uint16_t>()) _cfg.artnetPort = doc["artnet_port"];
        if (doc["u1_artnet"].is<uint16_t>())   _cfg.u1Artnet   = doc["u1_artnet"];
        if (doc["u2_artnet"].is<uint16_t>())   _cfg.u2Artnet   = doc["u2_artnet"];
        if (doc["u1_mode"].is<const char*>())  _cfg.u1Mode = _parseMode(doc["u1_mode"]);
        if (doc["u2_mode"].is<const char*>())  _cfg.u2Mode = _parseMode(doc["u2_mode"]);

        save();
        _out->println("{\"status\":\"saved, rebooting\"}");
        delay(200);
        ESP.restart();
        return true;
    }

    // Unknown command — don't print anything if line is empty
    if (line[0] != '\0') {
        _out->printf("{\"error\":\"unknown command: %s\"}\n", line);
    }
    return false;
}

void ConfigManager::printStatus(Stream& out) const {
    JsonDocument doc;
    doc["ssid"]        = _cfg.ssid;
    doc["ip"]          = _cfg.ip;
    doc["gw"]          = _cfg.gw;
    doc["subnet"]      = _cfg.subnet;
    doc["artnet_port"] = _cfg.artnetPort;
    doc["u1_artnet"]   = _cfg.u1Artnet;
    doc["u2_artnet"]   = _cfg.u2Artnet;
    doc["u1_mode"]     = _modeStr(_cfg.u1Mode);
    doc["u2_mode"]     = _modeStr(_cfg.u2Mode);
    doc["firmware"]    = "1.0.0";
    serializeJson(doc, out);
    out.println();
}

DMXMode ConfigManager::_parseMode(const char* s) {
    if (strcasecmp(s, "RX")   == 0) return DMXMode::RX;
    if (strcasecmp(s, "PASS") == 0) return DMXMode::PASSTHROUGH;
    return DMXMode::TX;
}

const char* ConfigManager::_modeStr(DMXMode m) const {
    switch (m) {
    case DMXMode::RX:          return "RX";
    case DMXMode::PASSTHROUGH: return "PASS";
    default:                   return "TX";
    }
}

void ConfigManager::_scanAndPrintNetworks() {
    // Ensure WiFi is in a mode that supports scanning
    wifi_mode_t prevMode = WiFi.getMode();
    bool modeChanged = false;
    if (prevMode == WIFI_MODE_AP) {
        WiFi.mode(WIFI_AP_STA);
        modeChanged = true;
    } else if (prevMode == WIFI_MODE_NULL) {
        WiFi.mode(WIFI_STA);
        modeChanged = true;
    }

    int n = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false);

    JsonDocument doc;
    if (n < 0) {
        doc["error"] = "scan failed";
    } else {
        JsonArray arr = doc["networks"].to<JsonArray>();
        for (int i = 0; i < n && i < 30; i++) {
            JsonObject net = arr.add<JsonObject>();
            net["ssid"]   = WiFi.SSID(i);
            net["rssi"]   = WiFi.RSSI(i);
            net["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        }
    }
    serializeJson(doc, *_out);
    _out->println();
    WiFi.scanDelete();

    // Restore previous mode if changed
    if (modeChanged) {
        WiFi.mode(prevMode);
    }
}
