# Changelog – Growatt SHINE WiFi X Firmware

## [1.0.0] – 2026-04-09

### ✨ Features
- ESP8266 NodeMCU Firmware für Growatt MIC 1500TL-X
- Growatt Modbus RTU via RS485 (SoftwareSerial, FC03 read / FC06 write)
- Direkte Leistungsbegrenzung per Modbus FC06 (Register 0x001F / 0x0020)
- WLAN STA + AP Fallback (Growatt-Setup / growatt123)
- Hostname einstellbar, Statische IP konfigurierbar
- LittleFS für Konfigurationsspeicherung (config.json)
- Webinterface vollständig in PROGMEM eingebettet (gzip, kein uploadfs nötig)

### 📡 MQTT
- PubSubClient, Publish + Subscribe
- Home Assistant Auto-Discovery (14 Entities: Sensoren, Switch, Number)
- LWT (Last Will Testament) – Availability online/offline
- Topics: status, relay/set, capW/set, powerRate/set, cmd

### 🌐 Webinterface (Dark Hack Theme)
- 9 Tabs: Energie, Statistik, Umwelt, Anlage, Fehler-Log, Zeit/NTP, Benutzer, Einstellungen, System
- Live PV-Chart (Canvas, 60 Datenpunkte)
- Statistik-Balkendiagramm mit echten Inverter-Daten (Heute/7 Tage/Monat/Jahr)
- Umweltbonus: CO₂, Kohle, Bäume mit Reset-Funktion
- Fehler-Log mit Growatt Fehlercode-Referenz
- NTP Uhrzeit-Synchronisation mit Live-Uhr
- Benutzerverwaltung (Admin/Benutzer/Gast)
- Factory Reset (Bestätigung durch Texteingabe "RESET")

### ⏰ NTP
- Automatische Zeitsynchronisation (3 Server, konfigurierbarer Intervall)
- POSIX-TZ-String für CET/CEST, EET/EEST, WET/WEST
- Zeitstempel in Fehler-Log, MQTT-Timestamps, Systemstart

### 🔧 REST API
- GET  /api/data, /api/config, /api/status
- POST /api/config, /api/relay, /api/cap, /api/powerRate, /api/cmd
- Factory Reset via POST /api/cmd {"cmd":"factory_reset"}
- NTP Force Sync via POST /api/cmd {"cmd":"ntp_sync"}

### Flash
```bash
pio run --target upload
```
Kein `uploadfs` nötig – HTML ist in der Firmware eingebettet.
