// ============================================================
//  ESP32-C6 Dual-Universe Bidirectional DMX Node
//  main.cpp
// ============================================================
//  Protocols:
//    USB (native) Serial:  ENTTEC USB Pro wire protocol (DMX) +
//                          JSON config terminal (HWCDC, shared port)
//    WiFi:                 Art-Net 4 (UDP 6454)
//
//  ENTTEC note: bytes starting with 0x7E are routed to the ENTTEC
//  parser; all other bytes go to the ASCII config terminal. Use any
//  software that supports manual COM port selection (e.g. QLC+).
//
//  Universes:
//    U1 → UART0    (GPIO6/7)   + MAX485 #1  (remapped away from CH340 pins 16/17)
//    U2 → UART1    (GPIO10/11) + MAX485 #2
//
//  Note: ESP32-C6 has no USB-OTG hardware (USBCDC / TinyUSB unavailable).
//  The native USB is a single-channel USB Serial/JTAG (HWCDC = Serial).
// ============================================================

#include <Arduino.h>

// On ESP32-C6 'Serial' is the native USB Serial/JTAG (HWCDC). It is the
// only USB CDC channel available. We use it as the config terminal.
Stream& ConfigSerial = Serial;

#include "config.h"
#include "ConfigManager.h"
#include "DMXPort.h"
#include "ArtNetNode.h"
#include "WiFiManager.h"
#include "EnttecUSBPro.h"
#include <WebServer.h>

// ---------------------------------------------------------------------------
//  Global objects
// ---------------------------------------------------------------------------

ConfigManager   cfgMgr;
DMXPort         dmxU1(0, DMX_U1_TX_PIN, DMX_U1_RX_PIN, DMX_U1_DERE_PIN);
DMXPort         dmxU2(1, DMX_U2_TX_PIN, DMX_U2_RX_PIN, DMX_U2_DERE_PIN);
EnttecUSBPro    enttec(Serial);  // ENTTEC wire protocol over native USB HWCDC

ArtNetNode      artnet;
WiFiManager     wifiMgr;
WebServer       webServer(80);

