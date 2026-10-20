// ELT295 Lab - Networked temperature alarm with 1-Wire bring-up:
//              HW-506 (DS18B20) + HW-508 passive buzzer
//
// Same alarm behavior as TempAlarm, but the DS18B20 is driven directly over 1-Wire
// so every bus transaction can be shown to the student. One JSON line per second
// goes out on Serial (115200 baud); bridge/bridge.html forwards it to the dashboard.
//
// BRING-UP (runs at boot, and again every 3 s while the sensor is failing):
//   STAGE 1 - 1-Wire bus      line idle level + presence pulse
//                             (is the sensor powered and wired to D2?)
//   STAGE 2 - Identification  ROM code, family code 0x28 = DS18B20, ROM CRC
//                             (is it the part we expect, and is the bus clean?)
//   STAGE 3 - Functional      power mode, scratchpad + CRC, a real conversion,
//                             range check on the result
//                             (does it produce sane DATA, not just a pulse?)
//
// Alarm:
//   Below BEEP_ON_F -> silent, BEEP_ON_F and above -> beeping, SOLID_ON_F and above -> solid
//   (thresholds and hysteresis are the constants below)
//
// Naming your board: send  ID:YourName  (Serial Monitor or the bridge page).
//   Saved to EEPROM. Unnamed boards appear on the dashboard as uno-XXXX.
//
// Wiring:
//   HW-506 VCC -> 5V, GND -> GND, signal -> D2
//   HW-508 S   -> D9, - -> GND, middle pin not connected (no VCC pin)
//   HW-503 light sensor (LDR module): VCC -> 5V, GND -> GND, AO -> A0, DO -> D3
//   Exactly one DS18B20 on the bus (commands use Skip ROM 0xCC).
//
// Light sensor: reports raw ADC (0-1023), a brightness %, the DO pin level, and the
//   min/max seen since boot. "Responds" turns PASS once the value has swung by 100+
//   counts, so cover the sensor with your hand to prove it works. Set LIGHT_INVERT
//   below if your module reads higher when dark.
//
// Library: OneWire (Paul Stoffregen)

#include <OneWire.h>
#include <EEPROM.h>

const int ONE_WIRE_PIN = 2;
const int BUZZER_PIN   = 9;
const int LIGHT_AO_PIN = A0;   // HW-503 analog output
const int LIGHT_DO_PIN = 3;    // HW-503 digital output (threshold set by the blue pot)

// Which way does your HW-503 read? Do the cover test (hand over the sensor):
//   raw value goes DOWN when covered -> false   raw value goes UP when covered -> true
const bool LIGHT_INVERT = false;

const float BEEP_ON_F  = 77.0;   // start beeping
const float SOLID_ON_F = 80.0;   // go solid
const float HYST_F     = 0.0;    // hysteresis band

const int           TONE_HZ = 2000;
const unsigned long BEEP_MS = 250;   // beep on / off time
const unsigned long READ_MS = 1000;  // read + report interval (conversion takes <= 750 ms)
const unsigned long RETRY_MS = 3000; // re-run bring-up this often while failing

// DS18B20 commands
#define OW_SKIP_ROM      0xCC
#define OW_CONVERT_T     0x44
#define OW_READ_SCRATCH  0xBE
#define OW_READ_POWER    0xB4

#define EEPROM_ID_ADDR   0
#define EEPROM_ID_MAXLEN 20
char deviceId[EEPROM_ID_MAXLEN + 1];

enum AlarmState { ALARM_OFF, ALARM_BEEP, ALARM_SOLID };
AlarmState state = ALARM_OFF;

OneWire oneWire(ONE_WIRE_PIN);

unsigned long lastRead = 0;
unsigned long lastBringUp = 0;
unsigned long lastBeepToggle = 0;
bool beepOn = false;

