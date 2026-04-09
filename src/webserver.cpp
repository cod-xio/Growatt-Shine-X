// ============================================================
//  webserver.cpp  –  ESPAsyncWebServer  (port 80)
//
//  REST endpoints:
//    GET  /api/data          → live inverter JSON
//    GET  /api/config        → current config JSON (no passwords)
//    POST /api/config        → update config (JSON body)
//    POST /api/relay         → {"relay": true/false}
//    POST /api/cap           → {"capW": 1500}
//    POST /api/powerRate     → {"pct": 0-100, "write": true}
//    GET  /api/status        → health / version JSON
//
//  Dashboard (index.html) served from PROGMEM (gzip-compressed)
//  No LittleFS needed for web files — single-binary deployment.
// ============================================================
#include "config.h"
#include "webserver.h"
#include "index_html.h"
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include "ntp.h"

extern AppConfig    gConfig;
extern InverterData gData;
extern bool         configSave();
extern void         relaySet(bool on);
extern bool         mqttConnected();
extern bool         modbusSetPowerRate(uint8_t pct);

static AsyncWebServer server(80);

// ── JSON helpers ─────────────────────────────────────────────
static void sendJson(AsyncWebServerRequest* req, const JsonDocument& doc, int code = 200) {
    String out;
    serializeJson(doc, out);
    req->send(code, "application/json", out);
}

// ── GET /api/data ─────────────────────────────────────────────
static void handleGetData(AsyncWebServerRequest* req) {
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
    doc["lastSeen"] = (millis() - gData.lastUpdate) / 1000;
    // NTP / time
    doc["ntpSynced"]    = gData.ntpSynced;
    doc["ntpTime"]      = gData.ntpTimeStr;
    doc["sysStartTime"] = gData.sysStartStr;
    sendJson(req, doc);
}

// ── GET /api/config ───────────────────────────────────────────
static void handleGetConfig(AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["wSSID"]       = gConfig.wifiSSID;
    doc["mHost"]       = gConfig.mqttHost;
    doc["mPort"]       = gConfig.mqttPort;
    doc["mUser"]       = gConfig.mqttUser;
    doc["mTopic"]      = gConfig.mqttTopicBase;
    doc["haDiscovery"] = gConfig.haDiscovery;
    doc["haPrefix"]    = gConfig.haPrefix;
    doc["capW"]        = gConfig.capPowerW;
    doc["modbusWrite"] = gConfig.modbusWriteEn;
    doc["pwrPct"]      = gConfig.powerRatePct;
    doc["devName"]     = gConfig.deviceName;
    doc["hostname"]    = gConfig.hostname;
    doc["staticIp"]    = gConfig.staticIp;
    doc["staticIpAddr"]= gConfig.staticIpAddr;
    doc["subnet"]      = gConfig.subnet;
    doc["gateway"]     = gConfig.gateway;
    doc["dns"]         = gConfig.dnsServer;
    doc["ntpServer1"]  = gConfig.ntpServer1;
    doc["ntpServer2"]  = gConfig.ntpServer2;
    doc["ntpServer3"]  = gConfig.ntpServer3;
    doc["ntpTzOffset"] = gConfig.ntpTzOffset;
    doc["ntpDst"]      = gConfig.ntpDst;
    doc["ntpInterval"] = gConfig.ntpInterval;
    doc["authEnabled"] = gConfig.authEnabled;
    sendJson(req, doc);
}

