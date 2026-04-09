// ============================================================
//  mqtt.cpp  –  PubSubClient wrapper  v1.1
//
//  Standard topics:
//    <base>/status           JSON all live data      (publish, retained)
//    <base>/availability     "online"/"offline"      (LWT, retained)
//    <base>/relay/state      "ON" / "OFF"            (publish, retained)
//    <base>/relay/set        "ON" / "OFF"            (subscribe)
//    <base>/capW/state       integer W               (publish, retained)
//    <base>/capW/set         integer 1-1500          (subscribe)
//    <base>/powerRate/state  integer %               (publish, retained)
//    <base>/powerRate/set    integer 0-100           (subscribe)
//    <base>/cmd              "reset","save","ha_discovery" (subscribe)
//
//  Home Assistant Auto-Discovery (published on each connect):
//    <haPrefix>/sensor/<devId>/<sensorId>/config
//    <haPrefix>/switch/<devId>/relay/config
//    <haPrefix>/number/<devId>/capW/config
//    <haPrefix>/number/<devId>/powerRate/config
// ============================================================
#include "config.h"
#include "mqtt.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

extern AppConfig    gConfig;
extern InverterData gData;
extern bool         configSave();
extern void         relaySet(bool on);
extern bool         modbusSetPowerRate(uint8_t pct);

static WiFiClient   wifiClient;
static PubSubClient mqttClient(wifiClient);
static unsigned long lastReconnect = 0;
static bool         haPublished    = false;

// ── Topic helpers ─────────────────────────────────────────────
static String T(const char* sub) {
    return String(gConfig.mqttTopicBase) + "/" + sub;
}
static String HA(const char* domain, const char* objId) {
    String id = "growatt_" + String(ESP.getChipId(), HEX);
    return String(gConfig.haPrefix) + "/" + domain + "/" + id + "/" + objId + "/config";
}
static String devId() {
    return "growatt_" + String(ESP.getChipId(), HEX);
}

// ── Build shared device block ─────────────────────────────────
static void addDevice(JsonDocument& doc) {
    JsonObject dev = doc["device"].to<JsonObject>();
    JsonArray  ids = dev["identifiers"].to<JsonArray>();
    ids.add(devId());
    dev["name"]         = gConfig.deviceName;
    dev["model"]        = "Growatt MIC 1500TL-X / SHINE WiFi X";
    dev["manufacturer"] = "Growatt";
    dev["sw_version"]   = FW_VERSION;
    // Availability
    doc["availability_topic"] = T("availability");
}

// ── Publish one HA sensor entity ─────────────────────────────
static void haPublishSensor(const char* objId, const char* name,
                             const char* stateTopic, const char* valueTpl,
                             const char* unit,       const char* devClass,
                             const char* icon = nullptr,
                             const char* stateClass = "measurement") {
    JsonDocument doc;
    doc["name"]           = name;
    doc["unique_id"]      = devId() + "_" + objId;
    doc["state_topic"]    = stateTopic;
    doc["value_template"] = valueTpl;
    if (unit)       doc["unit_of_measurement"] = unit;
    if (devClass)   doc["device_class"]        = devClass;
    if (icon)       doc["icon"]                = icon;
    if (stateClass) doc["state_class"]         = stateClass;
    doc["expire_after"]   = 120;
    addDevice(doc);

    char buf[800];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    mqttClient.beginPublish(HA("sensor", objId).c_str(), n, true);
    mqttClient.print(buf);
    mqttClient.endPublish();
    yield();
}

// ── Publish HA switch (relay) ─────────────────────────────────
static void haPublishSwitch() {
    JsonDocument doc;
    doc["name"]          = String(gConfig.deviceName) + " Relay";
    doc["unique_id"]     = devId() + "_relay";
    doc["state_topic"]   = T("relay/state");
    doc["command_topic"] = T("relay/set");
    doc["payload_on"]    = "ON";
    doc["payload_off"]   = "OFF";
    doc["icon"]          = "mdi:electric-switch";
    addDevice(doc);

    char buf[512];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    mqttClient.beginPublish(HA("switch", "relay").c_str(), n, true);
    mqttClient.print(buf);
    mqttClient.endPublish();
    yield();
}