// ---------- 1-Wire transaction log (one report window) ----------
struct OwMsg {
  char    op;        // 'P' = reset + presence (data[0] = 1/0), 'W' = write, 'R' = read
  uint8_t len;
  uint8_t data[9];
};
#define OW_LOG_MAX 8
OwMsg   owLog[OW_LOG_MAX];
uint8_t owLogCount = 0;
bool    loggingEnabled = false;  // off during bring-up so its traffic doesn't fill the log

// ---------- Sensor state ----------
uint8_t rom[8];                  // ROM code found in bring-up
uint8_t scratch[9];              // last scratchpad read
bool    parasitic = false;       // power mode found in bring-up

struct BringUp {
  bool idleHigh, presence, familyOk, romCrcOk, scratchCrcOk, tempOk, pass;
} bu;

bool lastReadOk = false;

// ---------- Light sensor (HW-503) ----------
int     lightRaw = 0;            // 0..1023, average of 8 ADC samples
int     lightSpread = 0;         // max - min of those 8 samples (noise check)
int     lightMin = 1023, lightMax = 0;   // range seen since boot (cover test)
bool    lightDo = false;         // digital output level
bool    lightInRange = false, lightStable = false;

void readLight() {
  int mn = 1023, mx = 0;
  long sum = 0;
  for (uint8_t i = 0; i < 8; i++) {
    int v = analogRead(LIGHT_AO_PIN);
    sum += v;
    if (v < mn) mn = v;
    if (v > mx) mx = v;
  }
  lightRaw = sum / 8;
  lightSpread = mx - mn;
  lightDo = digitalRead(LIGHT_DO_PIN) == HIGH;
  if (lightRaw < lightMin) lightMin = lightRaw;
  if (lightRaw > lightMax) lightMax = lightRaw;
  lightInRange = lightRaw >= 8 && lightRaw <= 1015;   // pinned at a rail = open, shorted or unpowered
  lightStable  = lightSpread <= 25;                   // a floating input jumps around
}

int lightPct() {
  int p = (int)((long)lightRaw * 100 / 1023);
  return LIGHT_INVERT ? 100 - p : p;
}

// Boot-time report for the light sensor (the live flags are also sent every second)
void runLightBringUp() {
  Serial.println(F("---- LIGHT SENSOR (HW-503) ----"));
  readLight();
  Serial.print(F("  analog AO (A0): ")); Serial.print(lightRaw);
  Serial.print(F(" of 1023, noise "));   Serial.print(lightSpread);
  Serial.print(F(" -> "));
  if (!lightInRange) {
    Serial.println(F("[FAIL] pinned at a rail: AO not on A0, VCC/GND missing, or shorted"));
  } else if (!lightStable) {
    Serial.println(F("[FAIL] noisy: loose AO wire (a floating input jumps around)"));
  } else {
    Serial.println(F("[PASS]"));
  }
  Serial.print(F("  digital DO (D3): ")); Serial.println(lightDo ? F("HIGH") : F("LOW"));
  Serial.println(F("  functional test: cover the sensor with your hand; the value must change a lot."));
  Serial.println();
}

const char *stateName(AlarmState s) {
  return s == ALARM_OFF ? "OFF" : s == ALARM_BEEP ? "BEEP" : "SOLID";
}

// Next state from the current state and temperature, with hysteresis
AlarmState nextState(AlarmState s, float tF) {
  switch (s) {
    case ALARM_OFF:
      if (tF >= SOLID_ON_F) return ALARM_SOLID;
      if (tF >= BEEP_ON_F)  return ALARM_BEEP;
      return ALARM_OFF;
    case ALARM_BEEP:
      if (tF >= SOLID_ON_F)         return ALARM_SOLID;
      if (tF < BEEP_ON_F - HYST_F)  return ALARM_OFF;
      return ALARM_BEEP;
    case ALARM_SOLID:
      if (tF < BEEP_ON_F - HYST_F)  return ALARM_OFF;
      if (tF < SOLID_ON_F - HYST_F) return ALARM_BEEP;
      return ALARM_SOLID;
  }
  return ALARM_OFF;
}

