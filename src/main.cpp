// ============================================================
//  ESP32-C6 Dual-Universe Bidirectional DMX Node
//  main.cpp
// ============================================================
//  Protocols:
//    USB (native) CDC0:  ENTTEC DMX USB Pro
//    USB (native) CDC1:  JSON config + status terminal
//    WiFi:               Art-Net 4 (UDP 6454)
//
//  Universes:
//    U1 → UART0 (GPIO16/17) + MAX485 #1
//    U2 → UART1 (GPIO10/11) + MAX485 #2
// ============================================================

#include <Arduino.h>
#include <USBCDC.h>

// CDC0 (Serial)       = ENTTEC DMX USB Pro binary protocol
// CDC1 (ConfigSerial) = plain-text config terminal (CONFIG / STATUS / etc.)
// Serial0             = UART0 hardware peripheral — used internally by DMX U1
USBCDC ConfigSerial(1);

#include "config.h"
#include "ConfigManager.h"
#include "DMXPort.h"
#include "EnttecUSBPro.h"
#include "ArtNetNode.h"
#include "WiFiManager.h"

// ---------------------------------------------------------------------------
//  Global objects
// ---------------------------------------------------------------------------

// 'Serial'       = Native USB-CDC0 (ENTTEC DMX USB Pro interface)
// 'ConfigSerial' = Native USB-CDC1 (plain-text config terminal)

ConfigManager   cfgMgr;
DMXPort         dmxU1(0, Serial0, DMX_U1_TX_PIN, DMX_U1_RX_PIN, DMX_U1_DERE_PIN);
DMXPort         dmxU2(1, Serial1, DMX_U2_TX_PIN, DMX_U2_RX_PIN, DMX_U2_DERE_PIN);
EnttecUSBPro    enttec(Serial);   // Serial = USB-CDC (ENTTEC interface)
ArtNetNode      artnet;
WiFiManager     wifiMgr;