// ── Publish HA number entity ──────────────────────────────────
static void haPublishNumber(const char* objId, const char* name,
                             const char* cmdTopic, const char* stateTopic,
                             float minV, float maxV, float step,
                             const char* unit, const char* icon) {
    JsonDocument doc;
    doc["name"]          = name;
    doc["unique_id"]     = devId() + "_" + objId;
    doc["command_topic"] = cmdTopic;
    doc["state_topic"]   = stateTopic;
    doc["min"]           = minV;
    doc["max"]           = maxV;
    doc["step"]          = step;
    if (unit) doc["unit_of_measurement"] = unit;
    if (icon) doc["icon"]                = icon;
    doc["mode"]          = "slider";
    addDevice(doc);

    char buf[512];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    mqttClient.beginPublish(HA("number", objId).c_str(), n, true);
    mqttClient.print(buf);
    mqttClient.endPublish();
    yield();
}

// ── Publish all HA discovery configs ─────────────────────────
static void publishHADiscovery() {
    if (!gConfig.haDiscovery) return;

    // Sensors (all reading from the /status JSON payload)
    haPublishSensor("pv_power",    "PV Leistung",
        T("status").c_str(), "{{ value_json.pvW | round(1) }}",
        "W",    "power",         "mdi:solar-power");

    haPublishSensor("ac_power",    "AC Ausgang",
        T("status").c_str(), "{{ value_json.acW | round(1) }}",
        "W",    "power",         "mdi:transmission-tower");

    haPublishSensor("ac_voltage",  "AC Spannung",
        T("status").c_str(), "{{ value_json.acV | round(1) }}",
        "V",    "voltage",       "mdi:sine-wave");

    haPublishSensor("ac_freq",     "Netzfrequenz",
        T("status").c_str(), "{{ value_json.acHz | round(2) }}",
        "Hz",   "frequency",     "mdi:waveform");

    haPublishSensor("pv1_voltage", "PV1 Spannung",
        T("status").c_str(), "{{ value_json.pv1V | round(1) }}",
        "V",    "voltage",       "mdi:solar-panel");

    haPublishSensor("pv1_current", "PV1 Strom",
        T("status").c_str(), "{{ value_json.pv1A | round(2) }}",
        "A",    "current",       "mdi:current-dc");

    haPublishSensor("temperature", "Inverter Temperatur",
        T("status").c_str(), "{{ value_json.tempC | round(1) }}",
        "°C",   "temperature",   "mdi:thermometer");

    haPublishSensor("energy_today","Ertrag Heute",
        T("status").c_str(), "{{ value_json.todayKWh | round(2) }}",
        "kWh",  "energy",        "mdi:weather-sunny",  "total_increasing");

    haPublishSensor("energy_total","Gesamtertrag",
        T("status").c_str(), "{{ value_json.totalKWh | round(1) }}",
        "kWh",  "energy",        "mdi:counter",        "total_increasing");

    haPublishSensor("rssi",        "WLAN Signalstärke",
        T("status").c_str(), "{{ value_json.rssi }}",
        "dBm",  "signal_strength","mdi:wifi");

    haPublishSensor("uptime",      "Uptime",
        T("status").c_str(), "{{ value_json.uptime }}",
        "s",    nullptr,         "mdi:clock-outline");

    haPublishSensor("power_rate",  "Leistungsbegrenzung",
        T("status").c_str(), "{{ value_json.pwrPct }}",
        "%",    nullptr,         "mdi:speedometer");

    // Switch
    haPublishSwitch();

    // Numbers (controllable from HA UI)
    haPublishNumber("cap_set",
        (String(gConfig.deviceName) + " Einspeise-Cap").c_str(),
        T("capW/set").c_str(), T("capW/state").c_str(),
        1, 1500, 10, "W", "mdi:gauge");

    haPublishNumber("power_rate_set",
        (String(gConfig.deviceName) + " Leistung %").c_str(),
        T("powerRate/set").c_str(), T("powerRate/state").c_str(),
        0, 100, 1, "%", "mdi:speedometer");

    haPublished = true;
    Serial.printf("[HA] %d discovery topics published\n", 14);
}