// ================= 1-Wire helpers (logged) =================

void logOw(char op, const uint8_t *data, uint8_t len) {
  if (!loggingEnabled || owLogCount >= OW_LOG_MAX) return;
  OwMsg &m = owLog[owLogCount++];
  m.op = op;
  m.len = len > 9 ? 9 : len;
  memcpy(m.data, data, m.len);
}

bool owReset() {
  bool present = oneWire.reset();
  uint8_t p = present ? 1 : 0;
  logOw('P', &p, 1);
  return present;
}

void owWriteBytes(const uint8_t *data, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) oneWire.write(data[i], 0);
  logOw('W', data, len);
}

void owReadBytes(uint8_t *buf, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) buf[i] = oneWire.read();
  logOw('R', buf, len);
}

// Reset, Skip ROM, then one command byte
bool owCommand(uint8_t cmd) {
  if (!owReset()) return false;
  uint8_t out[2] = { OW_SKIP_ROM, cmd };
  owWriteBytes(out, 2);
  return true;
}

bool readScratchpad() {
  if (!owCommand(OW_READ_SCRATCH)) { memset(scratch, 0xFF, 9); return false; }
  owReadBytes(scratch, 9);
  return OneWire::crc8(scratch, 8) == scratch[8];
}

// Raw 16-bit temperature register and the converted value
int16_t rawTemp()          { return (int16_t)((scratch[1] << 8) | scratch[0]); }
float   tempC()            { return rawTemp() / 16.0f; }
uint8_t resolutionBits()   { return ((scratch[4] >> 5) & 0x03) + 9; }

// ================= Bring-up =================

void printHexByte(uint8_t b) {
  if (b < 0x10) Serial.print('0');
  Serial.print(b, HEX);
}

void printRom() {
  for (uint8_t i = 0; i < 8; i++) printHexByte(rom[i]);
}

