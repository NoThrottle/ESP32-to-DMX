#include "DMXPort.h"
#include "config.h"

// esp_dmx driver numbers map directly to IDF UART port numbers.
// portIndex 0 -> DMX_NUM_0 (UART0), portIndex 1 -> DMX_NUM_1 (UART1).

DMXPort::DMXPort(uint8_t portIndex,
                 uint8_t txPin, uint8_t rxPin, uint8_t derePin)
    : _portIdx(portIndex),
      _txPin(txPin), _rxPin(rxPin), _derePin(derePin),
      _mode(DMXMode::TX),
      _rxFrameCount(0), _rxByteCount(0)
{
    memset(_txBuf, 0, sizeof(_txBuf));
    memset(_rxBuf, 0, sizeof(_rxBuf));
}

void DMXPort::begin(DMXMode mode) {
    _mode = mode;
    _applyMode();
}

void DMXPort::setMode(DMXMode mode) {
    if (_mode == mode) return;
    dmx_driver_delete((dmx_port_t)_portIdx);
    _mode = mode;
    _applyMode();
}

void DMXPort::_applyMode() {
    dmx_port_t port = (dmx_port_t)_portIdx;

    dmx_driver_delete(port);

    if (_mode == DMXMode::TX) {
        dmx_config_t cfg = DMX_CONFIG_DEFAULT;
        dmx_driver_install(port, &cfg, 0, 0);
        dmx_set_pin(port, _txPin, DMX_PIN_NO_CHANGE, _derePin);
    } else {
        dmx_config_t cfg = DMX_CONFIG_DEFAULT;
        dmx_driver_install(port, &cfg, 0, 0);
        dmx_set_pin(port, DMX_PIN_NO_CHANGE, _rxPin, _derePin);
    }
}

void DMXPort::setChannel(uint16_t ch, uint8_t value) {
    if (ch >= 1 && ch <= DMX_PACKET_SIZE - 1) {
        _txBuf[ch] = value;
    }
}

void DMXPort::setFrame(const uint8_t* data, uint16_t len) {
    uint16_t n = (len < (uint16_t)(DMX_PACKET_SIZE - 1))
                 ? len : (uint16_t)(DMX_PACKET_SIZE - 1);
    memcpy(&_txBuf[1], data, n);
    if (n < DMX_PACKET_SIZE - 1) {
        memset(&_txBuf[1 + n], 0, (DMX_PACKET_SIZE - 1) - n);
    }
}

void DMXPort::tick() {
    dmx_port_t port = (dmx_port_t)_portIdx;

    if (_mode == DMXMode::TX) {
        dmx_write(port, _txBuf, DMX_PACKET_SIZE);
        dmx_send(port);
    }

    if (_mode == DMXMode::RX || _mode == DMXMode::PASSTHROUGH) {
        dmx_packet_t pkt;
        if (dmx_receive(port, &pkt, pdMS_TO_TICKS(25))) {
            if (pkt.err == DMX_OK && pkt.size > 0) {
                dmx_read(port, _rxBuf, pkt.size);
                _rxByteCount  += pkt.size;
                _rxFrameCount++;
                if (_recvCb) {
                    uint16_t chLen = (pkt.size > 1) ? pkt.size - 1 : 0;
                    _recvCb(_portIdx, &_rxBuf[1], chLen);
                }
                if (_mode == DMXMode::PASSTHROUGH) {
                    memcpy(_txBuf, _rxBuf, pkt.size);
                }
            }
        }
    }
}