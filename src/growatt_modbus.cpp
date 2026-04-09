// ============================================================
//  growatt_modbus.cpp
//  Minimal Modbus RTU master for Growatt MIC 1500TL-X
//  Register map based on Growatt Modbus RTU Protocol v1.20
//
//  Holding Registers (Function 0x03), address base 0x0000:
//    0x0000  Status              (0=waiting,1=normal,3=fault)
//    0x0001  PV power high word  x0.1 W
//    0x0002  PV power low word
//    0x0003  PV1 voltage         x0.1 V
//    0x0004  PV1 current         x0.1 A
//    0x0009  AC power high word  x0.1 W
//    0x000A  AC power low word
//    0x000B  AC voltage          x0.1 V
//    0x000C  AC current          x0.1 A
//    0x000F  AC frequency        x0.01 Hz
//    0x0011  Temperature         x0.1 °C
//    0x0013  Energy today high   x0.1 kWh
//    0x0014  Energy today low
//    0x0015  Energy total high   x0.1 kWh
//    0x0016  Energy total low
//    0x001E  Fault code
// ============================================================
#include "config.h"
#include "growatt_modbus.h"
#include <SoftwareSerial.h>

extern AppConfig    gConfig;
extern InverterData gData;

static SoftwareSerial rs485(PIN_RS485_RX, PIN_RS485_TX);

// ── CRC-16 Modbus ─────────────────────────────────────────────
static uint16_t crc16(const uint8_t* buf, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 1) { crc >>= 1; crc ^= 0xA001; }
            else           crc >>= 1;
        }
    }
    return crc;
}

// ── Build FC03 request ────────────────────────────────────────
static void buildRequest(uint8_t* frame, uint8_t addr, uint16_t reg, uint16_t count) {
    frame[0] = addr;
    frame[1] = 0x03;
    frame[2] = reg >> 8;
    frame[3] = reg & 0xFF;
    frame[4] = count >> 8;
    frame[5] = count & 0xFF;
    uint16_t c = crc16(frame, 6);
    frame[6] = c & 0xFF;
    frame[7] = c >> 8;
}

// ── Read N registers starting at reg ─────────────────────────
static bool readRegs(uint16_t startReg, uint16_t count, uint16_t* out) {
    uint8_t req[8];
    buildRequest(req, GROWATT_ADDR, startReg, count);

    // flush RX
    while (rs485.available()) rs485.read();

    rs485.write(req, 8);
    rs485.flush();

    // expected response: addr(1) + fc(1) + byteCount(1) + data(count*2) + CRC(2)
    size_t expected = 3 + (size_t)count * 2 + 2;
    // max response for 32 registers = 3 + 64 + 2 = 69 bytes → use 72 for safety
    uint8_t resp[72];
    if (expected > sizeof(resp)) return false;   // guard
    size_t  got = 0;
    unsigned long t0 = millis();
    while (got < expected && millis() - t0 < 500) {
        if (rs485.available()) resp[got++] = rs485.read();
        yield();
    }
    if (got < expected) return false;

    // verify CRC
    uint16_t calc = crc16(resp, got - 2);
    uint16_t recv = resp[got-2] | (resp[got-1] << 8);
    if (calc != recv) return false;

    // extract registers
    for (uint16_t i = 0; i < count; i++) {
        out[i] = (resp[3 + i*2] << 8) | resp[4 + i*2];
    }
    return true;
}

// ── Combine high/low 16-bit words into 32-bit float ──────────
static float reg32f(uint16_t hi, uint16_t lo, float scale) {
    uint32_t v = ((uint32_t)hi << 16) | lo;
    return v * scale;
}

// ── Build FC06 single-register write request ─────────────────
static void buildWriteRequest(uint8_t* frame, uint8_t addr,
                               uint16_t reg, uint16_t value) {
    frame[0] = addr;
    frame[1] = 0x06;           // FC06 – Write Single Register
    frame[2] = reg >> 8;
    frame[3] = reg & 0xFF;
    frame[4] = value >> 8;
    frame[5] = value & 0xFF;
    uint16_t c = crc16(frame, 6);
    frame[6] = c & 0xFF;
    frame[7] = c >> 8;
}

