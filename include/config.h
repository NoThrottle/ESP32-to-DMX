#pragma once

// ============================================================
//  ESP32-C6 DMX Node – Hardware & Default Configuration
// ============================================================
//  Board: ESP32-C6-DevKitC-1 (15 pins/side, dual USB-C)
//         Module: ESP32-C6-WROOM-1
//  Two MAX485 transceivers on UART0 and UART1
//
//  Reserved / avoid on DevKitC-1:
//    GPIO8  – onboard WS2812 RGB LED
//    GPIO9  – BOOT strapping pin (active-low; driving LOW at boot → download mode)
//    GPIO12 – USB Serial/JTAG D−  (native USB debug port)
//    GPIO13 – USB Serial/JTAG D+
//    GPIO16 – CH340 UART bridge RX (UART0 default TX — repurposed, see below)
//    GPIO17 – CH340 UART bridge TX (UART0 default RX)
//
//  GPIO4–7 are legacy JTAG pins (MTDI/MTDO/MTCK/MTMS) but are free for
//  general use because the C6 uses USB-JTAG (GPIO12/13), not these pins.
// ============================================================

// --- Universe 1: UART0 (remapped away from CH340 pins 16/17) ---
#define DMX_U1_UART_NUM     0           // UART index (HardwareSerial → Serial0)
#define DMX_U1_TX_PIN       6           // MAX485 DI  (GPIO6 = MTCK — safe)
#define DMX_U1_RX_PIN       7           // MAX485 RO  (GPIO7 = MTMS — safe)
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
