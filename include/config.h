#pragma once
// ============================================================
//  config.h  –  Configuration structures & defaults
//  Growatt SHINE WiFi X Firmware
// ============================================================
#include <Arduino.h>
#include <ArduinoJson.h>

// ── Firmware meta ────────────────────────────────────────────
#define FW_VERSION      "1.1.0"
#define FW_BUILD        __DATE__ " " __TIME__
#define DEVICE_NAME     "Growatt-SHINE-X"

// ── Hardware pins ────────────────────────────────────────────
#define PIN_RELAY       D1   // Relay output (active HIGH)
#define PIN_LED         LED_BUILTIN

// ── Growatt RS485 (Software Serial) ─────────────────────────
#define PIN_RS485_RX    D6
#define PIN_RS485_TX    D7
#define GROWATT_BAUD    9600
#define GROWATT_ADDR    0x01   // Default Modbus address

// ── AP fallback ──────────────────────────────────────────────
#define AP_SSID         "Growatt-Setup"
#define AP_PASS         "growatt123"
#define AP_IP           "192.168.4.1"

// ── Timing (ms) ─────────────────────────────────────────────
#define INTERVAL_MODBUS   5000   // Poll inverter every 5 s
#define INTERVAL_MQTT     10000  // Publish MQTT every 10 s
#define INTERVAL_SAVE     30000  // Persist config every 30 s

// ── Power cap default ────────────────────────────────────────
#define DEFAULT_MAX_POWER_W  1500   // Hard limit per spec
#define DEFAULT_CAP_POWER_W  1500   // User-adjustable cap

// ── Config filesystem path ───────────────────────────────────
#define CONFIG_PATH     "/config.json"

// ── Home Assistant Discovery ─────────────────────────────────
#define HA_DISCOVERY_PREFIX  "homeassistant"   // HA default prefix
#define HA_STATUS_TOPIC      "homeassistant/status"

// ── Growatt Power-Limit Registers (FC06 Write) ───────────────
//   Register 0x001F  –  Active Power Rate (0–100 %)
//   Register 0x0020  –  Active Power Rate Enable (1=enabled)
#define REG_POWER_RATE        0x001F
#define REG_POWER_RATE_EN     0x0020

// ────────────────────────────────────────────────────────────
//  Runtime configuration (persisted to LittleFS)
// ────────────────────────────────────────────────────────────
struct AppConfig {
    // WiFi
    char wifiSSID[64]     = "";
    char wifiPass[64]     = "";

    // MQTT
    char mqttHost[64]     = "192.168.1.100";
    uint16_t mqttPort     = 1883;
    char mqttUser[32]     = "";
    char mqttPass[32]     = "";
    char mqttTopicBase[64]= "growatt/shine";

    // Home Assistant Auto-Discovery
    bool haDiscovery      = true;    // publish HA discovery topics on connect
    char haPrefix[32]     = HA_DISCOVERY_PREFIX;

    // NTP
    char     ntpServer1[64]  = "pool.ntp.org";
    char     ntpServer2[64]  = "time.google.com";
    char     ntpServer3[64]  = "time.cloudflare.com";
    int8_t   ntpTzOffset     = 1;      // UTC+1 default (CET)
    uint8_t  ntpDst          = 1;      // DST auto
    uint32_t ntpInterval     = 21600;  // sync every 6 h

    // Auth
    bool     authEnabled     = false;

    // Network / Hostname / Static IP
    char     hostname[32]    = "Growatt-SHINE-X";
    bool     staticIp        = false;
    char     staticIpAddr[16]= "";
    char     subnet[16]      = "255.255.255.0";
    char     gateway[16]     = "";
    char     dnsServer[16]   = "8.8.8.8";

    // Power cap  (1 – 1500 W)
    uint16_t capPowerW    = DEFAULT_CAP_POWER_W;

    // Modbus power-limit write
    bool modbusWriteEn    = false;   // must be explicitly enabled by user
    uint8_t powerRatePct  = 100;     // 0-100 % written to REG_POWER_RATE

    // Relay state
    bool relayOn          = false;

    // Misc
    char deviceName[32]   = DEVICE_NAME;
};

// ────────────────────────────────────────────────────────────
//  Live inverter data (from Modbus)
// ────────────────────────────────────────────────────────────
struct InverterData {
    float pvPowerW        = 0.0f;   // Current PV power (W)
    float pvVoltage1V     = 0.0f;   // String 1 voltage
    float pvCurrent1A     = 0.0f;   // String 1 current
    float acPowerW        = 0.0f;   // AC output power (W)
    float acVoltageV      = 0.0f;   // AC voltage
    float acFreqHz        = 0.0f;   // AC frequency
    float tempC           = 0.0f;   // Inverter temperature
    float energyTodayKWh  = 0.0f;   // Today's yield
    float energyTotalKWh  = 0.0f;   // Lifetime yield
    uint16_t statusCode   = 0;      // Growatt status word
    bool   faultActive    = false;
    uint8_t powerRatePct  = 100;
    // NTP / time
    bool   ntpSynced      = false;
    char   ntpTimeStr[32] = "";        // formatted local time string
    char   sysStartStr[32]= "";        // boot time string
    unsigned long lastUpdate = 0;
};
