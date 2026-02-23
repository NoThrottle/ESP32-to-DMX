#pragma once
#include <Arduino.h>
#include <functional>

// ============================================================
//  DMXPort – Bidirectional DMX512 port (one MAX485 + UART)
// ============================================================
//  Each port independently supports TX, RX, or PASSTHROUGH.
//  TX: sends a full 512-channel DMX frame at ~44 fps.
//  RX: detects BREAK, reads 512 bytes, fires onReceive cb.
//  PASSTHROUGH: reads incoming frame, immediately retransmits.
// ============================================================

enum class DMXMode : uint8_t {
    TX          = 0,    // Output: drive fixtures
    RX          = 1,    // Input:  read from existing controller
    PASSTHROUGH = 2,    // Read in, retransmit out
};

// Callback type: called with (portIndex 0|1, frameBuffer ptr, len)
using DMXReceiveCallback = std::function<void(uint8_t port, const uint8_t* data, uint16_t len)>;

class DMXPort {
public:
    // portIndex: 0 or 1 (for identification in callbacks)
    // serial:    HardwareSerial instance (&Serial0 or &Serial1)
    // txPin / rxPin / derePin: GPIO numbers
    DMXPort(uint8_t portIndex,
            HardwareSerial& serial,
            uint8_t txPin, uint8_t rxPin, uint8_t derePin);

    // Call once in setup()
    void begin(DMXMode mode = DMXMode::TX);

    // Change mode at runtime
    void setMode(DMXMode mode);
    DMXMode getMode() const { return _mode; }

    // Set a single channel value (ch: 1–512)
    void setChannel(uint16_t ch, uint8_t value);

    // Set all 512 channels from a buffer (must be at least 512 bytes)
    void setFrame(const uint8_t* data, uint16_t len = 512);

    // Get current TX frame buffer (read-only)
    const uint8_t* getFrame() const { return _txBuf; }

    // Register callback for received frames (RX / PASSTHROUGH mode)
    void onReceive(DMXReceiveCallback cb) { _recvCb = cb; }

    // Must be called from a FreeRTOS task loop (or loop())
    void tick();

    uint8_t portIndex() const { return _portIdx; }

private:
    void _setTX();
    void _setRX();
    bool _sendFrame();      // returns true when frame sent
    bool _recvFrame();      // returns true when full frame received

    uint8_t        _portIdx;
    HardwareSerial& _serial;
    uint8_t        _txPin, _rxPin, _derePin;
    DMXMode        _mode;

    uint8_t  _txBuf[513];       // [0]=start code(0x00), [1..512]=channels
    uint8_t  _rxBuf[513];
    uint16_t _rxIdx;
    bool     _rxInBreak;
    uint32_t _lastFrameUs;

    DMXReceiveCallback _recvCb;

    static const uint32_t FRAME_INTERVAL_US = 1000000 / 44; // ~22727 µs
};
