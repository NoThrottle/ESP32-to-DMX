# ESP32-C6 Dual-Universe Bidirectional DMX Node

A firmware for the **ESP32-C6-DevKitC-1** that turns the board into a professional DMX512 gateway supporting two independent universes, ENTTEC DMX USB Pro over native USB, and Art-Net 4 over WiFi.

---

## Features

| Feature | Detail |
|---|---|
| **2 DMX universes** | UART0 (U1) and UART1 (U2), fully independent |
| **Per-universe mode** | TX (output), RX (input), or PASSTHROUGH |
| **ENTTEC USB Pro** | Native USB-CDC — plug in as a standard DMX widget |
| **Art-Net 4** | UDP port 6454 — ArtDmx receive + transmit + ArtPoll reply |
| **Config terminal** | CH340 UART bridge — JSON commands over serial |
| **Persistent config** | All settings saved to NVS flash (survive reboot) |
| **WiFi fallback AP** | Starts its own hotspot if no credentials configured |

---

## Hardware Wiring

### Universe 1 — UART0 + MAX485 #1

| ESP32-C6 GPIO | MAX485 Pin | Purpose |
|---|---|---|
| GPIO 16 | DI | DMX TX data |
| GPIO 17 | RO | DMX RX data |
| GPIO 4 | DE + /RE (tied) | Direction control (HIGH=TX, LOW=RX) |

### Universe 2 — UART1 + MAX485 #2

| ESP32-C6 GPIO | MAX485 Pin | Purpose |
|---|---|---|
| GPIO 10 | DI | DMX TX data |
| GPIO 11 | RO | DMX RX data |
| GPIO 5 | DE + /RE (tied) | Direction control (HIGH=TX, LOW=RX) |

### Other Connections

| GPIO | Purpose |
|---|---|
| GPIO 8 | Status LED (onboard) |
| USB-C (native) | **CDC0** — ENTTEC DMX USB Pro interface + **CDC1** — config terminal |
| USB-C (CH340) | Firmware flashing only (`pio run --target upload`) |

> **Note:** Tie the MAX485 DE and /RE pins together to a single GPIO. Drive HIGH for transmit, LOW for receive.
>
> When plugged into a PC the native USB-C enumerates **two virtual COM ports** simultaneously — one for DMX software (ENTTEC CDC0) and one for the config terminal (CDC1). Use any serial terminal (PuTTY, `pio device monitor`) on the second port.

---

## Flashing

