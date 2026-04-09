#pragma once
// ============================================================
//  mqtt.h  –  MQTT publish / subscribe interface
//  Growatt SHINE WiFi X Firmware
// ============================================================
#include <Arduino.h>

void mqttSetup();
void mqttLoop();
void mqttPublishAll();
bool mqttConnected();