bool runBringUp() {
  Serial.println();
  Serial.println(F("========== BRING-UP START =========="));
  loggingEnabled = false;
  memset(&bu, 0, sizeof(bu));

  // ---- Stage 1: bus level + presence ----
  Serial.println(F("---- STAGE 1: 1-Wire bus ----"));
  pinMode(ONE_WIRE_PIN, INPUT);
  delay(5);
  bu.idleHigh = digitalRead(ONE_WIRE_PIN) == HIGH;
  Serial.print(F("  line idle level: "));
  Serial.println(bu.idleHigh ? F("HIGH [PASS]") : F("LOW [FAIL]"));
  if (!bu.idleHigh) {
    Serial.println(F("         line is held low: signal shorted to GND, VCC/GND reversed,"));
    Serial.println(F("         or the sensor has no power. If the chip is warm, unplug it."));
  }
  bu.presence = oneWire.reset();
  Serial.print(F("  presence pulse: "));
  Serial.println(bu.presence ? F("YES [PASS]") : F("NO [FAIL]"));
  if (!bu.presence) {
    Serial.println(F("         nothing answered. check: signal -> D2, VCC -> 5V, GND -> GND;"));
    Serial.println(F("         try a 4.7k resistor between signal and 5V."));
    Serial.println(F("========== BRING-UP FAILED -- fix the issue above =========="));
    Serial.println(F("(will automatically retry in 3 seconds)"));
    Serial.println();
    return false;
  }

  // ---- Stage 2: identification ----
  Serial.println(F("---- STAGE 2: identify sensor ----"));
  oneWire.reset_search();
  bool found = oneWire.search(rom);
  if (!found) {
    Serial.println(F("  [FAIL] presence pulse but ROM search found no device (noisy line?)."));
  } else {
    Serial.print(F("  ROM code: ")); printRom(); Serial.println();
    bu.romCrcOk = OneWire::crc8(rom, 7) == rom[7];
    Serial.print(F("  ROM CRC: ")); Serial.println(bu.romCrcOk ? F("OK [PASS]") : F("BAD [FAIL] (corrupted bits: marginal connection or noise)"));
    bu.familyOk = rom[0] == 0x28;
    Serial.print(F("  family code 0x")); printHexByte(rom[0]);
    if (bu.familyOk) {
      Serial.println(F(" = DS18B20 [PASS]"));
    } else {
      Serial.println(F(" [FAIL] (expected 0x28; 0x10 = DS18S20, 0x22 = DS1822)"));
    }
  }

  if (!(found && bu.romCrcOk && bu.familyOk)) {
    Serial.println(F("========== BRING-UP FAILED -- fix the issue above =========="));
    Serial.println(F("(will automatically retry in 3 seconds)"));
    Serial.println();
    return false;
  }

  // ---- Stage 3: functional check ----
  Serial.println(F("---- STAGE 3: functional check (real data, not just a pulse) ----"));
  if (owCommand(OW_READ_POWER)) {
    parasitic = oneWire.read_bit() == 0;
  }
  Serial.print(F("  power mode: "));
  Serial.println(parasitic ? F("parasitic (VDD low; module should use 5V)") : F("external VDD"));

  bu.scratchCrcOk = readScratchpad();
  Serial.print(F("  scratchpad: "));
  for (uint8_t i = 0; i < 9; i++) { printHexByte(scratch[i]); Serial.print(' '); }
  Serial.print(F("CRC "));
  Serial.println(bu.scratchCrcOk ? F("OK [PASS]") : F("BAD [FAIL]"));
  if (bu.scratchCrcOk) {
    Serial.print(F("  resolution: ")); Serial.print(resolutionBits()); Serial.println(F(" bits"));
  }

  owCommand(OW_CONVERT_T);
  unsigned long t0 = millis();
  while (oneWire.read_bit() == 0 && millis() - t0 < 1000) {}  // device holds 0 while converting
  Serial.print(F("  conversion time: ")); Serial.print(millis() - t0); Serial.println(F(" ms"));

  bool crc2 = readScratchpad();
  float c = tempC();
  bu.tempOk = crc2 && rawTemp() != 0x0550 && c > -10.0f && c < 60.0f;  // 0x0550 = 85 C power-on default
  Serial.print(F("  reading: ")); Serial.print(c, 2); Serial.print(F(" C, "));
  Serial.print(c * 9.0f / 5.0f + 32.0f, 1); Serial.print(F(" F -> "));
  if (bu.tempOk) {
    Serial.println(F("[PASS] plausible"));
  } else if (rawTemp() == 0x0550) {
    Serial.println(F("[FAIL] 85 C is the power-on default: conversion never ran"));
  } else {
    Serial.println(F("[FAIL] out of range or bad CRC"));
  }

  bu.pass = bu.idleHigh && bu.presence && bu.familyOk && bu.romCrcOk && bu.scratchCrcOk && bu.tempOk;
  if (bu.pass) {
    Serial.println(F("========== BRING-UP: ALL SYSTEMS GO =========="));
  } else {
    Serial.println(F("========== BRING-UP FAILED -- fix the issue above =========="));
    Serial.println(F("(will automatically retry in 3 seconds)"));
  }
  Serial.println();
  return bu.pass;
}

// ================= Device ID (EEPROM) =================

void loadDeviceId() {
  for (uint8_t i = 0; i < EEPROM_ID_MAXLEN; i++) {
    deviceId[i] = EEPROM.read(EEPROM_ID_ADDR + i);
  }
  deviceId[EEPROM_ID_MAXLEN] = '\0';

  bool valid = (deviceId[0] != '\0' && deviceId[0] != (char)0xFF);
  for (uint8_t i = 0; valid && i < EEPROM_ID_MAXLEN && deviceId[i]; i++) {
    if (deviceId[i] < 32 || deviceId[i] > 126) valid = false;
  }

  if (!valid) {
    randomSeed(analogRead(A5));  // floating pin noise
    snprintf(deviceId, sizeof(deviceId), "uno-%04X", (unsigned)random(0, 0xFFFF));
  }
}