1. Install [PlatformIO](https://platformio.org/)
2. Clone or download this project
3. Connect the **CH340 USB-C port** (used for flashing only)
4. Run:
   ```
   pio run --target upload
   ```
5. Disconnect the CH340 cable, connect the **native USB-C** port to your PC.
6. Two COM ports appear. Open the **second** one (CDC1) in a terminal at any baud rate:
   ```
   pio device monitor --baud 115200
   ```

---

## Serial Configuration Terminal

Connect the **native USB-C** port to your PC. Two virtual COM ports will appear:

| COM port | Purpose |
|---|---|
| First (lower number) | ENTTEC DMX USB Pro — point your lighting software here |
| Second (higher number) | Config terminal — open with any serial terminal app |

Send plain-text commands terminated with `\n`.

### Commands

| Command | Description |
|---|---|
| `HELP` | List all commands |
| `STATUS` | Print current config as JSON |
| `CONFIG {...}` | Update one or more settings (JSON) |
| `DMXMON ON` | Enable live DMX channel monitor (prints ~4 Hz, not persisted) |
| `DMXMON OFF` | Disable live DMX channel monitor |
| `REBOOT` | Restart the device |
| `RESET` | Factory reset all settings and reboot |

### CONFIG Fields

```json
{
  "ssid":        "YourWiFiSSID",
  "pass":        "YourWiFiPass",
  "ip":          "192.168.1.50",
  "gw":          "192.168.1.1",
  "subnet":      "255.255.255.0",
  "artnet_port": 6454,
  "u1_artnet":   0,
  "u2_artnet":   1,
  "u1_mode":     "TX",
  "u2_mode":     "RX"
}
```

> Valid mode values: `TX` (output), `RX` (input), `PASS` (passthrough — retransmits received frames on the same universe).  
> For cross-port relay, set one universe to `RX` and the other to `TX` — received frames are automatically forwarded.  
> Only include the fields you want to change — all others are left untouched.  
> `ip` / `gw` / `subnet` are optional. Leave blank or omit for DHCP.  
> After any `CONFIG` command the device saves and **reboots automatically**.

### Mode Values

| Value | Behaviour |
|---|---|
| `"TX"` | Output DMX to fixtures |
| `"RX"` | Read DMX from an external controller; forwards to USB host + Art-Net |
| `"PASS"` | Read incoming DMX, immediately retransmit on the same port |

### Example: Set WiFi and universe modes

```
CONFIG {"ssid":"MyNetwork","pass":"secret","u1_mode":"TX","u2_mode":"RX"}
```

### Example: Map Art-Net universes

```
CONFIG {"u1_artnet":0,"u2_artnet":1}
```

---

## ENTTEC DMX USB Pro (USB)

1. Connect the **native USB-C port** to your PC.
2. Your lighting software (QLC+, MA onPC, DMX Workshop, etc.) will detect the device as **"ENTTEC USB Pro"**.
3. Select it as your DMX output/input interface.

| Direction | Label | Universe |
|---|---|---|
| Host → Device (TX) | `0x06` | Universe 1 |
| Host → Device (TX) | `0x64` | Universe 2 (extended) |
| Device → Host (RX) | `0x05` | Both universes |

---

## Art-Net 4 (WiFi)

### Setup

1. Send WiFi credentials via the config terminal (see above).
2. On the next boot the device connects to your network and prints its IP on the CH340 terminal.
3. If connection fails, it starts a fallback AP:
   - **SSID:** `ESP32-DMX`
   - **Password:** `dmx12345`
   - **IP:** `192.168.4.1`  
   Connect to the AP, then send a `CONFIG` command with your real credentials.

### Receiving Art-Net (Art-Net → DMX output)

Configure your software to send ArtDmx to the device's IP (or broadcast `255.255.255.255`) on UDP port **6454**.

- Art-Net universe `u1_artnet` → DMX Universe 1 output
- Art-Net universe `u2_artnet` → DMX Universe 2 output

### Transmitting Art-Net (DMX input → Art-Net)

Set a port to `RX` or `PASS` mode. When a DMX frame is received it is automatically broadcast as ArtDmx on `255.255.255.255:6454`.

### Device Discovery

The node responds to **ArtPoll** packets. It will appear in Art-Net browsers and discovery tools as `ESP32-DMX`.

---

## Default Settings

| Setting | Default |
|---|---|
| Art-Net port | 6454 |
| U1 Art-Net universe | 0 |
| U2 Art-Net universe | 1 |
| U1 mode | TX |
| U2 mode | TX |
| WiFi | None (starts AP) |
| AP SSID | `ESP32-DMX` |
| AP password | `dmx12345` |
| DMX baud | 250000 |
| DMX channels | 512 |
| DMX frame rate | ~44 fps |

---

## Architecture

```
┌─────────────────────────────────────┐
│           ESP32-C6                  │
│                                     │
│  Native USB ──► ENTTEC USB Pro CDC  │
│                      │              │
│  WiFi UDP   ──► Art-Net 4 Node      │
│                      │              │
│               ┌──────┴──────┐       │
│               ▼             ▼       │
│          DMX Universe 1  DMX Universe 2
│          UART0+MAX485    UART1+MAX485
│               │             │       │
│  CH340 UART ──► Config Terminal     │
└─────────────────────────────────────┘
```

Core 0 (Arduino loop): ENTTEC USB parsing, Art-Net UDP, config terminal  
Core 1 (FreeRTOS task): DMX frame output at ~44 fps, DMX input polling