// ---------------------------------------------------------------------------
//  FreeRTOS tasks
//
//  TX task: calls tick() for the TX port. dmx_send() is non-blocking;
//           esp_dmx manages BREAK + MAB timing in hardware, so this task
//           runs as fast as the scheduler allows and esp_dmx rate-limits
//           to the correct 44 fps internally.
//
//  RX task: calls tick() for the RX port. dmx_receive() blocks up to 25 ms
//           waiting for a packet — this is intentional; it avoids busy-
//           polling while still waking immediately when data arrives.
//           Runs at priority 5 (above loop at 1, below system tasks).
//
//  Both tasks are separate so a blocking dmx_receive() never delays TX.
// ---------------------------------------------------------------------------
void dmxTxTask(void* /*param*/) {
    for (;;) {
        dmxU1.tick();
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}

void dmxRxTask(void* /*param*/) {
    for (;;) {
        dmxU2.tick();
        // Fallback delay in case the port is in TX mode or error state,
        // to prevent busy loops and WDT resets.
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}

// ---------------------------------------------------------------------------
//  Callbacks: Art-Net → DMX
//  Called when an ArtDmx UDP packet arrives
// ---------------------------------------------------------------------------
static uint32_t _lastArtPrintMs[2] = {};

void onArtNetFrame(uint16_t universe, const uint8_t* data, uint16_t len) {
    const auto& cfg = cfgMgr.config();
    if (universe == cfg.u1Artnet) dmxU1.setFrame(data, len);
    if (universe == cfg.u2Artnet) dmxU2.setFrame(data, len);

    if (cfgMgr.isDMXMonitorEnabled()) {
        uint8_t idx = (universe == cfg.u2Artnet) ? 1 : 0;
        uint32_t now = millis();
        if ((uint32_t)(now - _lastArtPrintMs[idx]) >= 250) {
            _lastArtPrintMs[idx] = now;
            uint16_t n = (len <= 512) ? len : 512;
            ConfigSerial.printf("[ArtNet U%u]", idx + 1);
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

// ---------------------------------------------------------------------------
//  Callback: ENTTEC USB host → DMX
// ---------------------------------------------------------------------------
void onEnttecFrame(uint8_t universe, const uint8_t* data, uint16_t len) {
    if (universe == 0) dmxU1.setFrame(data, len);
    if (universe == 1) dmxU2.setFrame(data, len);
}

// ---------------------------------------------------------------------------
//  Callbacks: DMX RX → forward to ENTTEC host + Art-Net
//  Called when a DMX frame is received from an external controller
// ---------------------------------------------------------------------------

// Monitor state — throttle prints to ~4 Hz per port to keep the terminal readable
static uint32_t _lastMonPrintMs[2] = {};

void onDMXReceive(uint8_t port, const uint8_t* data, uint16_t len) {
    const auto& cfg = cfgMgr.config();

    // Forward as Art-Net broadcast
    uint16_t universe = (port == 0) ? cfg.u1Artnet : cfg.u2Artnet;
    artnet.sendDMX(universe, data, len);

    // Forward to any ENTTEC USB host listening on native USB
    enttec.sendDMXToHost(port, data, len);

    // PASSTHROUGH: relay received data to the other port's TX buffer.
    // RX mode does NOT relay — it only reports to Art-Net/ENTTEC above.
    if (port == 0 && cfg.u1Mode == DMXMode::PASSTHROUGH) dmxU2.setFrame(data, len);
    if (port == 1 && cfg.u2Mode == DMXMode::PASSTHROUGH) dmxU1.setFrame(data, len);

    // -----------------------------------------------------------------------
    //  Live DMX monitor — prints all non-zero channels at ~4 Hz per port.
    //  Prints "(all zero)" when all channels are 0 so you can confirm RX
    //  is working even when U1 is sending a blank frame.
    // -----------------------------------------------------------------------
    if (cfgMgr.isDMXMonitorEnabled()) {
        uint8_t  p   = (port < 2) ? port : 1;
        uint32_t now = millis();
        if ((uint32_t)(now - _lastMonPrintMs[p]) >= 250) {
            _lastMonPrintMs[p] = now;

            uint16_t n = (len <= 512) ? len : 512;

            // Always print — don't suppress all-zero or unchanged frames.
            // The 250 ms throttle is enough to keep the terminal readable.
            ConfigSerial.printf("[DMX U%u]", port + 1);
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

// ---------------------------------------------------------------------------
//  Status LED helper (uses built-in neopixelWrite for addressable RGB)
// ---------------------------------------------------------------------------
static void ledBlink(uint8_t times, uint32_t ms = 100) {
    for (uint8_t i = 0; i < times; i++) {
        // neopixelWrite(pin, red, green, blue) - max value is 255. 50 is a safe brightness.
        neopixelWrite(STATUS_LED_PIN, 0, 50, 0); // Green
        delay(ms);
        neopixelWrite(STATUS_LED_PIN, 0, 0, 0);  // Off
        delay(ms);
    }
}

// ---------------------------------------------------------------------------
//  WebServer handlers & HTML UI
// ---------------------------------------------------------------------------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 DMX Config</title>
  <style>
    body { font-family: Roboto, sans-serif; background: #121212; color: #ffffff; padding: 20px; max-width: 600px; margin: 0 auto; }
    h2 { border-bottom: 1px solid #333; padding-bottom: 10px; }
    .card { background: #1e1e1e; padding: 20px; border-radius: 8px; margin-bottom: 20px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
    label { display: block; margin-top: 10px; font-weight: bold; color: #aaa; font-size: 0.9em; }
    input, select { width: 100%; padding: 10px; margin-top: 5px; border-radius: 4px; border: 1px solid #444; background: #2c2c2c; color: #fff; box-sizing: border-box; }
    button { background: #4caf50; color: white; border: none; padding: 12px 20px; border-radius: 4px; cursor: pointer; font-size: 16px; margin-top: 15px; width: 100%; transition: background 0.3s; }
    button:hover { background: #45a049; }
    .status { margin-top: 10px; color: #4caf50; font-size: 0.9em; text-align: center; min-height: 20px; }
    .row { display: flex; gap: 15px; }
    .col { flex: 1; }
  </style>
</head>
<body>
  <h2>ESP32 DMX Configuration</h2>
  <div class="card">
    <div class="row">
      <div class="col">
        <label>U1 Mode</label>
        <select id="u1_mode"><option value="TX">TX</option><option value="RX">RX</option><option value="PASS">PASS</option></select>
      </div>
      <div class="col">
        <label>U1 Art-Net Universe</label>
        <input type="number" id="u1_artnet">
      </div>
    </div>
    <div class="row">
      <div class="col">
        <label>U2 Mode</label>
        <select id="u2_mode"><option value="TX">TX</option><option value="RX">RX</option><option value="PASS">PASS</option></select>
      </div>
      <div class="col">
        <label>U2 Art-Net Universe</label>
        <input type="number" id="u2_artnet">
      </div>
    </div>
    <label>Art-Net Port</label>
    <input type="number" id="artnet_port">
  </div>
  
  <div class="card">
    <label>WiFi SSID</label>
    <input type="text" id="ssid">
    <label>WiFi Password</label>
    <input type="password" id="pass" placeholder="(unchanged if blank)">
    <label>Static IP (Leave blank for DHCP)</label>
    <input type="text" id="ip" placeholder="e.g. 192.168.1.50">
    <button onclick="saveConfig()">Save Configuration</button>
    <div id="status" class="status"></div>
  </div>

  <script>
    function loadConfig() {
      fetch('/api/status').then(r => r.json()).then(data => {
        ['ssid', 'ip', 'artnet_port', 'u1_artnet', 'u2_artnet', 'u1_mode', 'u2_mode'].forEach(k => {
          if (document.getElementById(k)) document.getElementById(k).value = data[k] || '';
        });
      });
    }
    function saveConfig() {
      const data = {};
      ['ssid', 'pass', 'ip', 'artnet_port', 'u1_artnet', 'u2_artnet', 'u1_mode', 'u2_mode'].forEach(k => {
        const val = document.getElementById(k).value;
        if (val) data[k] = val;
      });
      data.artnet_port = parseInt(data.artnet_port);
      data.u1_artnet = parseInt(data.u1_artnet);
      data.u2_artnet = parseInt(data.u2_artnet);
      
      document.getElementById('status').innerText = 'Saving...';
      fetch('/api/config', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(data)
      }).then(r => r.json()).then(res => {
        document.getElementById('status').innerText = res.status || res.error || 'Done. Device applying changes...';
        setTimeout(() => document.getElementById('status').innerText = '', 3000);
      });
    }
    window.onload = loadConfig;
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
    webServer.send(200, "text/html", INDEX_HTML);
}

void handleApiStatus() {
    webServer.send(200, "application/json", cfgMgr.getStatusJSON());
}

void handleApiConfig() {
    if (webServer.hasArg("plain")) {
        String json = webServer.arg("plain");
        String responseMsg;
        if (cfgMgr.applyConfigJSON(json.c_str(), responseMsg)) {
            webServer.send(200, "application/json", responseMsg);
        } else {
            webServer.send(400, "application/json", responseMsg);
        }
    } else {
        webServer.send(400, "application/json", "{\"error\":\"No payload\"}");
    }
}

// ---------------------------------------------------------------------------
//  setup()
// ---------------------------------------------------------------------------
void setup() {
    // --- Status LED ---
    neopixelWrite(STATUS_LED_PIN, 0, 0, 0); // Ensure NeoPixel is off at boot

    // --- Config terminal (native USB Serial/JTAG) ---
    Serial.begin(115200);
    cfgMgr.setOutput(ConfigSerial);
    delay(500);
    ConfigSerial.println("\n\n=== ESP32-C6 DMX Node v1.0 ===");
    ConfigSerial.println("Type HELP for commands.");

    // --- Native USB CDC is auto-started with ARDUINO_USB_CDC_ON_BOOT=1 ---
    // Serial (HWCDC) is available without explicit begin() on ESP32-C6

    // --- Load config from NVS ---
    cfgMgr.load();
    const DMXNodeConfig& cfg = cfgMgr.config();

    // --- Init DMX ports ---
    dmxU1.begin(cfg.u1Mode);
    dmxU2.begin(cfg.u2Mode);
    dmxU1.onReceive(onDMXReceive);
    dmxU2.onReceive(onDMXReceive);

    // Supply live RX frame+byte count pointers so STATUS includes them
    cfgMgr.setRxCounters(&dmxU1.rxFrameCount_ref(), &dmxU2.rxFrameCount_ref(),
                         &dmxU1.rxByteCount_ref(),  &dmxU2.rxByteCount_ref());

    ConfigSerial.printf("[DMX] U1: UART0 TX=GPIO%d DE/RE=GPIO%d mode=%s\n",
                  DMX_U1_TX_PIN, DMX_U1_DERE_PIN,
                  cfg.u1Mode == DMXMode::TX ? "TX" :
                  cfg.u1Mode == DMXMode::RX ? "RX" : "PASS");
    ConfigSerial.printf("[DMX] U2: UART1 TX=GPIO%d DE/RE=GPIO%d mode=%s\n",
                  DMX_U2_TX_PIN, DMX_U2_DERE_PIN,
                  cfg.u2Mode == DMXMode::TX ? "TX" :
                  cfg.u2Mode == DMXMode::RX ? "RX" : "PASS");

    // --- ENTTEC USB Pro wire protocol over native USB HWCDC ---
    enttec.begin();
    enttec.onFrame(onEnttecFrame);
    ConfigSerial.println("[ENTTEC] Wire protocol active on native USB port.");

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

    // --- WebServer ---
    webServer.on("/", HTTP_GET, handleRoot);
    webServer.on("/api/status", HTTP_GET, handleApiStatus);
    webServer.on("/api/config", HTTP_POST, handleApiConfig);
    webServer.begin();
    ConfigSerial.println("[WebServer] Started on port 80");

    // --- DMX FreeRTOS tasks ---
    // TX at priority 5 (non-blocking, 1 ms yield keeps loop() responsive)
    // RX at priority 5 (dmx_receive blocks 25 ms naturally, yielding to loop())
    xTaskCreate(dmxTxTask, "DMX_TX", 4096, nullptr, 5, nullptr);
    xTaskCreate(dmxRxTask, "DMX_RX", 4096, nullptr, 5, nullptr);

    // --- Boot complete ---
    ledBlink(3, 80);
    ConfigSerial.println("[Boot] Ready.");
    if (!wifiOk && !cfgMgr.hasWiFiCredentials()) {
        ConfigSerial.printf("[Boot] No WiFi configured. Connect to AP '%s' then send:\n", WIFI_AP_SSID);
        ConfigSerial.println("  CONFIG {\"ssid\":\"YourSSID\",\"pass\":\"YourPass\"}");
    }
}

// ---------------------------------------------------------------------------
//  loop() runs on Core 0 — handles Art-Net and config serial
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
void loop() {
    // Process incoming Web requests
    webServer.handleClient();

    // Process Art-Net UDP packets
    artnet.tick();

    // Process incoming serial bytes — route to ENTTEC parser or config terminal
    while (Serial.available()) {
        uint8_t b = (uint8_t)Serial.read();
        if (!enttec.feedByte(b)) {
            cfgMgr.feedChar((char)b);
        }
    }

    // Apply config changes live (no reboot needed)
    if (cfgMgr.wasUpdated()) {
        const DMXNodeConfig& cfg = cfgMgr.config();

        // Re-init DMX ports with potentially new modes
        dmxU1.begin(cfg.u1Mode);
        dmxU2.begin(cfg.u2Mode);

        // Reconnect WiFi with new credentials
        bool wifiOk = wifiMgr.reconnect(cfg);

        // Restart Art-Net if WiFi is up
        if (wifiOk) {
            artnet.onFrame(onArtNetFrame);
            artnet.begin("ESP32-DMX", cfg.artnetPort);
            ConfigSerial.printf("[ArtNet] U1=artnet:%d U2=artnet:%d\n",
                          cfg.u1Artnet, cfg.u2Artnet);
        }

        ConfigSerial.println("{\"status\":\"applied\"}");
    }

    // Small yield so watchdog stays happy
    vTaskDelay(1 / portTICK_PERIOD_MS);
}
