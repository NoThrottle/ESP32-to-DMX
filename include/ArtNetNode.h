#pragma once
#include <Arduino.h>
#include <WiFiUdp.h>
#include <functional>
#include <vector>

// ============================================================
//  ArtNetNode – Art-Net 4 UDP node (transmit + receive)
// ============================================================
//  Listens on UDP port 6454 for ArtDmx packets.
//  Routes incoming frames to DMX universes via callback.
//  Replies to ArtPoll for device discovery.
//  In RX mode: broadcasts ArtDmx with received DMX data.
// ============================================================

using ArtNetFrameCallback = std::function<void(uint16_t universe, const uint8_t* data, uint16_t len)>;

class ArtNetNode {
public:
    ArtNetNode();

    // Call after WiFi is connected
    // nodeName: shown in Art-Net browsers (max 17 chars)
    void begin(const char* nodeName = "ESP32-DMX", uint16_t port = 6454);

    // Register callback for incoming ArtDmx frames
    void onFrame(ArtNetFrameCallback cb) { _frameCb = cb; }

    // Send a DMX frame out as ArtDmx (e.g. from DMX RX input)
    // Broadcasts to 255.255.255.255:6454
    void sendDMX(uint16_t universe, const uint8_t* data, uint16_t len);

    // Call in loop / task
    void tick();

    bool isRunning() const { return _running; }

private:
    // Art-Net opcodes
    static const uint16_t OP_POLL        = 0x2000;
    static const uint16_t OP_POLL_REPLY  = 0x2100;
    static const uint16_t OP_DMX         = 0x5000;

    static const uint8_t ARTNET_HEADER[8];  // "Art-Net\0"

    void _handlePoll(const IPAddress& from, uint16_t fromPort);
    bool _parseHeader(const uint8_t* buf, uint16_t len, uint16_t& opcode);
    void _buildPollReply(uint8_t* buf, uint16_t& len);

    WiFiUDP  _udp;
    uint16_t _port;
    char     _nodeName[18];
    bool     _running;

    ArtNetFrameCallback _frameCb;

    uint8_t  _rxBuf[600];
    uint8_t  _txBuf[530];

    uint8_t  _sequence;  // rolling ArtDmx sequence counter
    std::vector<uint16_t> _seenOpcodes;
};