// ── POST /api/config ──────────────────────────────────────────
static void handlePostConfig(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len)) { req->send(400, "application/json", "{\"error\":\"bad json\"}"); return; }

    if (doc["wSSID"].is<const char*>())  strlcpy(gConfig.wifiSSID,  doc["wSSID"],  sizeof(gConfig.wifiSSID));
    if (doc["wPass"].is<const char*>())  strlcpy(gConfig.wifiPass,  doc["wPass"],  sizeof(gConfig.wifiPass));
    if (doc["mHost"].is<const char*>())  strlcpy(gConfig.mqttHost,  doc["mHost"],  sizeof(gConfig.mqttHost));
    if (doc["mPort"].is<uint16_t>())     gConfig.mqttPort = doc["mPort"];
    if (doc["mUser"].is<const char*>())  strlcpy(gConfig.mqttUser,  doc["mUser"],  sizeof(gConfig.mqttUser));
    if (doc["mPass"].is<const char*>())  strlcpy(gConfig.mqttPass,  doc["mPass"],  sizeof(gConfig.mqttPass));
    if (doc["mTopic"].is<const char*>()) strlcpy(gConfig.mqttTopicBase, doc["mTopic"], sizeof(gConfig.mqttTopicBase));
    if (doc["devName"].is<const char*>())strlcpy(gConfig.deviceName, doc["devName"], sizeof(gConfig.deviceName));
    if (doc["hostname"].is<const char*>())    strlcpy(gConfig.hostname,     doc["hostname"],     sizeof(gConfig.hostname));
    if (doc["staticIp"].is<bool>())           gConfig.staticIp     = doc["staticIp"];
    if (doc["staticIpAddr"].is<const char*>())strlcpy(gConfig.staticIpAddr, doc["staticIpAddr"], sizeof(gConfig.staticIpAddr));
    if (doc["subnet"].is<const char*>())      strlcpy(gConfig.subnet,       doc["subnet"],       sizeof(gConfig.subnet));
    if (doc["gateway"].is<const char*>())     strlcpy(gConfig.gateway,      doc["gateway"],      sizeof(gConfig.gateway));
    if (doc["dns"].is<const char*>())         strlcpy(gConfig.dnsServer,    doc["dns"],          sizeof(gConfig.dnsServer));
    if (doc["ntpServer1"].is<const char*>()) strlcpy(gConfig.ntpServer1, doc["ntpServer1"], sizeof(gConfig.ntpServer1));
    if (doc["ntpServer2"].is<const char*>()) strlcpy(gConfig.ntpServer2, doc["ntpServer2"], sizeof(gConfig.ntpServer2));
    if (doc["ntpServer3"].is<const char*>()) strlcpy(gConfig.ntpServer3, doc["ntpServer3"], sizeof(gConfig.ntpServer3));
    if (doc["ntpTzOffset"].is<int>())        gConfig.ntpTzOffset  = (int8_t)doc["ntpTzOffset"];
    if (doc["ntpDst"].is<uint8_t>())         gConfig.ntpDst       = doc["ntpDst"];
    if (doc["ntpInterval"].is<uint32_t>())   gConfig.ntpInterval  = doc["ntpInterval"];
    if (doc["authEnabled"].is<bool>())       gConfig.authEnabled  = doc["authEnabled"];
    if (doc["haDiscovery"].is<bool>())   gConfig.haDiscovery   = doc["haDiscovery"];
    if (doc["haPrefix"].is<const char*>())strlcpy(gConfig.haPrefix, doc["haPrefix"], sizeof(gConfig.haPrefix));
    if (doc["modbusWrite"].is<bool>())   gConfig.modbusWriteEn = doc["modbusWrite"];
    if (doc["pwrPct"].is<uint8_t>())     gConfig.powerRatePct  = constrain((uint8_t)doc["pwrPct"], 0, 100);
    if (doc["capW"].is<uint16_t>())      gConfig.capPowerW = constrain((uint16_t)doc["capW"], 1, DEFAULT_MAX_POWER_W);

    configSave();
    req->send(200, "application/json", "{\"ok\":true}");
}

// ── POST /api/relay ───────────────────────────────────────────
static void handlePostRelay(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len)) { req->send(400); return; }
    relaySet(doc["relay"] | false);
    req->send(200, "application/json", "{\"ok\":true}");
}

// ── POST /api/cap ─────────────────────────────────────────────
static void handlePostCap(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len)) { req->send(400); return; }
    uint16_t w = constrain((uint16_t)(doc["capW"] | DEFAULT_CAP_POWER_W), 1, DEFAULT_MAX_POWER_W);
    gConfig.capPowerW = w;
    configSave();
    req->send(200, "application/json", "{\"ok\":true}");
}

// ── POST /api/powerRate ───────────────────────────────────────
//  Body: {"pct": 0-100, "write": true}
//  write=true → immediately push to inverter via Modbus FC06
static void handlePostPowerRate(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len)) { req->send(400); return; }
    uint8_t pct = (uint8_t)constrain((int)(doc["pct"] | 100), 0, 100);
    bool    doWrite = doc["write"] | false;

    gConfig.powerRatePct = pct;
    configSave();

    bool writeOk = true;
    if (doWrite && gConfig.modbusWriteEn) {
        writeOk = modbusSetPowerRate(pct);
    }

    JsonDocument resp;
    resp["ok"]      = writeOk;
    resp["pct"]     = pct;
    resp["written"] = (doWrite && gConfig.modbusWriteEn);
    sendJson(req, resp);
}