// ---------------------------------------------------------------------------
//  FreeRTOS task: DMX output at ~44fps on both universes
//  Runs on Core 1 (separate from Arduino loop on Core 0)
// ---------------------------------------------------------------------------
void dmxTask(void* /*param*/) {
    for (;;) {
        dmxU1.tick();
        dmxU2.tick();
        // tick() itself rate-limits to FRAME_INTERVAL_US
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}

// ---------------------------------------------------------------------------
//  Callbacks: ENTTEC → DMX
//  Called when ENTTEC USB host sends a DMX frame
// ---------------------------------------------------------------------------
void onEnttecFrame(uint8_t universe, const uint8_t* data, uint16_t len) {
    if (universe == 0) dmxU1.setFrame(data, len);
    else               dmxU2.setFrame(data, len);
}

// ---------------------------------------------------------------------------
//  Callbacks: Art-Net → DMX
//  Called when an ArtDmx UDP packet arrives
// ---------------------------------------------------------------------------
void onArtNetFrame(uint16_t universe, const uint8_t* data, uint16_t len) {
    const auto& cfg = cfgMgr.config();
    if (universe == cfg.u1Artnet) dmxU1.setFrame(data, len);
    if (universe == cfg.u2Artnet) dmxU2.setFrame(data, len);
}

// ---------------------------------------------------------------------------
//  Callbacks: DMX RX → forward to ENTTEC host + Art-Net
//  Called when a DMX frame is received from an external controller
// ---------------------------------------------------------------------------

// Monitor state — throttle prints to ~4 Hz to keep the terminal readable
static uint8_t  _lastMonFrame[512]  = {};
static uint32_t _lastMonPrintMs     = 0;

void onDMXReceive(uint8_t port, const uint8_t* data, uint16_t len) {
    const auto& cfg = cfgMgr.config();

    // Forward to USB host using ENTTEC label 0x05
    enttec.sendDMXToHost(port, data, len);

    // Forward as Art-Net broadcast
    uint16_t universe = (port == 0) ? cfg.u1Artnet : cfg.u2Artnet;
    artnet.sendDMX(universe, data, len);

    // Cross-port relay: U1(RX) → U2(TX) or U2(RX) → U1(TX)
    if (port == 0 && cfg.u1Mode == DMXMode::RX)   dmxU2.setFrame(data, len);
    if (port == 1 && cfg.u2Mode == DMXMode::RX)   dmxU1.setFrame(data, len);

    // Same-port PASSTHROUGH: update the TX buffer so dmxTask retransmits it
    if (port == 0 && cfg.u1Mode == DMXMode::PASSTHROUGH) dmxU1.setFrame(data, len);
    if (port == 1 && cfg.u2Mode == DMXMode::PASSTHROUGH) dmxU2.setFrame(data, len);

    // -----------------------------------------------------------------------
    //  Live DMX monitor — prints changed non-zero channels at ~4 Hz
    // -----------------------------------------------------------------------
    if (cfgMgr.isDMXMonitorEnabled()) {
        uint32_t now = millis();
        if ((uint32_t)(now - _lastMonPrintMs) >= 250) {
            _lastMonPrintMs = now;

            uint16_t n = (len <= 512) ? len : 512;

            // Check whether anything changed
            bool changed = memcmp(_lastMonFrame, data, n) != 0;
            if (changed) {
                memcpy(_lastMonFrame, data, n);

                // Print a compact line with all non-zero channels
                ConfigSerial.printf("[DMX U%u]", port);
                bool anyNonZero = false;
                for (uint16_t i = 0; i < n; i++) {
                    if (data[i] != 0) {
                        ConfigSerial.printf(" ch%u=%u", i + 1, data[i]);
                        anyNonZero = true;
                    }
                }
                if (!anyNonZero) ConfigSerial.print(" (all zero)");
                ConfigSerial.println();
            }
        }
    }
}

// ---------------------------------------------------------------------------
//  Status LED helper (simple on/off, no NeoPixel lib needed for indication)
// ---------------------------------------------------------------------------
static void ledBlink(uint8_t times, uint32_t ms = 100) {
    for (uint8_t i = 0; i < times; i++) {
        digitalWrite(STATUS_LED_PIN, HIGH);
        delay(ms);
        digitalWrite(STATUS_LED_PIN, LOW);
        delay(ms);
    }
}

// ---------------------------------------------------------------------------
//  setup()
// ---------------------------------------------------------------------------
void setup() {
    // --- Status LED ---
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    // --- Config terminal (USB CDC1) ---
    ConfigSerial.begin(0);   // baud ignored for USB CDC
    cfgMgr.setOutput(ConfigSerial);
    delay(500);
    ConfigSerial.println("\n\n=== ESP32-C6 DMX Node v1.0 ===");
    ConfigSerial.println("Type HELP for commands.");

    // --- Native USB CDC is auto-started with ARDUINO_USB_CDC_ON_BOOT=1 ---
    // Serial (USB-CDC) is available without explicit begin()

    // --- Load config from NVS ---
    cfgMgr.load();
    const DMXNodeConfig& cfg = cfgMgr.config();

    // --- Init DMX ports ---
    dmxU1.begin(cfg.u1Mode);
    dmxU2.begin(cfg.u2Mode);
    dmxU1.onReceive(onDMXReceive);
    dmxU2.onReceive(onDMXReceive);

    ConfigSerial.printf("[DMX] U1: UART0 TX=GPIO%d DE/RE=GPIO%d mode=%s\n",
                  DMX_U1_TX_PIN, DMX_U1_DERE_PIN,
                  cfg.u1Mode == DMXMode::TX ? "TX" :
                  cfg.u1Mode == DMXMode::RX ? "RX" : "PASS");
    ConfigSerial.printf("[DMX] U2: UART1 TX=GPIO%d DE/RE=GPIO%d mode=%s\n",
                  DMX_U2_TX_PIN, DMX_U2_DERE_PIN,
                  cfg.u2Mode == DMXMode::TX ? "TX" :
                  cfg.u2Mode == DMXMode::RX ? "RX" : "PASS");

    // --- ENTTEC USB Pro setup ---
    enttec.begin();
    enttec.onFrame(onEnttecFrame);
    ConfigSerial.println("[ENTTEC] Ready on Native USB CDC0 — select 'ENTTEC USB Pro' in your software");

    // --- WiFi ---
    bool wifiOk = wifiMgr.begin(cfg);
    if (wifiOk) {
        ConfigSerial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
    }

    // --- Art-Net (only useful if WiFi connected) ---
    if (wifiMgr.isConnected()) {
        artnet.onFrame(onArtNetFrame);
        artnet.begin("ESP32-DMX", cfg.artnetPort);
        ConfigSerial.printf("[ArtNet] U1=artnet:%d U2=artnet:%d\n",
                      cfg.u1Artnet, cfg.u2Artnet);
    }

    // --- DMX FreeRTOS task on Core 1 ---
    xTaskCreatePinnedToCore(
        dmxTask,     // function
        "DMX",       // name
        4096,        // stack bytes
        nullptr,     // param
        10,          // priority (high)
        nullptr,     // handle
        1            // core 1
    );

    // --- Boot complete ---
    ledBlink(3, 80);
    ConfigSerial.println("[Boot] Ready.");
    if (!wifiOk && !cfgMgr.hasWiFiCredentials()) {
        ConfigSerial.printf("[Boot] No WiFi configured. Connect to AP '%s' then send:\n", WIFI_AP_SSID);
        ConfigSerial.println("  CONFIG {\"ssid\":\"YourSSID\",\"pass\":\"YourPass\"}");
    }
}

// ---------------------------------------------------------------------------
//  loop() runs on Core 0 — handles USB, Art-Net, and config serial
// ---------------------------------------------------------------------------
void loop() {
    // Process ENTTEC USB frames from host
    enttec.tick();

    // Process Art-Net UDP packets
    artnet.tick();

    // Process config terminal (USB CDC1 = ConfigSerial)
    while (ConfigSerial.available()) {
        cfgMgr.feedChar((char)ConfigSerial.read());
    }

    // Small yield so watchdog stays happy
    vTaskDelay(1 / portTICK_PERIOD_MS);
}