// ── Subscription callback ─────────────────────────────────────
static void onMessage(char* topic, byte* payload, unsigned int len) {
    String msg;
    msg.reserve(len);
    for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
    msg.trim();

    if (String(topic) == T("relay/set")) {
        bool on = msg.equalsIgnoreCase("ON") || msg == "1";
        relaySet(on);
        mqttClient.publish(T("relay/state").c_str(), on ? "ON" : "OFF", true);

    } else if (String(topic) == T("capW/set")) {
        uint16_t w = (uint16_t)constrain((int)msg.toInt(), 1, DEFAULT_MAX_POWER_W);
        gConfig.capPowerW = w;
        configSave();
        char buf[8]; itoa(w, buf, 10);
        mqttClient.publish(T("capW/state").c_str(), buf, true);
        Serial.printf("[MQTT] capW → %d W\n", w);

    } else if (String(topic) == T("powerRate/set")) {
        uint8_t pct = (uint8_t)constrain((int)msg.toInt(), 0, 100);
        gConfig.powerRatePct = pct;
        if (gConfig.modbusWriteEn) {
            bool ok = modbusSetPowerRate(pct);
            Serial.printf("[MQTT] PowerRate → %d%% – Modbus write %s\n",
                          pct, ok ? "OK" : "FAILED");
        } else {
            Serial.printf("[MQTT] PowerRate → %d%% (Modbus write disabled)\n", pct);
        }
        configSave();
        char buf[8]; itoa(pct, buf, 10);
        mqttClient.publish(T("powerRate/state").c_str(), buf, true);

    } else if (String(topic) == T("cmd")) {
        if (msg == "save")         configSave();
        if (msg == "reset")        ESP.restart();
        if (msg == "ha_discovery") { haPublished = false; publishHADiscovery(); }

    } else if (String(topic) == HA_STATUS_TOPIC) {
        if (msg == "online") {
            haPublished = false;
            delay(2000);           // brief delay so HA is ready
            publishHADiscovery();
        }
    }
}

// ── Connect / reconnect ───────────────────────────────────────
static bool doConnect() {
    String cid = String(gConfig.deviceName) + "-" + String(ESP.getChipId(), HEX);
    String lwt = T("availability");
    bool ok;
    if (strlen(gConfig.mqttUser) > 0) {
        ok = mqttClient.connect(cid.c_str(),
                                gConfig.mqttUser, gConfig.mqttPass,
                                lwt.c_str(), 1, true, "offline");
    } else {
        ok = mqttClient.connect(cid.c_str(), nullptr, nullptr,
                                lwt.c_str(), 1, true, "offline");
    }
    if (!ok) return false;

    // Mark online
    mqttClient.publish(lwt.c_str(), "online", true);

    // Subscriptions
    mqttClient.subscribe(T("relay/set").c_str());
    mqttClient.subscribe(T("capW/set").c_str());
    mqttClient.subscribe(T("powerRate/set").c_str());
    mqttClient.subscribe(T("cmd").c_str());
    mqttClient.subscribe(HA_STATUS_TOPIC);

    // Publish retained states
    mqttClient.publish(T("relay/state").c_str(),
                       gConfig.relayOn ? "ON" : "OFF", true);
    char buf[8];
    itoa(gConfig.capPowerW,     buf, 10);
    mqttClient.publish(T("capW/state").c_str(), buf, true);
    itoa(gConfig.powerRatePct,  buf, 10);
    mqttClient.publish(T("powerRate/state").c_str(), buf, true);

    Serial.println("[MQTT] Connected");
    if (!haPublished) publishHADiscovery();
    return true;
}

// ── Public API ────────────────────────────────────────────────
void mqttSetup() {
    mqttClient.setServer(gConfig.mqttHost, gConfig.mqttPort);
    mqttClient.setCallback(onMessage);
    mqttClient.setBufferSize(1024);
}

void mqttLoop() {
    if (!mqttClient.connected()) {
        unsigned long now = millis();
        if (now - lastReconnect > 5000) {
            lastReconnect = now;
            haPublished   = false;
            if (WiFi.status() == WL_CONNECTED) doConnect();
        }
    }
    mqttClient.loop();
}

void mqttPublishAll() {
    if (!mqttClient.connected()) return;

    JsonDocument doc;
    doc["pvW"]      = gData.pvPowerW;
    doc["acW"]      = gData.acPowerW;
    doc["acV"]      = gData.acVoltageV;
    doc["acHz"]     = gData.acFreqHz;
    doc["pv1V"]     = gData.pvVoltage1V;
    doc["pv1A"]     = gData.pvCurrent1A;
    doc["tempC"]    = gData.tempC;
    doc["todayKWh"] = gData.energyTodayKWh;
    doc["totalKWh"] = gData.energyTotalKWh;
    doc["status"]   = gData.statusCode;
    doc["fault"]    = gData.faultActive;
    doc["capW"]     = gConfig.capPowerW;
    doc["pwrPct"]   = gConfig.powerRatePct;
    doc["relay"]    = gConfig.relayOn;
    doc["rssi"]     = WiFi.RSSI();
    doc["uptime"]   = millis() / 1000;

    char buf[512];
    serializeJson(doc, buf, sizeof(buf));
    mqttClient.publish(T("status").c_str(), buf, true);
}

bool mqttConnected() { return mqttClient.connected(); }
