#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "DMXPort.h"

// ============================================================
//  ConfigManager – NVS-backed configuration + serial commands
// ============================================================
//  Reads/writes config to NVS flash (survives reboots).
//  Listens on USB CDC1 (native USB-C, second virtual COM) for commands:
//
//    CONFIG {...json...}   – set one or more config fields
//    STATUS                – print current config as JSON
//    REBOOT                – restart ESP32
//    HELP                  – list commands
//
//  CONFIG fields (all optional, only specified fields updated):
//    ssid          string   WiFi SSID (STA mode)
//    pass          string   WiFi password
//    ip            string   Static IP (empty = DHCP)
//    gw            string   Gateway IP
//    subnet        string   Subnet mask
//    artnet_port   int      Art-Net UDP port (default 6454)
//    u1_artnet     int      Art-Net universe number → Universe 1
//    u2_artnet     int      Art-Net universe number → Universe 2
//    u1_mode       string   "TX" | "RX" | "PASS"
//    u2_mode       string   "TX" | "RX" | "PASS"
// ============================================================

struct DMXNodeConfig {
    char     ssid[64]     = "";
    char     pass[64]     = "";
    char     ip[16]       = "";         // empty = DHCP
    char     gw[16]       = "";
    char     subnet[16]   = "255.255.255.0";
    uint16_t artnetPort   = 6454;
    uint16_t u1Artnet     = 0;
    uint16_t u2Artnet     = 1;
    DMXMode  u1Mode       = DMXMode::TX;
    DMXMode  u2Mode       = DMXMode::TX;
};

class ConfigManager {
public:
    ConfigManager();

    // Load config from NVS (call in setup before anything else)
    void load();

    // Save current config to NVS
    void save();

    // Reset to factory defaults and save
    void reset();

    // Get config reference (read/modify directly)
    DMXNodeConfig& config() { return _cfg; }
    const DMXNodeConfig& config() const { return _cfg; }

    // Runtime DMX monitor toggle (not persisted — resets on reboot)
    bool isDMXMonitorEnabled() const { return _dmxMonitor; }
    void setDMXMonitor(bool on)      { _dmxMonitor = on; }

    // Returns true if WiFi credentials have been configured
    bool hasWiFiCredentials() const;

    // Returns true (once) if a CONFIG command was received since last call.
    // Caller should reconnect WiFi / re-init DMX after checking this.
    bool wasUpdated() { bool v = _configUpdated; _configUpdated = false; return v; }

    // Set the output stream for all terminal responses (call before first feedChar)
    void setOutput(Stream& s) { _out = &s; }

    // Process one character from the config serial port
    // Returns true if a full command was processed
    bool feedChar(char c);

    // Supply live RX frame/byte count pointers so STATUS includes them.
    // Call once in setup() after DMXPorts are constructed.
    void setRxCounters(const uint32_t* u1Frames, const uint32_t* u2Frames,
                       const uint32_t* u1Bytes,  const uint32_t* u2Bytes) {
        _u1RxCount = u1Frames;  _u2RxCount = u2Frames;
        _u1RxBytes = u1Bytes;   _u2RxBytes = u2Bytes;
    }

    // Print current config as JSON to given stream
    void printStatus(Stream& out,
                     uint32_t u1RxFrames = 0, uint32_t u2RxFrames = 0) const;

private:
    bool _processLine(const char* line);
    void _scanAndPrintNetworks();
    DMXMode _parseMode(const char* s);
    const char* _modeStr(DMXMode m) const;

    DMXNodeConfig _cfg;
    Preferences   _prefs;
    bool          _dmxMonitor  = false;
    Stream*       _out         = nullptr;
    const uint32_t* _u1RxCount = nullptr;
    const uint32_t* _u2RxCount = nullptr;
    const uint32_t* _u1RxBytes = nullptr;
    const uint32_t* _u2RxBytes = nullptr;

    char     _lineBuf[512];
    uint16_t _lineIdx;
    bool     _configUpdated = false;
};
