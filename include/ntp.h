#pragma once
// ============================================================
//  ntp.h  –  NTP time synchronisation
//  Growatt SHINE WiFi X Firmware
// ============================================================
#include <Arduino.h>

void ntpSetup();          // call after WiFi connected
void ntpLoop();           // call in loop()
void ntpForceSync();      // trigger immediate re-sync
bool ntpIsSynced();
String ntpGetTimeStr();   // "DD.MM.YYYY HH:MM:SS"
