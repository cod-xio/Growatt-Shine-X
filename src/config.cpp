// ============================================================
//  config.cpp  –  LittleFS persistence for AppConfig
//  Growatt SHINE WiFi X Firmware
// ============================================================
#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

// ── Globals (defined here, declared extern in other TUs) ─────
AppConfig  gConfig;
InverterData gData;

// ─────────────────────────────────────────────────────────────
bool configLoad() {
    if (!LittleFS.exists(CONFIG_PATH)) return false;

    File f = LittleFS.open(CONFIG_PATH, "r");
    if (!f) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;

    // WiFi
    strlcpy(gConfig.wifiSSID,  doc["wSSID"] | "",          sizeof(gConfig.wifiSSID));
    strlcpy(gConfig.wifiPass,  doc["wPass"] | "",          sizeof(gConfig.wifiPass));

    // MQTT
    strlcpy(gConfig.mqttHost,  doc["mHost"] | "192.168.1.100", sizeof(gConfig.mqttHost));
    gConfig.mqttPort =         doc["mPort"] | 1883;
    strlcpy(gConfig.mqttUser,  doc["mUser"] | "",          sizeof(gConfig.mqttUser));
    strlcpy(gConfig.mqttPass,  doc["mPass"] | "",          sizeof(gConfig.mqttPass));
    strlcpy(gConfig.mqttTopicBase, doc["mTopic"] | "growatt/shine", sizeof(gConfig.mqttTopicBase));

    // NTP
    strlcpy(gConfig.ntpServer1, doc["ntpS1"] | "pool.ntp.org",       sizeof(gConfig.ntpServer1));
    strlcpy(gConfig.ntpServer2, doc["ntpS2"] | "time.google.com",    sizeof(gConfig.ntpServer2));
    strlcpy(gConfig.ntpServer3, doc["ntpS3"] | "time.cloudflare.com",sizeof(gConfig.ntpServer3));
    gConfig.ntpTzOffset  = (int8_t)(doc["ntpTz"]  | 1);
    gConfig.ntpDst       = (uint8_t)(doc["ntpDst"] | 1);
    gConfig.ntpInterval  = doc["ntpIv"]  | 21600;
    gConfig.authEnabled  = doc["auth"]   | false;

    // Network
    strlcpy(gConfig.hostname,     doc["host"]    | "Growatt-SHINE-X", sizeof(gConfig.hostname));
    gConfig.staticIp = doc["sIp"] | false;
    strlcpy(gConfig.staticIpAddr, doc["sAddr"]   | "",                sizeof(gConfig.staticIpAddr));
    strlcpy(gConfig.subnet,       doc["sMask"]   | "255.255.255.0",   sizeof(gConfig.subnet));
    strlcpy(gConfig.gateway,      doc["sGw"]     | "",                sizeof(gConfig.gateway));
    strlcpy(gConfig.dnsServer,    doc["sDns"]    | "8.8.8.8",         sizeof(gConfig.dnsServer));

    // Home Assistant
    gConfig.haDiscovery = doc["haDisc"] | true;
    strlcpy(gConfig.haPrefix, doc["haPrefix"] | HA_DISCOVERY_PREFIX, sizeof(gConfig.haPrefix));

    // Modbus write
    gConfig.modbusWriteEn = doc["mbWrite"] | false;
    gConfig.powerRatePct  = constrain((uint8_t)(doc["pwrPct"] | 100), 0, 100);

    // Power
    gConfig.capPowerW = constrain((uint16_t)(doc["capW"] | DEFAULT_CAP_POWER_W), 1, DEFAULT_MAX_POWER_W);

    // Relay & name
    gConfig.relayOn  = doc["relay"] | false;
    strlcpy(gConfig.deviceName, doc["devName"] | DEVICE_NAME, sizeof(gConfig.deviceName));

    return true;
}

// ─────────────────────────────────────────────────────────────
bool configSave() {
    JsonDocument doc;

    doc["wSSID"]   = gConfig.wifiSSID;
    doc["wPass"]   = gConfig.wifiPass;
    doc["mHost"]   = gConfig.mqttHost;
    doc["mPort"]   = gConfig.mqttPort;
    doc["mUser"]   = gConfig.mqttUser;
    doc["mPass"]   = gConfig.mqttPass;
    doc["mTopic"]  = gConfig.mqttTopicBase;
    doc["ntpS1"]   = gConfig.ntpServer1;
    doc["ntpS2"]   = gConfig.ntpServer2;
    doc["ntpS3"]   = gConfig.ntpServer3;
    doc["ntpTz"]   = gConfig.ntpTzOffset;
    doc["ntpDst"]  = gConfig.ntpDst;
    doc["ntpIv"]   = gConfig.ntpInterval;
    doc["auth"]    = gConfig.authEnabled;
    doc["host"]    = gConfig.hostname;
    doc["sIp"]     = gConfig.staticIp;
    doc["sAddr"]   = gConfig.staticIpAddr;
    doc["sMask"]   = gConfig.subnet;
    doc["sGw"]     = gConfig.gateway;
    doc["sDns"]    = gConfig.dnsServer;
    doc["haDisc"]  = gConfig.haDiscovery;
    doc["haPrefix"]= gConfig.haPrefix;
    doc["mbWrite"] = gConfig.modbusWriteEn;
    doc["pwrPct"]  = gConfig.powerRatePct;
    doc["capW"]    = gConfig.capPowerW;
    doc["relay"]   = gConfig.relayOn;
    doc["devName"] = gConfig.deviceName;

    File f = LittleFS.open(CONFIG_PATH, "w");
    if (!f) return false;
    serializeJson(doc, f);
    f.close();
    return true;
}
