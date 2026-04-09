#pragma once
// ============================================================
//  webserver.h  –  Async HTTP server interface
//  Growatt SHINE WiFi X Firmware
// ============================================================
#include <Arduino.h>

void webServerSetup();
void webServerLoop();   // not strictly needed (async) but kept for symmetry
