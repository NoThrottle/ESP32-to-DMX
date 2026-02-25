#include "ArtNetNode.h"
#include <WiFi.h>
#include <USBCDC.h>
#include <string.h>

extern USBCDC ConfigSerial;

// Art-Net ID header (8 bytes: "Art-Net\0")
const uint8_t ArtNetNode::ARTNET_HEADER[8] = {
    'A','r','t','-','N','e','t', 0x00
};

ArtNetNode::ArtNetNode()
    : _port(6454), _running(false), _sequence(0)
{
    strncpy(_nodeName, "ESP32-DMX", sizeof(_nodeName) - 1);
    _nodeName[sizeof(_nodeName) - 1] = '\0';
}

void ArtNetNode::begin(const char* nodeName, uint16_t port) {
    _port = port;
    strncpy(_nodeName, nodeName, sizeof(_nodeName) - 1);
    _nodeName[sizeof(_nodeName) - 1] = '\0';

    if (_udp.begin(_port)) {
        _running = true;
        ConfigSerial.printf("[ArtNet] Listening on UDP port %d\n", _port);
    } else {
        ConfigSerial.println("[ArtNet] Failed to start UDP socket");
    }
}

// ---------------------------------------------------------------------------
//  tick() — parse incoming UDP packets
// ---------------------------------------------------------------------------
void ArtNetNode::tick() {
    if (!_running) return;

    int pktLen = _udp.parsePacket();
    if (pktLen <= 0) return;

    int n = _udp.read(_rxBuf, sizeof(_rxBuf));
    if (n < 10) return;

    uint16_t opcode;
    if (!_parseHeader(_rxBuf, (uint16_t)n, opcode)) return;

    switch (opcode) {
    case OP_POLL:
        _handlePoll(_udp.remoteIP(), _udp.remotePort());
        break;

    case OP_DMX: {
        // ArtDmx packet layout (after 8-byte header):
        //  [8..9]  opcode (LE)
        //  [10]    ProtVer hi
        //  [11]    ProtVer lo
        //  [12]    Sequence
        //  [13]    Physical
        //  [14]    SubUni (low byte of universe)
        //  [15]    Net (bits [14:8] of universe)
        //  [16..17] Length BE
        //  [18+]   DMX data
        if (n < 18) break;
        uint16_t universe = _rxBuf[14] | ((uint16_t)(_rxBuf[15] & 0x7F) << 8);
        uint16_t dataLen  = ((uint16_t)_rxBuf[16] << 8) | _rxBuf[17];
        uint16_t offset   = 18;
        if (offset + dataLen > (uint16_t)n) dataLen = (uint16_t)n - offset;

        if (_frameCb) {
            _frameCb(universe, _rxBuf + offset, dataLen);
        }
        break;
    }

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
//  sendDMX() — broadcast ArtDmx packet (used when relaying DMX input)
// ---------------------------------------------------------------------------
void ArtNetNode::sendDMX(uint16_t universe, const uint8_t* data, uint16_t len) {
    if (!_running) return;
    if (len > 512) len = 512;
    if (len % 2 != 0) len++;  // Art-Net requires even length

    uint16_t pktLen = 18 + len;
    memcpy(_txBuf, ARTNET_HEADER, 8);
    _txBuf[8]  = (uint8_t)(OP_DMX & 0xFF);
    _txBuf[9]  = (uint8_t)(OP_DMX >> 8);
    _txBuf[10] = 0x00;   // ProtVer hi
    _txBuf[11] = 0x0E;   // ProtVer lo (14)
    _txBuf[12] = ++_sequence;
    _txBuf[13] = 0x00;   // Physical
    _txBuf[14] = (uint8_t)(universe & 0xFF);       // Sub-Universe
    _txBuf[15] = (uint8_t)((universe >> 8) & 0x7F); // Net
    _txBuf[16] = (uint8_t)(len >> 8);
    _txBuf[17] = (uint8_t)(len & 0xFF);
    memcpy(_txBuf + 18, data, len);

    _udp.beginPacket(IPAddress(255,255,255,255), _port);
    _udp.write(_txBuf, pktLen);
    _udp.endPacket();
}

bool ArtNetNode::_parseHeader(const uint8_t* buf, uint16_t len, uint16_t& opcode) {
    if (len < 10) return false;
    if (memcmp(buf, ARTNET_HEADER, 8) != 0) return false;
    // Opcode is little-endian at bytes 8–9
    opcode = (uint16_t)buf[8] | ((uint16_t)buf[9] << 8);
    return true;
}

void ArtNetNode::_handlePoll(const IPAddress& from, uint16_t fromPort) {
    uint16_t replyLen = 0;
    _buildPollReply(_txBuf, replyLen);
    _udp.beginPacket(from, fromPort);
    _udp.write(_txBuf, replyLen);
    _udp.endPacket();
}

void ArtNetNode::_buildPollReply(uint8_t* buf, uint16_t& len) {
    memset(buf, 0, 239);
    memcpy(buf, ARTNET_HEADER, 8);
    buf[8]  = (uint8_t)(OP_POLL_REPLY & 0xFF);
    buf[9]  = (uint8_t)(OP_POLL_REPLY >> 8);

    IPAddress ip = WiFi.localIP();
    buf[10] = ip[0]; buf[11] = ip[1];
    buf[12] = ip[2]; buf[13] = ip[3];
    buf[14] = (uint8_t)(_port & 0xFF);
    buf[15] = (uint8_t)(_port >> 8);

    // Firmware version
    buf[16] = 0x01; buf[17] = 0x00;

    // Net/Sub-Uni/Universe = 0
    buf[18] = 0; buf[19] = 0;

    // OEM code (0xFF = unknown/open source)
    buf[20] = 0xFF; buf[21] = 0xFF;

    // Ubea = 0, Status1 = 0
    buf[22] = 0; buf[23] = 0;

    // ESTA code
    buf[24] = 0xFF; buf[25] = 0xFF;

    // Short name (18 bytes), long name (64 bytes)
    strncpy((char*)buf + 26, _nodeName, 17);
    strncpy((char*)buf + 44, "ESP32-C6 DMX Node", 63);

    // NodeReport
    strncpy((char*)buf + 108, "#0001 [0000] OK", 63);

    // NumPorts = 2
    buf[172] = 0x00; buf[173] = 0x02;

    // PortTypes: [0]=DMX output, [1]=DMX output
    buf[174] = 0xC0; buf[175] = 0xC0;
    buf[176] = 0x00; buf[177] = 0x00;

    // GoodInput / GoodOutput
    buf[178] = 0x80; buf[182] = 0x80;

    // Status2 — supports Art-Net 4
    buf[212] = 0x08;

    len = 239;
}
