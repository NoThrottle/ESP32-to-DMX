#pragma once
#include <Arduino.h>
#include <functional>
#include <esp_dmx.h>   // someweisguy/esp_dmx library

// ============================================================
//  DMXPort – Bidirectional DMX512 port backed by esp_dmx
// ============================================================
//  TX: sends a full 512-channel DMX frame at ~44 fps.
//  RX: hardware BREAK detection, fires onReceive callback.
//  PASSTHROUGH: receives and immediately retransmits.
// ============================================================

enum class DMXMode : uint8_t {
    TX          = 0,
    RX          = 1,
    PASSTHROUGH = 2,
};

using DMXReceiveCallback = std::function<void(uint8_t port, const uint8_t* data, uint16_t len)>;

class DMXPort {
public:
    // portIndex: 0 or 1  |  txPin/rxPin/derePin: GPIO numbers
    DMXPort(uint8_t portIndex,
            uint8_t txPin, uint8_t rxPin, uint8_t derePin);

    void    begin(DMXMode mode = DMXMode::TX);
    void    setMode(DMXMode mode);
    DMXMode getMode() const { return _mode; }

    void           setChannel(uint16_t ch, uint8_t value);  // ch: 1–512
    void           setFrame(const uint8_t* data, uint16_t len = 512);
    const uint8_t* getFrame() const { return _txBuf + 1; }  // channel bytes [1..512]

    void onReceive(DMXReceiveCallback cb) { _recvCb = cb; }

    // Call from a dedicated FreeRTOS task (or loop) at >= ~1 kHz
    void tick();

    uint8_t  portIndex()    const { return _portIdx; }
    uint32_t rxFrameCount() const { return _rxFrameCount; }
    uint32_t rxByteCount()  const { return _rxByteCount; }
    const uint32_t& rxFrameCount_ref() const { return _rxFrameCount; }
    const uint32_t& rxByteCount_ref()  const { return _rxByteCount; }

private:
    void _applyMode();

    uint8_t  _portIdx;
    uint8_t  _txPin, _rxPin, _derePin;
    DMXMode  _mode;

    uint8_t  _txBuf[DMX_PACKET_SIZE];  // [0]=start code, [1..512]=channels
    uint8_t  _rxBuf[DMX_PACKET_SIZE];

    uint32_t _rxFrameCount;
    uint32_t _rxByteCount;

    DMXReceiveCallback _recvCb;
};