// ── Write single Modbus register (FC06) ──────────────────────
//  The inverter echoes the request frame unchanged on success.
bool modbusWriteReg(uint16_t reg, uint16_t value) {
    uint8_t req[8];
    buildWriteRequest(req, GROWATT_ADDR, reg, value);

    // flush RX
    while (rs485.available()) rs485.read();

    rs485.write(req, 8);
    rs485.flush();

    // echo response is identical to request (8 bytes)
    uint8_t resp[8];
    size_t  got = 0;
    unsigned long t0 = millis();
    while (got < 8 && millis() - t0 < 500) {
        if (rs485.available()) resp[got++] = rs485.read();
        yield();
    }
    if (got < 8) {
        Serial.printf("[RS485] FC06 timeout (reg=0x%04X)\n", reg);
        return false;
    }

    // Verify CRC of echo
    uint16_t calc = crc16(resp, 6);
    uint16_t recv = resp[6] | (resp[7] << 8);
    if (calc != recv) {
        Serial.printf("[RS485] FC06 CRC error (reg=0x%04X)\n", reg);
        return false;
    }

    // Verify echo matches request
    for (int i = 0; i < 6; i++) {
        if (resp[i] != req[i]) {
            Serial.printf("[RS485] FC06 echo mismatch byte %d\n", i);
            return false;
        }
    }
    return true;
}

// ── Set inverter active-power limit ──────────────────────────
//  Growatt MIC 1500TL-X register map:
//    0x001F  Active Power Rate  (0–100, integer %)
//    0x0020  Active Power Rate Enable  (0=disable, 1=enable)
//
//  Both registers must be written together for the change to take effect.
//  pct = 0  → 0 % output (effectively off)
//  pct = 100 → full output (no limiting)
bool modbusSetPowerRate(uint8_t pct) {
    pct = constrain(pct, 0, 100);
    Serial.printf("[RS485] Writing power rate %d%% to inverter...\n", pct);

    // 1. Write rate value
    if (!modbusWriteReg(REG_POWER_RATE, (uint16_t)pct)) return false;
    delay(50);   // brief gap between writes

    // 2. Enable rate limiter (write 1)
    if (!modbusWriteReg(REG_POWER_RATE_EN, 1)) return false;

    Serial.printf("[RS485] Power rate %d%% confirmed\n", pct);
    return true;
}

// ── Public API ────────────────────────────────────────────────
void modbusSetup() {
    rs485.begin(GROWATT_BAUD);
    Serial.printf("[RS485] SoftSerial RX=%d TX=%d @ %d baud\n",
                  PIN_RS485_RX, PIN_RS485_TX, GROWATT_BAUD);
}

bool modbusPoll() {
    // Read 0x0000 – 0x001F  (32 registers in one shot)
    uint16_t regs[32];
    if (!readRegs(0x0000, 32, regs)) return false;

    gData.statusCode      = regs[0x00];
    gData.pvPowerW        = reg32f(regs[0x01], regs[0x02], 0.1f);
    gData.pvVoltage1V     = regs[0x03] * 0.1f;
    gData.pvCurrent1A     = regs[0x04] * 0.1f;
    gData.acPowerW        = reg32f(regs[0x09], regs[0x0A], 0.1f);
    gData.acVoltageV      = regs[0x0B] * 0.1f;
    gData.acFreqHz        = regs[0x0F] * 0.01f;
    gData.tempC           = regs[0x11] * 0.1f;
    gData.energyTodayKWh  = reg32f(regs[0x13], regs[0x14], 0.1f);
    gData.energyTotalKWh  = reg32f(regs[0x15], regs[0x16], 0.1f);
    gData.faultActive     = (regs[0x1E] != 0);
    gData.lastUpdate      = millis();

    // Enforce power cap: if AC power exceeds cap, flag it (relay can be used)
    if (gData.acPowerW > gConfig.capPowerW) {
        Serial.printf("[CAP] AC %.1f W exceeds cap %d W\n",
                      gData.acPowerW, gConfig.capPowerW);
    }
    return true;
}