void saveDeviceId(const char *name) {
  uint8_t i = 0;
  for (; i < EEPROM_ID_MAXLEN && name[i]; i++) {
    char c = name[i];
    if (!isalnum((unsigned char)c)) c = '_';
    EEPROM.update(EEPROM_ID_ADDR + i, c);
    deviceId[i] = c;
  }
  EEPROM.update(EEPROM_ID_ADDR + i, 0);
  deviceId[i] = '\0';
}

void handleSerialCommands() {
  static char lineBuf[32];
  static uint8_t lineLen = 0;

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (lineLen > 0) {
        lineBuf[lineLen] = '\0';
        if (strncmp(lineBuf, "ID:", 3) == 0 && lineBuf[3] != '\0') {
          saveDeviceId(lineBuf + 3);
          Serial.print(F("{\"event\":\"id_set\",\"id\":\""));
          Serial.print(deviceId);
          Serial.println(F("\"}"));
        }
        lineLen = 0;
      }
    } else if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = c;
    }
  }
}

// ================= JSON report =================

void printBool(bool b) { Serial.print(b ? F("true") : F("false")); }

void sendReport(bool presence, bool crcOk, bool ok) {
  float c = tempC();
  float f = c * 9.0f / 5.0f + 32.0f;

  Serial.print(F("{\"id\":\""));          Serial.print(deviceId);
  Serial.print(F("\",\"uptime_ms\":"));   Serial.print(millis());

  Serial.print(F(",\"ds18b20\":{\"ok\":")); printBool(ok);
  Serial.print(F(",\"presence\":"));        printBool(presence);
  Serial.print(F(",\"crc_ok\":"));          printBool(crcOk);
  Serial.print(F(",\"rom\":\""));           printRom();
  Serial.print(F("\",\"power\":\""));       Serial.print(parasitic ? F("parasitic") : F("external"));
  Serial.print(F("\",\"scratchpad\":["));
  for (uint8_t i = 0; i < 9; i++) { if (i) Serial.print(','); Serial.print(scratch[i]); }
  Serial.print(F("]"));
  if (crcOk) {
    Serial.print(F(",\"resolution_bits\":")); Serial.print(resolutionBits());
    Serial.print(F(",\"raw_temp\":"));        Serial.print(rawTemp());
  }
  if (ok) {
    Serial.print(F(",\"temp_c\":")); Serial.print(c, 2);
    Serial.print(F(",\"temp_f\":")); Serial.print(f, 2);
  }
  Serial.print(F(",\"bringup\":{\"idle_high\":")); printBool(bu.idleHigh);
  Serial.print(F(",\"presence\":"));      printBool(bu.presence);
  Serial.print(F(",\"family_ok\":"));     printBool(bu.familyOk);
  Serial.print(F(",\"rom_crc_ok\":"));    printBool(bu.romCrcOk);
  Serial.print(F(",\"scratch_crc_ok\":")); printBool(bu.scratchCrcOk);
  Serial.print(F(",\"temp_ok\":"));       printBool(bu.tempOk);
  Serial.print(F(",\"pass\":"));          printBool(bu.pass);
  Serial.print(F("}}"));

  int swing = lightMax - lightMin;
  Serial.print(F(",\"light\":{\"raw\":"));   Serial.print(lightRaw);
  Serial.print(F(",\"pct\":"));              Serial.print(lightPct());
  Serial.print(F(",\"do\":"));               Serial.print(lightDo ? 1 : 0);
  Serial.print(F(",\"spread\":"));           Serial.print(lightSpread);
  Serial.print(F(",\"min\":"));              Serial.print(lightMin);
  Serial.print(F(",\"max\":"));              Serial.print(lightMax);
  Serial.print(F(",\"invert\":"));           printBool(LIGHT_INVERT);
  Serial.print(F(",\"bringup\":{\"in_range\":")); printBool(lightInRange);
  Serial.print(F(",\"stable\":"));           printBool(lightStable);
  Serial.print(F(",\"responds\":"));         printBool(swing >= 100);
  Serial.print(F(",\"pass\":"));             printBool(lightInRange && lightStable);
  Serial.print(F("}}"));

  Serial.print(F(",\"alarm\":{\"state\":\"")); Serial.print(stateName(state));
  Serial.print(F("\",\"beep_on_f\":"));  Serial.print(BEEP_ON_F, 1);
  Serial.print(F(",\"solid_on_f\":"));   Serial.print(SOLID_ON_F, 1);
  Serial.print(F("}"));

  Serial.print(F(",\"ow_log\":["));
  for (uint8_t i = 0; i < owLogCount; i++) {
    if (i) Serial.print(',');
    OwMsg &m = owLog[i];
    Serial.print(F("{\"op\":\"")); Serial.print(m.op); Serial.print(F("\",\"bytes\":["));
    for (uint8_t j = 0; j < m.len; j++) { if (j) Serial.print(','); Serial.print(m.data[j]); }
    Serial.print(F("]}"));
  }
  Serial.println(F("]}"));

  owLogCount = 0;  // new log window
}