// ── GET /api/status ───────────────────────────────────────────
static void handleGetStatus(AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["fw"]       = FW_VERSION;
    doc["build"]    = FW_BUILD;
    doc["device"]   = gConfig.deviceName;
    doc["hostname"] = gConfig.hostname;
    doc["ip"]       = WiFi.localIP().toString();
    doc["mac"]      = WiFi.macAddress();
    doc["rssi"]     = WiFi.RSSI();
    doc["uptime"]   = millis() / 1000;
    doc["heap"]     = ESP.getFreeHeap();
    doc["mqtt"]     = mqttConnected();
    doc["ntpSynced"]= gData.ntpSynced;
    doc["htmlSz"]   = INDEX_HTML_GZ_LEN;
    sendJson(req, doc);
}

// ── POST /api/cmd ─────────────────────────────────────────────
static void handlePostCmd(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    JsonDocument doc;
    if (deserializeJson(doc, data, len)) { req->send(400); return; }
    String cmd = doc["cmd"] | "";
    if (cmd == "reset") {
        req->send(200, "application/json", "{\"ok\":true}");
        delay(300);
        ESP.restart();
        return;
    }
    if (cmd == "ntp_sync") {
        ntpForceSync();
        req->send(200, "application/json", "{\"ok\":true}");
        return;
    }
    if (cmd == "factory_reset") {
        // Wipe LittleFS config file → resets all settings on next boot
        LittleFS.remove(CONFIG_PATH);
        req->send(200, "application/json", "{\"ok\":true,\"msg\":\"Config deleted – rebooting\"}");
        delay(500);
        ESP.restart();
        return;
    }
    req->send(200, "application/json", "{\"ok\":true}");
}

// ── GET / — serve embedded gzipped HTML ──────────────────────
static void handleRoot(AsyncWebServerRequest* req) {
    AsyncWebServerResponse* resp =
        req->beginResponse_P(200, "text/html",
                             INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
    resp->addHeader("Content-Encoding", "gzip");
    resp->addHeader("Cache-Control",    "max-age=300");
    req->send(resp);
}

// ── Setup ─────────────────────────────────────────────────────
void webServerSetup() {
    // CORS headers for every response
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

    server.on("/api/data",   HTTP_GET,  handleGetData);
    server.on("/api/config", HTTP_GET,  handleGetConfig);
    server.on("/api/status", HTTP_GET,  handleGetStatus);

    server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest* r){},
        nullptr,
        [](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t){ handlePostConfig(r,d,l,i,t); });

    server.on("/api/relay", HTTP_POST,
        [](AsyncWebServerRequest* r){},
        nullptr,
        [](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t){ handlePostRelay(r,d,l,i,t); });

    server.on("/api/cmd", HTTP_POST,
        [](AsyncWebServerRequest* r){},
        nullptr,
        [](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t){ handlePostCmd(r,d,l,i,t); });

    server.on("/api/powerRate", HTTP_POST,
        [](AsyncWebServerRequest* r){},
        nullptr,
        [](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t){ handlePostPowerRate(r,d,l,i,t); });

    server.on("/api/cap", HTTP_POST,
        [](AsyncWebServerRequest* r){},
        nullptr,
        [](AsyncWebServerRequest* r, uint8_t* d, size_t l, size_t i, size_t t){ handlePostCap(r,d,l,i,t); });

    server.on("/",         HTTP_GET,  handleRoot);
    server.on("/index.html", HTTP_GET, handleRoot);

    server.onNotFound([](AsyncWebServerRequest* req) {
        // Redirect everything unknown to root (SPA behaviour)
        if (req->method() == HTTP_GET) {
            AsyncWebServerResponse* resp =
                req->beginResponse_P(200, "text/html",
                                     INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
            resp->addHeader("Content-Encoding", "gzip");
            req->send(resp);
        } else {
            req->send(404, "application/json", "{\"error\":\"not found\"}");
        }
    });

    server.begin();
    Serial.println("[WEB] HTTP server started");
}

void webServerLoop() { /* async – nothing to do */ }
