// ============================================================
//  main.cpp  –  Growatt SHINE WiFi X  ESP8266 Firmware
//  Target: NodeMCU v2 / ESP8266
//  Framework: Arduino (PlatformIO)
//  No OTA. No Arduino IDE.
// ============================================================
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

#include "config.h"
#include "webserver.h"
#include "mqtt.h"
#include "growatt_modbus.h"
#include "ntp.h"

// ── Externs defined in config.cpp ────────────────────────────
extern AppConfig    gConfig;
extern InverterData gData;
extern bool         configLoad();
extern bool         configSave();

// ── WiFi state ────────────────────────────────────────────────
static bool apMode = false;

// ── Timers ────────────────────────────────────────────────────
static unsigned long tModbus = 0;
static unsigned long tMqtt   = 0;
static unsigned long tBlink  = 0;
static bool          ledState = false;

// ─────────────────────────────────────────────────────────────
//  Relay control (shared symbol used by webserver + mqtt)
// ─────────────────────────────────────────────────────────────
void relaySet(bool on) {
    gConfig.relayOn = on;
    digitalWrite(PIN_RELAY, on ? HIGH : LOW);
    Serial.printf("[RELAY] %s\n", on ? "ON" : "OFF");
    configSave();
}

// ─────────────────────────────────────────────────────────────
//  WiFi – try STA, fallback to AP
// ─────────────────────────────────────────────────────────────
static void wifiBegin() {
    if (strlen(gConfig.wifiSSID) == 0) goto fallback;

    Serial.printf("[WiFi] Connecting to '%s' (hostname: %s)...\n",
                  gConfig.wifiSSID, gConfig.hostname);
    WiFi.mode(WIFI_STA);
    WiFi.hostname(gConfig.hostname);   // mDNS hostname

    // Static IP
    if (gConfig.staticIp && strlen(gConfig.staticIpAddr) > 0) {
        IPAddress ip, mask, gw, dns;
        if (ip.fromString(gConfig.staticIpAddr) &&
            mask.fromString(gConfig.subnet)) {
            gw.fromString(gConfig.gateway);
            dns.fromString(gConfig.dnsServer);
            WiFi.config(ip, gw, mask, dns);
            Serial.printf("[WiFi] Static IP: %s / %s  GW: %s\n",
                          gConfig.staticIpAddr, gConfig.subnet, gConfig.gateway);
        } else {
            Serial.println("[WiFi] Static IP parse error – using DHCP");
        }
    }

    WiFi.begin(gConfig.wifiSSID, gConfig.wifiPass);

    {
        uint8_t tries = 0;
        while (WiFi.status() != WL_CONNECTED && tries++ < 30) {
            delay(500);
            Serial.print('.');
        }
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Connected  IP=%s  MAC=%s\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.macAddress().c_str());
        return;
    }

fallback:
    Serial.println("\n[WiFi] STA failed – starting AP");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("[WiFi] AP '%s'  IP=%s\n", AP_SSID, AP_IP);
    apMode = true;
}

// ─────────────────────────────────────────────────────────────
//  setup()
// ─────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n\n=== Growatt SHINE WiFi X  FW %s ===\n", FW_VERSION);

    // GPIO
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, LOW);
    pinMode(PIN_LED, OUTPUT);

    // Filesystem (config.json only — web UI is embedded in firmware)
    if (!LittleFS.begin()) {
        Serial.println("[FS] Formatting LittleFS...");
        LittleFS.format();
        LittleFS.begin();
    }
    Serial.println("[FS] Ready (config storage)");

    // Config
    if (configLoad()) Serial.println("[CFG] Loaded from flash");
    else              Serial.println("[CFG] Using defaults");

    // Restore relay state
    digitalWrite(PIN_RELAY, gConfig.relayOn ? HIGH : LOW);

    // WiFi
    wifiBegin();

    // Sub-systems
    modbusSetup();
    webServerSetup();
    if (!apMode) {
        mqttSetup();
        ntpSetup();   // start NTP sync (requires WiFi)
    }

    Serial.println("[MAIN] Init complete");
}

// ─────────────────────────────────────────────────────────────
//  loop()
// ─────────────────────────────────────────────────────────────
void loop() {
    unsigned long now = millis();

    // ── Status LED blink ──────────────────────────────────────
    uint16_t blinkMs = apMode ? 200 : (gData.faultActive ? 100 : 1000);
    if (now - tBlink >= blinkMs) {
        tBlink   = now;
        ledState = !ledState;
        digitalWrite(PIN_LED, ledState ? LOW : HIGH);   // active LOW
    }

    // ── Poll inverter ─────────────────────────────────────────
    if (now - tModbus >= INTERVAL_MODBUS) {
        tModbus = now;
        bool ok = modbusPoll();
        if (!ok) Serial.println("[RS485] Poll timeout");
    }

    // ── MQTT ──────────────────────────────────────────────────
    if (!apMode) {
        mqttLoop();
        ntpLoop();
        if (now - tMqtt >= INTERVAL_MQTT) {
            tMqtt = now;
            mqttPublishAll();
        }
    }

    // ── Reconnect WiFi if STA drops ───────────────────────────
    if (!apMode && WiFi.status() != WL_CONNECTED) {
        static unsigned long tWifi = 0;
        if (now - tWifi > 15000) {
            tWifi = now;
            Serial.println("[WiFi] Reconnecting...");
            WiFi.reconnect();
        }
    }

    yield();
}
