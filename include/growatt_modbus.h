#pragma once
// ============================================================
//  growatt_modbus.h  –  Growatt MIC 1500TL-X  Modbus RTU reader
//  Growatt SHINE WiFi X Firmware
// ============================================================
#include <Arduino.h>

void     modbusSetup();
bool     modbusPoll();              // returns true on successful read
bool     modbusSetPowerRate(uint8_t pct);  // write 0-100% limit to inverter
bool     modbusWriteReg(uint16_t reg, uint16_t value); // low-level FC06 write
