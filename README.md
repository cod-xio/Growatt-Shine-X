# Growatt SHINE WiFi X — ESP8266 Firmware

<img width="1707" height="1300" alt="grafik" src="https://github.com/user-attachments/assets/9a5336fa-c07e-4ddb-a29b-e6d9f402276d" />

<img width="1707" height="1300" alt="grafik" src="https://github.com/user-attachments/assets/b1e23bcf-0ecd-4ce5-82dd-d71997cecb63" />



Replacement firmware für den **Growatt SHINE WiFi X** Dongle, kompatibel mit dem **Growatt MIC 1500TL-X** Wechselrichter.

---

## Funktionsumfang

| Feature | Detail |
|---|---|
| WLAN STA + AP Fallback | Auto-AP wenn keine Verbindung |
| Growatt Modbus RTU | RS485 via SoftwareSerial, FC03 Register-Read |
| Einspeise-Begrenzung | 1 – 1500 W, einstellbar per Webinterface + MQTT |
| Relais-Steuerung | D1 (GPIO5), via Web + MQTT |
| MQTT | Publish/Subscribe, PubSubClient |
| Webinterface | Async, Dark-Hack-Theme, Chart, 3 Tabs |
| REST API | /api/data, /api/config, /api/relay, /api/cap, /api/status |
| LittleFS | Config + Webdateien im Flash |

---

## Hardware-Verdrahtung

```
NodeMCU v2          RS485-Modul       Growatt RJ45
─────────────────────────────────────────────────
D6 (GPIO12)  ──▶  RO  (Receive)
D7 (GPIO13)  ──▶  DI  (Transmit)
3.3V         ──▶  VCC
GND          ──▶  GND
                  A   ──────────▶  DATA+ (Pin 4)
                  B   ──────────▶  DATA- (Pin 5)
D1 (GPIO5)   ──▶  Relais IN
```

---

## Voraussetzungen

```
VSCode + PlatformIO IDE Extension
Python 3.x  (für PlatformIO Build-Tools)
```

---

## Kompilieren und Flashen

### 1. Repository öffnen

```bash
code growatt-shine/
```

### 2. Abhängigkeiten installieren (automatisch)

PlatformIO lädt beim ersten Build alle lib_deps automatisch.

### 3. Firmware flashen (ein Schritt — kein uploadfs nötig!)

Das Webinterface ist direkt in der Firmware eingebettet (PROGMEM, gzip-komprimiert).

```bash
pio run --target upload
```

Oder im PlatformIO-Menü: **Upload**

oder Menü: **PlatformIO → Upload**

### 5. Seriellen Monitor öffnen

```bash
pio device monitor --baud 115200
```

---

## Ersteinrichtung

1. Nach dem ersten Flash startet der ESP8266 als Access Point:
   - SSID: `Growatt-Setup`
   - Passwort: `growatt123`
2. Browser: `http://192.168.4.1`
3. Tab **Einstellungen** → WLAN-Daten eingeben → Speichern
4. ESP8266 neu starten → verbindet mit Ihrem Netzwerk

---

## Webinterface

| Tab | Inhalt |
|---|---|
| Energieübersicht | Live-Daten, PV-Chart, Einspeise-Cap-Slider, Relais |
| Einstellungen | WLAN, MQTT, Gerätename |
| System | Firmware, IP, Heap, Uptime, MQTT-Status |

---

## MQTT Topics

| Topic | Richtung | Inhalt |
|---|---|---|
| `growatt/shine/status` | → Broker | JSON mit allen Live-Daten (alle 10 s) |
| `growatt/shine/relay` | ← Broker | `ON` / `OFF` |
| `growatt/shine/capW` | ← Broker | Integer 1–1500 |
| `growatt/shine/cmd` | ← Broker | `save` / `reset` |

Beispiel-JSON (status):
```json
{
  "pvW": 1234.5,
  "acW": 1200.0,
  "acV": 230.1,
  "acHz": 50.00,
  "pv1V": 320.0,
  "pv1A": 3.86,
  "tempC": 42.3,
  "todayKWh": 4.20,
  "totalKWh": 1234.5,
  "status": 1,
  "fault": false,
  "capW": 1500,
  "relay": false,
  "rssi": -62,
  "uptime": 3600
}
```

---

## REST API

```
GET  /api/data       Live-Inverter-Daten
GET  /api/config     Aktuelle Konfiguration (ohne Passwörter)
POST /api/config     Konfiguration aktualisieren (JSON body)
POST /api/relay      {"relay": true}
POST /api/cap        {"capW": 800}
GET  /api/status     System-Health (FW, IP, Heap …)
```

---

## Dateistruktur

```
growatt-shine/
├── platformio.ini          PlatformIO Konfiguration
├── include/
│   ├── config.h            Strukturen, Pins, Konstanten
│   ├── mqtt.h
│   ├── webserver.h
│   └── growatt_modbus.h
├── src/
│   ├── main.cpp            Setup / Loop / WiFi / Relay
│   ├── config.cpp          LittleFS Load/Save
│   ├── mqtt.cpp            PubSubClient Wrapper
│   ├── webserver.cpp       ESPAsyncWebServer REST API
│   └── growatt_modbus.cpp  Modbus RTU Reader
└── data/                   LittleFS Filesystem Image
    ├── index.html          Dark-Hack Webinterface
    └── config.json         Beispiel-Konfiguration
```

---

## Verwendete Libraries

| Library | Version | Zweck |
|---|---|---|
| PubSubClient | ^2.8 | MQTT |
| ArduinoJson | ^7.0.4 | JSON |
| ESPAsyncTCP | ^1.2.2 | Async TCP Basis |
| ESP Async WebServer | ^1.2.3 | Async HTTP Server |
| SoftwareSerial | (built-in) | RS485 UART |
| LittleFS | (built-in) | Flash Filesystem |

---

## Lizenz

MIT — frei verwendbar, keine Garantie.
