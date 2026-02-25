#pragma once

// ============================================================
//  ESP32-C6 DMX Node – Hardware & Default Configuration
// ============================================================
//  Board: ESP32-C6-DevKitC-1 (15 pins/side, dual USB-C)
//  Two MAX485 transceivers on UART0 and UART1
// ============================================================

// --- Universe 1: UART0 (also connected to CH340 bridge) ----
#define DMX_U1_UART_NUM     0           // UART index (HardwareSerial)
#define DMX_U1_TX_PIN       16          // MAX485 DI
#define DMX_U1_RX_PIN       17          // MAX485 RO
#define DMX_U1_DERE_PIN     4           // MAX485 DE + /RE (HIGH=TX, LOW=RX)

// --- Universe 2: UART1 -------------------------------------- 
#define DMX_U2_UART_NUM     1           // UART index
#define DMX_U2_TX_PIN       10          // MAX485 DI
#define DMX_U2_RX_PIN       11          // MAX485 RO
#define DMX_U2_DERE_PIN     5           // MAX485 DE + /RE

// --- Status LED (onboard addressable RGB, driven simple) ----
#define STATUS_LED_PIN      8

// --- DMX Protocol Timing (per DMX512-A spec) ----------------
#define DMX_BAUD            250000      // 250 kbaud
#define DMX_BREAK_US        88          // BREAK duration µs (min 88)
#define DMX_MAB_US          12          // Mark After Break µs (min 8)
#define DMX_CHANNELS        512         // Channels per universe
#define DMX_FPS             44          // Target refresh rate

// --- Art-Net Defaults ---------------------------------------
#define ARTNET_PORT         6454        // Standard Art-Net UDP port
#define ARTNET_U1_UNIVERSE  0           // Art-Net universe → DMX U1
#define ARTNET_U2_UNIVERSE  1           // Art-Net universe → DMX U2

// --- WiFi AP fallback (used when no STA credentials stored) -
#define WIFI_AP_SSID        "ESP32-DMX"
#define WIFI_AP_PASS        "dmx12345"
#define WIFI_AP_IP          "192.168.4.1"

// --- Config terminal (USB CDC1 on native USB-C) -------------
// Baud rate is ignored for USB CDC but kept for reference.
#define CONFIG_SERIAL_BAUD  115200

// --- NVS namespace ------------------------------------------
#define NVS_NAMESPACE       "dmxnode"

// --- ENTTEC USB Pro fake serial number ----------------------
#define ENTTEC_SERIAL_HI    0x00
#define ENTTEC_SERIAL_LO    0x01
#define ENTTEC_FIRMWARE_VER 0x0144      // Reported firmware version (v1.44)
