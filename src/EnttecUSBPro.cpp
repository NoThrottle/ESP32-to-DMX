#include "EnttecUSBPro.h"
#include "config.h"

EnttecUSBPro::EnttecUSBPro(Stream& usbSerial)
    : _usb(usbSerial), _state(ParseState::WAIT_START),
      _label(0), _dataLen(0), _dataIdx(0)
{}

void EnttecUSBPro::begin() {
    // USBCDC is started by main.cpp via USB.begin() / USB CDC init
    // Nothing extra needed here
}

// ---------------------------------------------------------------------------
//  feedByte() — process one byte; returns true if consumed as ENTTEC data
// ---------------------------------------------------------------------------
bool EnttecUSBPro::feedByte(uint8_t b) {
    if (_state == ParseState::WAIT_START && b != START_BYTE) {
        return false;  // not our byte
    }

    switch (_state) {
    case ParseState::WAIT_START:
        // b == START_BYTE guaranteed here
        _state = ParseState::LABEL;
        break;

    case ParseState::LABEL:
        _label = b;
        _state = ParseState::LEN_LO;
        break;

    case ParseState::LEN_LO:
        _dataLen = b;
        _state   = ParseState::LEN_HI;
        break;

    case ParseState::LEN_HI:
        _dataLen |= ((uint16_t)b << 8);
        _dataIdx  = 0;
        if (_dataLen == 0) {
            _state = ParseState::WAIT_END;
        } else if (_dataLen <= sizeof(_buf)) {
            _state = ParseState::DATA;
        } else {
            _state = ParseState::WAIT_START;  // too large, discard
        }
        break;

    case ParseState::DATA:
        _buf[_dataIdx++] = b;
        if (_dataIdx >= _dataLen) {
            _state = ParseState::WAIT_END;
        }
        break;

    case ParseState::WAIT_END:
        if (b == END_BYTE) {
            _processPacket(_label, _buf, _dataLen);
        }
        _state = ParseState::WAIT_START;
        break;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  tick() — call frequently; parses incoming ENTTEC frames from USB host
// ---------------------------------------------------------------------------
void EnttecUSBPro::tick() {
    while (_usb.available()) {
        feedByte((uint8_t)_usb.read());
    }
}


// ---------------------------------------------------------------------------
//  Process a fully-received ENTTEC packet
// ---------------------------------------------------------------------------
void EnttecUSBPro::_processPacket(uint8_t label, const uint8_t* data, uint16_t len) {
    switch (label) {
    case LABEL_GET_PARAMS:
        _handleGetParams();
        break;

    case LABEL_GET_SERIAL:
        _handleGetSerial();
        break;

    case LABEL_DMX_TX_U1:
        // data[0] = start code, data[1..] = channel values
        if (_frameCb && len >= 1) {
            _frameCb(0, data + 1, len - 1);  // universe 0 = U1
        }
        break;

    case LABEL_DMX_TX_U2:
        // Extended label: universe 2
        if (_frameCb && len >= 1) {
            _frameCb(1, data + 1, len - 1);  // universe 1 = U2
        }
        break;

    case LABEL_RX_DMX_ON_CHG:
        // Host is requesting live DMX RX frames — enable outbound stream
        _dmxRxEnabled = true;
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
//  Send a DMX frame to the host (for RX mode)
//  universe: 0=U1, 1=U2
// ---------------------------------------------------------------------------
void EnttecUSBPro::sendDMXToHost(uint8_t universe, const uint8_t* data, uint16_t len) {
    // Only send if a proper ENTTEC host has requested the RX stream (label 0x08).
    // Without this guard, raw binary frames would corrupt any text terminal session.
    if (!_dmxRxEnabled) return;

    // Payload: start code 0x00 + channel data
    // Max 513 bytes
    uint16_t payloadLen = min((uint16_t)(len + 1), (uint16_t)513);
    uint8_t payload[513];
    payload[0] = 0x00;  // DMX start code
    memcpy(payload + 1, data, payloadLen - 1);

    // Use label 0x05 for both universes (standard ENTTEC behaviour)
    // Some software uses a separate label for U2 — send label 0x05 always
    _sendPacket(LABEL_RECV_DMX, payload, payloadLen);
}

// ---------------------------------------------------------------------------
//  Low-level packet framing: 0x7E label len_lo len_hi data 0xE7
// ---------------------------------------------------------------------------
void EnttecUSBPro::_sendPacket(uint8_t label, const uint8_t* data, uint16_t len) {
    // _usb is a Stream reference — always valid
    _usb.write(START_BYTE);
    _usb.write(label);
    _usb.write((uint8_t)(len & 0xFF));
    _usb.write((uint8_t)(len >> 8));
    if (data && len) _usb.write(data, len);
    _usb.write(END_BYTE);
}

void EnttecUSBPro::_handleGetParams() {
    // Widget Parameters reply (label 0x03):
    //   [0..1] firmware version LSB/MSB
    //   [2]    DMX output break time (in 10.67µs units, 9 = ~96µs)
    //   [3]    DMX output MAB time   (in 10.67µs units, 1 = ~11µs)
    //   [4]    DMX output rate (fps, 0 = max)
    uint8_t params[5] = {
        (uint8_t)(ENTTEC_FIRMWARE_VER & 0xFF),
        (uint8_t)(ENTTEC_FIRMWARE_VER >> 8),
        9,      // break time ~96µs
        1,      // MAB ~11µs
        0       // max rate
    };
    _sendPacket(LABEL_GET_PARAMS, params, sizeof(params));
}

void EnttecUSBPro::_handleGetSerial() {
    // Serial number: 4 bytes LSB first
    uint8_t serial[4] = { 0x39, 0x00, 0x00, 0x00 };
    _sendPacket(LABEL_GET_SERIAL, serial, sizeof(serial));
}