// One report cycle: read the finished conversion, start the next one, report
void readCycle() {
  owLogCount = 0;
  loggingEnabled = true;

  bool presence = owReset();
  bool crcOk = false;
  if (presence) {
    uint8_t out[2] = { OW_SKIP_ROM, OW_READ_SCRATCH };
    owWriteBytes(out, 2);
    owReadBytes(scratch, 9);
    crcOk = OneWire::crc8(scratch, 8) == scratch[8];
  } else {
    memset(scratch, 0xFF, 9);
  }
  bool ok = presence && crcOk && rawTemp() != 0x0550;

  owCommand(OW_CONVERT_T);  // start the next conversion
  readLight();

  if (ok) state = nextState(state, tempC() * 9.0f / 5.0f + 32.0f);
  else    state = ALARM_OFF;

  lastReadOk = ok;
  sendReport(presence, crcOk, ok);
}

// ================= Arduino entry points =================

void setup() {
  Serial.begin(115200);
  loadDeviceId();
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LIGHT_DO_PIN, INPUT);

  Serial.print(F("{\"event\":\"boot_id\",\"id\":\"")); Serial.print(deviceId); Serial.println(F("\"}"));

  runBringUp();
  runLightBringUp();
  owCommand(OW_CONVERT_T);          // first conversion for the first report
  lastBringUp = lastRead = millis();
}

void loop() {
  handleSerialCommands();
  unsigned long now = millis();

  if (now - lastRead >= READ_MS) {
    lastRead = now;
    readCycle();
  }

  // While the sensor is failing, re-run the bring-up so a fixed cable recovers live
  if (!lastReadOk && now - lastBringUp >= RETRY_MS) {
    runBringUp();
    owCommand(OW_CONVERT_T);
    lastBringUp = lastRead = millis();
  }

  digitalWrite(LED_BUILTIN, lastReadOk ? HIGH : LOW);  // solid = sensor healthy

  // Drive the buzzer
  switch (state) {
    case ALARM_OFF:
      noTone(BUZZER_PIN);
      beepOn = false;
      break;
    case ALARM_SOLID:
      tone(BUZZER_PIN, TONE_HZ);
      break;
    case ALARM_BEEP:
      if (now - lastBeepToggle >= BEEP_MS) {
        lastBeepToggle = now;
        beepOn = !beepOn;
        if (beepOn) tone(BUZZER_PIN, TONE_HZ);
        else        noTone(BUZZER_PIN);
      }
      break;
  }
}
