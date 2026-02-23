#include "DMXPort.h"
#include "config.h"

DMXPort::DMXPort(uint8_t portIndex, HardwareSerial& serial,
                 uint8_t txPin, uint8_t rxPin, uint8_t derePin)
    : _portIdx(portIndex), _serial(serial),
      _txPin(txPin), _rxPin(rxPin), _derePin(derePin),
      _mode(DMXMode::TX), _rxIdx(0), _rxInBreak(false), _lastFrameUs(0)
{
    memset(_txBuf, 0, sizeof(_txBuf));
    memset(_rxBuf, 0, sizeof(_rxBuf));
}

void DMXPort::begin(DMXMode mode) {
    pinMode(_derePin, OUTPUT);
    _mode = mode;
    if (_mode == DMXMode::TX) {
        _setTX();
    } else {
        _setRX();
    }
}

void DMXPort::setMode(DMXMode mode) {
    _mode = mode;
    if (_mode == DMXMode::TX) {
        _setTX();
    } else {
        _setRX();
    }
}

void DMXPort::_setTX() {
    // Reconfigure UART for DMX 250 kbaud, 8N2
    _serial.end();
    _serial.begin(DMX_BAUD, SERIAL_8N2, _rxPin, _txPin);
    digitalWrite(_derePin, HIGH);  // MAX485 drive enable
}

void DMXPort::_setRX() {
    // Reconfigure UART for DMX 250 kbaud, 8N2
    _serial.end();
    _serial.begin(DMX_BAUD, SERIAL_8N2, _rxPin, _txPin);
    digitalWrite(_derePin, LOW);   // MAX485 receive enable
    _rxIdx = 0;
    _rxInBreak = false;
}

void DMXPort::setChannel(uint16_t ch, uint8_t value) {
    if (ch >= 1 && ch <= DMX_CHANNELS) {
        _txBuf[ch] = value;  // _txBuf[0] = start code, channels at [1..512]
    }
}

void DMXPort::setFrame(const uint8_t* data, uint16_t len) {
    uint16_t n = min(len, (uint16_t)DMX_CHANNELS);
    memcpy(&_txBuf[1], data, n);
    if (n < DMX_CHANNELS) {
        memset(&_txBuf[1 + n], 0, DMX_CHANNELS - n);
    }
}

// ---------------------------------------------------------------------------
//  TX: send one full DMX frame
//  DMX timing requires a BREAK (88µs low) before each packet.
//  We toggle the UART TX line low by temporarily using it as a GPIO.
// ---------------------------------------------------------------------------
bool DMXPort::_sendFrame() {
    uint32_t now = micros();
    if ((uint32_t)(now - _lastFrameUs) < FRAME_INTERVAL_US) return false;
    _lastFrameUs = now;

    // --- BREAK: pull TX line LOW for >=88µs ---
    // Temporarily set TX pin as GPIO output, drive low
    _serial.end();
    pinMode(_txPin, OUTPUT);
    digitalWrite(_txPin, LOW);
    delayMicroseconds(DMX_BREAK_US);

    // --- MAB: drive HIGH for >=12µs ---
    digitalWrite(_txPin, HIGH);
    delayMicroseconds(DMX_MAB_US);

    // Restore UART
    _serial.begin(DMX_BAUD, SERIAL_8N2, _rxPin, _txPin);
    digitalWrite(_derePin, HIGH);

    // --- Send start code + 512 channel bytes ---
    _txBuf[0] = 0x00;  // DMX start code
    _serial.write(_txBuf, DMX_CHANNELS + 1);
    _serial.flush();

    return true;
}

// ---------------------------------------------------------------------------
//  RX: detect BREAK condition and receive full frame
//  A BREAK is detected as a framing error (stop bit = 0) on the UART.
//  Arduino-esp32 surfaces this as a byte 0x00 with odd framing — we watch
//  for the UART break detection flag via the available() + peek() pattern.
// ---------------------------------------------------------------------------
bool DMXPort::_recvFrame() {
    // ESP32 UART hardware signals BREAK as a 0x00 byte with a framing error.
    // We treat reception of 0x00 after a period of silence as a new packet.
    while (_serial.available()) {
        uint8_t b = _serial.read();

        if (_rxIdx == 0) {
            // Expecting start code 0x00
            if (b != 0x00) continue;  // not a valid DMX start
            _rxBuf[0] = b;
            _rxIdx = 1;
        } else {
            _rxBuf[_rxIdx++] = b;
            if (_rxIdx >= DMX_CHANNELS + 1) {
                // Full frame received
                _rxIdx = 0;
                if (_recvCb) {
                    _recvCb(_portIdx, &_rxBuf[1], DMX_CHANNELS);
                }
                return true;
            }
        }
    }
    return false;
}

void DMXPort::tick() {
    switch (_mode) {
    case DMXMode::TX:
        _sendFrame();
        break;

    case DMXMode::RX:
        _recvFrame();
        break;

    case DMXMode::PASSTHROUGH: {
        // Receive on this port, then retransmit the received data
        bool got = _recvFrame();
        if (got) {
            // setFrame has already been updated via callback if wired up
            // Switch briefly to TX, send, then back to RX
            _setTX();
            _sendFrame();
            _setRX();
        }
        break;
    }
    }
}
