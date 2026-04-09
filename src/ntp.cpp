// ============================================================
//  ntp.cpp  –  NTP time synchronisation
//  Uses ESP8266 built-in configTime() / sntp
//  Timezone: UTC offset + DST from AppConfig
// ============================================================
#include "config.h"
#include "ntp.h"
#include <ESP8266WiFi.h>
#include <time.h>       // POSIX time / gmtime_r / strftime

extern AppConfig    gConfig;
extern InverterData gData;

static unsigned long lastNtpSync   = 0;
static bool          synced        = false;
static bool          bootTimeSaved = false;

// ── Build POSIX TZ string from offset + DST flag ─────────────
//  e.g. UTC+1 with DST → "CET-1CEST,M3.5.0,M10.5.0/3"
static String buildTzString(int8_t utcOff, bool dst) {
    if (!dst) {
        // No DST — simple fixed offset
        // POSIX sign is inverted (UTC-1 = offset +1)
        String s = "UTC";
        if      (utcOff > 0) { s += "-"; s += utcOff; }
        else if (utcOff < 0) { s += "+"; s += -utcOff; }
        return s;
    }
    // With DST — common European rules hard-coded for convenience
    // Central European: CET-1CEST,M3.5.0,M10.5.0/3
    // Eastern European: EET-2EEST,M3.5.0/3,M10.5.0/4
    // Western European: WET0WEST,M3.5.0/1,M10.5.0
    switch (utcOff) {
        case 0:  return "WET0WEST,M3.5.0/1,M10.5.0";
        case 1:  return "CET-1CEST,M3.5.0,M10.5.0/3";
        case 2:  return "EET-2EEST,M3.5.0/3,M10.5.0/4";
        case 3:  return "MSK-3";   // Moscow – no DST
        default: {
            // Generic: just add 1h for DST in summer (rough)
            String s = "STD";
            int8_t n = -utcOff;
            if (n >= 0) s += String(n); else { s += "+"; s += String(-n); }
            s += "DST"; s += String(-(utcOff+1));
            s += ",M3.5.0,M10.5.0/3";
            return s;
        }
    }
}

// ── Format struct tm to readable string ──────────────────────
static String fmtTime(const struct tm* t) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d.%02d.%04d %02d:%02d:%02d",
             t->tm_mday, t->tm_mon+1, t->tm_year+1900,
             t->tm_hour, t->tm_min, t->tm_sec);
    return String(buf);
}

// ── Public API ────────────────────────────────────────────────
void ntpSetup() {
    bool dst = (gConfig.ntpDst != 0);
    String tz = buildTzString(gConfig.ntpTzOffset, dst);

    // configTime with 3 NTP servers and POSIX TZ string
    configTime(tz.c_str(),
               gConfig.ntpServer1,
               gConfig.ntpServer2,
               gConfig.ntpServer3);

    Serial.printf("[NTP] Servers: %s  %s  %s\n",
                  gConfig.ntpServer1, gConfig.ntpServer2, gConfig.ntpServer3);
    Serial.printf("[NTP] TZ string: %s\n", tz.c_str());

    lastNtpSync = millis();
}

void ntpLoop() {
    // Check if time is now valid (year > 2020)
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);

    if (!synced && ti.tm_year > 120) {
        synced = true;
        Serial.printf("[NTP] Synced: %s\n", fmtTime(&ti).c_str());

        // Store current time string
        String ts = fmtTime(&ti);
        strlcpy(gData.ntpTimeStr, ts.c_str(), sizeof(gData.ntpTimeStr));
        gData.ntpSynced = true;

        // Save boot time once
        if (!bootTimeSaved) {
            bootTimeSaved = true;
            // Boot time = now - uptime
            time_t bootEpoch = now - (time_t)(millis() / 1000);
            struct tm bt;
            localtime_r(&bootEpoch, &bt);
            String bts = fmtTime(&bt);
            strlcpy(gData.sysStartStr, bts.c_str(), sizeof(gData.sysStartStr));
        }
    }

    // Periodic re-sync (configTime handles it via SNTP, but log it)
    unsigned long interval = (unsigned long)gConfig.ntpInterval * 1000UL;
    if (synced && millis() - lastNtpSync >= interval) {
        lastNtpSync = millis();
        // Update time string
        String ts = fmtTime(&ti);
        strlcpy(gData.ntpTimeStr, ts.c_str(), sizeof(gData.ntpTimeStr));
        Serial.printf("[NTP] Periodic update: %s\n", ts.c_str());
    }

    // Always keep time string current when synced
    if (synced) {
        String ts = fmtTime(&ti);
        strlcpy(gData.ntpTimeStr, ts.c_str(), sizeof(gData.ntpTimeStr));
    }
}

void ntpForceSync() {
    Serial.println("[NTP] Force re-sync requested");
    synced = false;
    gData.ntpSynced = false;
    ntpSetup();   // re-call configTime to trigger immediate SNTP query
    lastNtpSync = millis();
}

bool ntpIsSynced() { return synced; }

String ntpGetTimeStr() {
    if (!synced) return "–";
    time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    return fmtTime(&ti);
}
