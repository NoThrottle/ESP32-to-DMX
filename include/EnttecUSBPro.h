#pragma once
#include <Arduino.h>

// ============================================================
//  EnttecUSBPro – ENTTEC DMX USB Pro protocol over Native USB
// ============================================================
//  Framing:  0x7E [label] [len_lo] [len_hi] [data...] 0xE7
//
//  Supported inbound labels (host → device):
//    0x03  Get Widget Parameters        → returns params reply
//    0x0A  Get Widget Serial Number     → returns fake serial
//    0x06  Output Only Send DMX Packet  → Universe 1 TX frame
//    0x64  Send DMX Packet Universe 2   → Universe 2 TX frame  (extended)
//
//  Outbound labels (device → host):
//    0x05  Receive DMX Packet           → forwarded RX frames
//    0x03  Widget Parameters Reply
//    0x0A  Widget Serial Number Reply
// ============================================================

// Callbacks fired when ENTTEC delivers a DMX frame
using EnttecFrameCallback = std::function<void(uint8_t universe, const uint8_t* data, uint16_t len)>;

class EnttecUSBPro {
public:
    explicit EnttecUSBPro(Stream& usbSerial);

    // Call once in setup() — USB CDC must already be started
    void begin();

    // Register callback for incoming DMX frames from host
    void onFrame(EnttecFrameCallback cb) { _frameCb = cb; }

    // Send a received DMX frame back to the host (RX mode forwarding)
    // universe: 0 = U1, 1 = U2
    void sendDMXToHost(uint8_t universe, const uint8_t* data, uint16_t len);

    // Process a single byte from the host.
    // Returns true if the byte was part of (or started) an ENTTEC frame,
    // false if it was ignored (not ENTTEC data). Use this when sharing the
    // stream with another parser — only forward unclaimed bytes elsewhere.
    bool feedByte(uint8_t b);

    // Call in loop / task — processes all available bytes via feedByte()
    void tick();

private:
    // ENTTEC framing labels
    static const uint8_t START_BYTE      = 0x7E;
    static const uint8_t END_BYTE        = 0xE7;
    static const uint8_t LABEL_GET_PARAMS      = 0x03;
    static const uint8_t LABEL_DMX_TX_U1       = 0x06;
    static const uint8_t LABEL_RECV_DMX        = 0x05;
    static const uint8_t LABEL_GET_SERIAL      = 0x0A;
    static const uint8_t LABEL_DMX_TX_U2       = 0x64;  // extended

    void _processPacket(uint8_t label, const uint8_t* data, uint16_t len);
    void _sendPacket(uint8_t label, const uint8_t* data, uint16_t len);
    void _handleGetParams();
    void _handleGetSerial();

    Stream& _usb;
    EnttecFrameCallback _frameCb;

    // Receive state machine
    enum class ParseState : uint8_t { WAIT_START, LABEL, LEN_LO, LEN_HI, DATA, WAIT_END };
    ParseState _state;
    uint8_t    _label;
    uint16_t   _dataLen;
    uint16_t   _dataIdx;
    uint8_t    _buf[600]; // max ENTTEC DMX payload: 513 bytes + headroom
};
