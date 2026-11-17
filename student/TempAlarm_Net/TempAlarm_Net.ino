// ELT295 Lab - Networked temperature alarm with 1-Wire bring-up:
//              HW-506 (DS18B20) + HW-508 passive buzzer + HW-496 microphone
//              + Grove LIS3DHTR 3-axis accelerometer (I2C)
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
//   Saved to EEPROM. Every board also has a permanent hardware ID (uno-XXXXXXXX), made the
//   first time it runs this sketch; the dashboard keeps one card per hardware ID, so a rename
//   updates the same card. Unnamed boards show their hardware ID as the name.
//
// Wiring:
//   HW-506 VCC -> 5V, GND -> GND, signal -> D2
//   HW-508 S   -> D9, - -> GND, middle pin not connected (no VCC pin)
//   HW-496 microphone module: VCC -> 5V, GND -> GND, AO -> A0, DO -> D3
//   Grove LIS3DHTR (Grove cable): VCC (red) -> 5V, GND (black) -> GND,
//                                 SDA (white) -> A4, SCL (yellow) -> A5
//   Exactly one DS18B20 on the bus (commands use Skip ROM 0xCC).
//
// HW-496: electret microphone + amplifier. AO is an audio waveform riding on a DC bias
//   (about mid-supply, ~512 counts). The loop samples it continuously; each report sends
//   the bias, min/max, peak-to-peak swing (loudness) and the loudest swing since boot.
//   DO is the module's comparator: it flips when the sound crosses the level set by the
//   blue pot (the sketch counts the flips). "Responds" turns PASS once the peak-to-peak
//   has hit 100+ counts (clap near the mic); "DO works" once DO has changed state.
//
// LIS3DHTR: 3-axis accelerometer on I2C (address 0x19, or 0x18 if its SDO pin is tied low),
//   driven register by register with Wire so the I2C traffic can be shown to the student.
//   Set to +/-16 g, high-resolution (12-bit), 12 mg per count: a bang on the table easily
//   passes 2 g (+/-2 g would clip it flat at 2 g). It measures 200x per second into its own
//   32-sample FIFO, and the loop empties the FIFO every 10 ms, so no sample is lost, even
//   while the Uno is busy sending its report. A bang lasts only a few ms, so each graph
//   point is the most extreme sample of its 100 ms (furthest from 1 g), not just whichever
//   sample happened to come last. Every report also carries the smallest and largest |a|
//   of the second.
//   Bring-up: bus scan, WHO_AM_I (must read 0x33), config written and read back, and a
//   functional check (|a| about 1 g while still). Shake it once to pass "moved".
//
// Library: OneWire (Paul Stoffregen). Wire and EEPROM come with the Arduino IDE.

#include <OneWire.h>
#include <EEPROM.h>
#include <Wire.h>

const int ONE_WIRE_PIN = 2;
const int BUZZER_PIN   = 9;
const int MIC_AO_PIN = A0;     // HW-496 analog output (audio riding on a DC bias)
const int MIC_DO_PIN = 3;      // HW-496 digital output (comparator; trip point set by the blue pot)

const float BEEP_ON_F  = 77.0;   // start beeping
const float SOLID_ON_F = 80.0;   // go solid
const float HYST_F     = 0.0;    // hysteresis band

const int           TONE_HZ = 2000;
const unsigned long BEEP_MS = 250;   // beep on / off time
const unsigned long READ_MS = 1000;  // read + report interval (conversion takes <= 750 ms)
const unsigned long RETRY_MS = 3000; // re-run bring-up this often while failing

// LIS3DHTR registers
const uint8_t LIS_ADDRS[2] = { 0x19, 0x18 };  // Grove default 0x19; 0x18 if SDO is tied low
#define LIS_WHO_AM_I   0x0F    // reads 0x33 on a LIS3DH
#define LIS_CTRL_REG1  0x20
#define LIS_CTRL_REG4  0x23
#define LIS_CTRL_REG5  0x24
#define LIS_OUT_X_L    0x28    // X_L, X_H, Y_L, Y_H, Z_L, Z_H follow in order
#define LIS_FIFO_CTRL  0x2E
#define LIS_FIFO_SRC   0x2F    // bits 4..0 = samples waiting in the FIFO, bit 6 = overrun
#define LIS_AUTO_INC   0x80    // set on the register address to read several in a row
#define LIS_CFG1       0x67    // 200 Hz output rate, X/Y/Z enabled
#define LIS_CFG4       0xB8    // block data update, +/-16 g, high-resolution (12 mg per count)
#define LIS_CFG5       0x40    // FIFO enabled
#define LIS_CFG_FIFO   0x80    // FIFO stream mode: keeps the newest 32 samples
const int16_t LIS_MG_PER_COUNT = 12;   // at +/-16 g, high-resolution
const int     LIS_RANGE_G = 16;
const int     LIS_RATE_HZ = 200;

const unsigned long ACCEL_POLL_MS = 10;   // empty the FIFO this often (it holds 160 ms)
const uint8_t       ACCEL_BUCKET = 20;    // samples per graph point: 20 at 200 Hz = 100 ms
#define ACCEL_HIST_MAX 12

// DS18B20 commands
#define OW_SKIP_ROM      0xCC
#define OW_CONVERT_T     0x44
#define OW_READ_SCRATCH  0xBE
#define OW_READ_POWER    0xB4

#define EEPROM_ID_ADDR   0
#define EEPROM_ID_MAXLEN 20
char deviceId[EEPROM_ID_MAXLEN + 1];   // display name (changeable with ID:Name)

// Permanent hardware ID: made once, never changes when the name does. The dashboard keys on this,
// so renaming a board updates its card instead of creating a new one.
#define EEPROM_UID_ADDR  32            // magic byte, then 4 random bytes
#define EEPROM_UID_MAGIC 0xC3
char deviceUid[13];                    // "uno-" + 8 hex digits

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

// ---------- HW-496 microphone / sound module ----------
// AO is an audio signal sitting on a DC bias (about mid-supply). Loudness = peak-to-peak
// swing over a window, so the loop samples AO continuously and each report closes one window.
int      micWinMin = 1023, micWinMax = 0;
uint32_t micWinSum = 0;
uint16_t micWinN = 0;

int      micMean = 0;            // DC bias over the last window (0..1023)
int      micMin = 0, micMax = 0; // extremes over the last window
int      micP2p = 0;             // peak-to-peak over the last window = loudness
int      micPeak = 0;            // loudest peak-to-peak seen since boot (clap test)
int      micSamples = 0;         // samples in the last window
bool     micDo = false;          // comparator output level
bool     micDoSeen = false;      // DO has changed state at least once since boot
uint16_t micDoEdges = 0;         // DO changes since boot
bool     micInRange = false;     // bias is not stuck at a rail

// ---------- LIS3DHTR accelerometer (I2C) ----------
uint8_t  accAddr = 0;                 // 0 = not found on the bus
uint8_t  accWho = 0;                  // WHO_AM_I value read in bring-up
bool     accOk = false;               // last read succeeded
int16_t  accMg[3];                    // latest X, Y, Z in mg (1000 mg = 1 g)
uint8_t  accRaw[6];                   // latest raw output registers
int16_t  accHist[ACCEL_HIST_MAX][3];  // graph points this second
uint8_t  accHistN = 0;
uint8_t  accBucketN = 0;               // samples in the current graph point
uint16_t accBucketDev = 0;             // its largest distance from 1 g (mg)
int16_t  accBucketPick[3];             // ...and the sample that had it
int32_t  accBucketSum[3];              // sum of its samples, for the average (tilt view)
int16_t  accTilt[ACCEL_HIST_MAX][3];   // average of each graph point's 100 ms, in centi-g
                                       // (smooth, for the dashboard's 3D tilt view)
uint16_t accOverruns = 0;              // FIFO overflowed (samples lost) since boot
uint16_t accWinMin = 0xFFFF, accWinMax = 0;  // smallest / largest |a| this second (mg)
uint16_t accPeak = 0;                 // largest |a| since boot (shake test)
unsigned long lastAccelRead = 0, lastAccelBringUp = 0;

struct AccelBringUp {
  bool found, whoOk, cfgOk, oneG, moved, pass;
} abu;

// I2C transaction log (the first accelerometer read of each report window)
struct I2cMsg {
  char    op;        // 'W' = write, 'R' = read
  uint8_t addr;
  uint8_t len;
  uint8_t data[6];
};
#define I2C_LOG_MAX 4
I2cMsg  i2cLog[I2C_LOG_MAX];
uint8_t i2cLogCount = 0;
bool    i2cLogging = false;

void logI2c(char op, uint8_t addr, const uint8_t *data, uint8_t len) {
  if (!i2cLogging || i2cLogCount >= I2C_LOG_MAX) return;
  I2cMsg &m = i2cLog[i2cLogCount++];
  m.op = op;
  m.addr = addr;
  m.len = len > 6 ? 6 : len;
  memcpy(m.data, data, m.len);
}

bool accWriteReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(accAddr);
  Wire.write(reg);
  Wire.write(val);
  uint8_t out[2] = { reg, val };
  logI2c('W', accAddr, out, 2);
  return Wire.endTransmission() == 0;
}

bool accReadRegs(uint8_t reg, uint8_t *buf, uint8_t len) {
  uint8_t sub = len > 1 ? (reg | LIS_AUTO_INC) : reg;
  Wire.beginTransmission(accAddr);
  Wire.write(sub);
  logI2c('W', accAddr, &sub, 1);
  if (Wire.endTransmission(false) != 0) return false;     // repeated start, then read
  if (Wire.requestFrom(accAddr, len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  logI2c('R', accAddr, buf, len);
  return true;
}

uint16_t accMagMg() {   // |a| = sqrt(x^2 + y^2 + z^2), in mg
  float x = accMg[0], y = accMg[1], z = accMg[2];
  return (uint16_t)sqrt(x * x + y * y + z * z);
}

bool readAccel() {
  if (!accAddr || !accReadRegs(LIS_OUT_X_L, accRaw, 6)) { accOk = false; return false; }
  for (uint8_t i = 0; i < 3; i++) {
    // 12-bit value, left-justified in 16 bits: shift right 4, then 12 mg per count at +/-16 g
    accMg[i] = (((int16_t)((accRaw[2 * i + 1] << 8) | accRaw[2 * i])) >> 4) * LIS_MG_PER_COUNT;
  }
  accOk = true;
  i2cLogging = false;   // log only the first read of each report window
  return true;
}

// One sample from the FIFO: update this second's numbers and the current graph point
void accelSample() {
  uint16_t m = accMagMg();
  if (m < accWinMin) accWinMin = m;
  if (m > accWinMax) accWinMax = m;
  if (m > accPeak) accPeak = m;
  if (accPeak >= 1500) abu.moved = true;          // shaken or tapped: 1.5 g or more

  // Peak-hold: the graph point keeps the sample furthest from 1 g, so a short bang shows
  uint16_t dev = m > 1000 ? m - 1000 : 1000 - m;
  if (accBucketN == 0 || dev >= accBucketDev) {
    accBucketDev = dev;
    memcpy(accBucketPick, accMg, sizeof(accMg));
  }
  for (uint8_t i = 0; i < 3; i++) accBucketSum[i] += accMg[i];
  if (++accBucketN >= ACCEL_BUCKET) {
    if (accHistN < ACCEL_HIST_MAX) {
      memcpy(accHist[accHistN], accBucketPick, sizeof(accMg));
      for (uint8_t i = 0; i < 3; i++) accTilt[accHistN][i] = accBucketSum[i] / (ACCEL_BUCKET * 10L);
      accHistN++;
    }
    accBucketSum[0] = accBucketSum[1] = accBucketSum[2] = 0;
    accBucketN = 0;
  }
}

// Called from loop(): every ACCEL_POLL_MS, read everything waiting in the FIFO
void accelTick(unsigned long now) {
  if (!accAddr || now - lastAccelRead < ACCEL_POLL_MS) return;
  lastAccelRead = now;
  uint8_t src;
  if (!accReadRegs(LIS_FIFO_SRC, &src, 1)) { accOk = false; return; }
  if (src & 0x40) accOverruns++;                  // FIFO filled up: oldest samples lost
  uint8_t n = src & 0x1F;
  for (uint8_t i = 0; i < n; i++) {
    if (!readAccel()) return;
    accelSample();
  }
}

// Close the report window: a still second (all of it 0.8..1.2 g) proves the sensor reads 1 g
void accelCloseWindow() {
  if (accWinMax > 0 && accWinMin >= 800 && accWinMax <= 1200) abu.oneG = true;
  abu.pass = abu.found && abu.whoOk && abu.cfgOk && abu.oneG;
}

void accelResetWindow() {
  accHistN = 0;
  accWinMin = 0xFFFF;
  accWinMax = 0;
  i2cLogCount = 0;
  i2cLogging = true;
}

void printHexByte(uint8_t b);

bool runAccelBringUp() {
  Serial.println(F("---- LIS3DHTR ACCELEROMETER (I2C) ----"));
  bool moved = abu.moved;
  memset(&abu, 0, sizeof(abu));
  abu.moved = moved;
  i2cLogging = false;

  // Bus scan: who ACKs at the two addresses a LIS3DH can have?
  accAddr = 0;
  for (uint8_t i = 0; i < 2 && !accAddr; i++) {
    Wire.beginTransmission(LIS_ADDRS[i]);
    if (Wire.endTransmission() == 0) accAddr = LIS_ADDRS[i];
  }
  abu.found = accAddr != 0;
  Serial.print(F("  bus scan: "));
  if (!abu.found) {
    accOk = false;
    Serial.println(F("nothing at 0x18 or 0x19 [FAIL]"));
    Serial.println(F("         check: SDA -> A4, SCL -> A5, VCC -> 5V, GND -> GND (Grove cable fully seated)."));
    Serial.println(F("(will automatically retry in 3 seconds)"));
    Serial.println();
    return false;
  }
  Serial.print(F("device at 0x")); printHexByte(accAddr); Serial.println(F(" [PASS]"));

  // Identification
  accWho = 0;
  accReadRegs(LIS_WHO_AM_I, &accWho, 1);
  abu.whoOk = accWho == 0x33;
  Serial.print(F("  WHO_AM_I: 0x")); printHexByte(accWho);
  Serial.println(abu.whoOk ? F(" = LIS3DH [PASS]") : F(" [FAIL] (expected 0x33: a different chip at this address?)"));

  // Configure, then read the settings back
  accWriteReg(LIS_CTRL_REG1, LIS_CFG1);
  accWriteReg(LIS_CTRL_REG4, LIS_CFG4);
  accWriteReg(LIS_FIFO_CTRL, 0x00);        // bypass first: clears the FIFO
  accWriteReg(LIS_CTRL_REG5, LIS_CFG5);
  accWriteReg(LIS_FIFO_CTRL, LIS_CFG_FIFO);
  uint8_t r1 = 0, r4 = 0, r5 = 0, rf = 0;
  accReadRegs(LIS_CTRL_REG1, &r1, 1);
  accReadRegs(LIS_CTRL_REG4, &r4, 1);
  accReadRegs(LIS_CTRL_REG5, &r5, 1);
  accReadRegs(LIS_FIFO_CTRL, &rf, 1);
  abu.cfgOk = r1 == LIS_CFG1 && r4 == LIS_CFG4 && (r5 & 0x40) && (rf & 0xC0) == LIS_CFG_FIFO;
  Serial.print(F("  config: CTRL_REG1=0x")); printHexByte(r1);
  Serial.print(F(" CTRL_REG4=0x")); printHexByte(r4);
  Serial.print(F(" CTRL_REG5=0x")); printHexByte(r5);
  Serial.print(F(" FIFO_CTRL=0x")); printHexByte(rf);
  Serial.println(abu.cfgOk ? F(" (200 Hz, +/-16 g, 12-bit, FIFO) [PASS]") : F(" [FAIL] (did not read back what was written)"));

  // Functional: lying still, |a| should be about 1 g (gravity)
  delay(30);   // first samples at the new rate
  if (readAccel()) {
    uint16_t m = accMagMg();
    abu.oneG = m >= 800 && m <= 1200;
    Serial.print(F("  reading: X ")); Serial.print(accMg[0]);
    Serial.print(F(" Y ")); Serial.print(accMg[1]);
    Serial.print(F(" Z ")); Serial.print(accMg[2]);
    Serial.print(F(" mg, |a| ")); Serial.print(m); Serial.print(F(" mg -> "));
    Serial.println(abu.oneG ? F("[PASS] about 1 g, as expected at rest")
                            : F("[CHECK] not about 1 g: hold the board still (passes on its own when it is)"));
  } else {
    Serial.println(F("  reading: [FAIL] the read did not complete"));
  }
  Serial.println(F("  functional test: shake or tap the board; the peak must go above 1.5 g."));
  Serial.println();
  abu.pass = abu.found && abu.whoOk && abu.cfgOk && abu.oneG;
  return abu.found && abu.whoOk && abu.cfgOk;
}

void sampleMic() {
  int v = analogRead(MIC_AO_PIN);
  micWinSum += v;
  micWinN++;
  if (v < micWinMin) micWinMin = v;
  if (v > micWinMax) micWinMax = v;

  bool d = digitalRead(MIC_DO_PIN) == HIGH;
  if (d != micDo) { micDo = d; micDoEdges++; micDoSeen = true; }
}

// Close the sampling window: turn the running totals into this second's numbers
void micCloseWindow() {
  if (micWinN > 0) {
    micMean = micWinSum / micWinN;
    micMin = micWinMin;
    micMax = micWinMax;
    micP2p = micWinMax - micWinMin;
    micSamples = micWinN;
    if (micP2p > micPeak) micPeak = micP2p;
    micInRange = micMean >= 50 && micMean <= 973;   // pinned at a rail = open, shorted or unpowered
  }
  micWinMin = 1023; micWinMax = 0; micWinSum = 0; micWinN = 0;
}

int micLevelPct() {   // 0..100, full scale = a 512-count swing (half the ADC range)
  int p = (int)((long)micP2p * 100 / 512);
  return p > 100 ? 100 : p;
}

// Boot-time report for the HW-496 (the live flags are also sent every second)
void runMicBringUp() {
  Serial.println(F("---- HW-496 MICROPHONE MODULE ----"));
  micDo = digitalRead(MIC_DO_PIN) == HIGH;
  unsigned long t0 = millis();
  while (millis() - t0 < 200) sampleMic();    // quiet-room baseline
  micCloseWindow();
  micDoSeen = false; micDoEdges = 0; micPeak = 0;
  Serial.print(F("  analog AO (A0): bias ")); Serial.print(micMean);
  Serial.print(F(" of 1023, noise "));        Serial.print(micP2p);
  Serial.print(F(" -> "));
  if (!micInRange) {
    Serial.println(F("[FAIL] pinned at a rail: AO not on A0, VCC/GND missing, or shorted"));
  } else {
    Serial.println(F("[PASS] bias is near mid-supply, as expected"));
  }
  Serial.print(F("  digital DO (D3): ")); Serial.println(micDo ? F("HIGH") : F("LOW"));
  Serial.println(F("  functional test: clap near the mic; the peak-to-peak value must jump."));
  Serial.println(F("  turn the blue pot to move the DO trip point."));
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

// Read this board's permanent ID from EEPROM, creating it the first time the board runs this sketch
void loadUid() {
  if (EEPROM.read(EEPROM_UID_ADDR) != EEPROM_UID_MAGIC) {
    // Noise from floating analog pins plus the boot-time timer: different on every board
    // (A4/A5 are the accelerometer's I2C lines, held high by pull-ups, so they add nothing)
    randomSeed(((uint32_t)analogRead(A1) << 20) ^ ((uint32_t)analogRead(A2) << 10)
               ^ analogRead(A3) ^ micros());
    for (uint8_t i = 0; i < 4; i++) EEPROM.update(EEPROM_UID_ADDR + 1 + i, random(0, 256));
    EEPROM.update(EEPROM_UID_ADDR, EEPROM_UID_MAGIC);   // magic last: a half-written ID is redone
  }
  snprintf(deviceUid, sizeof(deviceUid), "uno-%02X%02X%02X%02X",
           EEPROM.read(EEPROM_UID_ADDR + 1), EEPROM.read(EEPROM_UID_ADDR + 2),
           EEPROM.read(EEPROM_UID_ADDR + 3), EEPROM.read(EEPROM_UID_ADDR + 4));
}

void loadDeviceId() {
  for (uint8_t i = 0; i < EEPROM_ID_MAXLEN; i++) {
    deviceId[i] = EEPROM.read(EEPROM_ID_ADDR + i);
  }
  deviceId[EEPROM_ID_MAXLEN] = '\0';

  bool valid = (deviceId[0] != '\0' && deviceId[0] != (char)0xFF);
  for (uint8_t i = 0; valid && i < EEPROM_ID_MAXLEN && deviceId[i]; i++) {
    if (deviceId[i] < 32 || deviceId[i] > 126) valid = false;
  }

  if (!valid) strcpy(deviceId, deviceUid);   // no name set yet: show the hardware ID
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
  Serial.print(F("\",\"uid\":\""));       Serial.print(deviceUid);
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

  Serial.print(F(",\"mic\":{\"ok\":"));      printBool(micInRange);
  Serial.print(F(",\"bias\":"));             Serial.print(micMean);
  Serial.print(F(",\"min\":"));              Serial.print(micMin);
  Serial.print(F(",\"max\":"));              Serial.print(micMax);
  Serial.print(F(",\"p2p\":"));              Serial.print(micP2p);
  Serial.print(F(",\"level\":"));            Serial.print(micLevelPct());
  Serial.print(F(",\"peak\":"));             Serial.print(micPeak);
  Serial.print(F(",\"samples\":"));          Serial.print(micSamples);
  Serial.print(F(",\"do\":"));               Serial.print(micDo ? 1 : 0);
  Serial.print(F(",\"do_edges\":"));         Serial.print(micDoEdges);
  Serial.print(F(",\"bringup\":{\"in_range\":")); printBool(micInRange);
  Serial.print(F(",\"responds\":"));         printBool(micPeak >= 100);
  Serial.print(F(",\"do_works\":"));         printBool(micDoSeen);
  Serial.print(F(",\"pass\":"));             printBool(micInRange);
  Serial.print(F("}}"));

  Serial.print(F(",\"accel\":{\"ok\":"));  printBool(accOk);
  if (accAddr) { Serial.print(F(",\"addr\":\"0x")); printHexByte(accAddr); Serial.print('"'); }
  Serial.print(F(",\"who_am_i\":"));      Serial.print(accWho);
  if (accOk) {
    Serial.print(F(",\"x_g\":"));   Serial.print(accMg[0] / 1000.0f, 3);
    Serial.print(F(",\"y_g\":"));   Serial.print(accMg[1] / 1000.0f, 3);
    Serial.print(F(",\"z_g\":"));   Serial.print(accMg[2] / 1000.0f, 3);
    Serial.print(F(",\"mag_g\":")); Serial.print(accMagMg() / 1000.0f, 3);
    Serial.print(F(",\"raw\":["));
    for (uint8_t i = 0; i < 6; i++) { if (i) Serial.print(','); Serial.print(accRaw[i]); }
    Serial.print(']');
  }
  if (accWinMax > 0) {
    Serial.print(F(",\"min_g\":")); Serial.print(accWinMin / 1000.0f, 3);
    Serial.print(F(",\"max_g\":")); Serial.print(accWinMax / 1000.0f, 3);
  }
  Serial.print(F(",\"peak_g\":"));  Serial.print(accPeak / 1000.0f, 3);
  Serial.print(F(",\"range_g\":")); Serial.print(LIS_RANGE_G);
  Serial.print(F(",\"rate_hz\":")); Serial.print(LIS_RATE_HZ);
  Serial.print(F(",\"mg_per_count\":")); Serial.print(LIS_MG_PER_COUNT);
  Serial.print(F(",\"overruns\":")); Serial.print(accOverruns);
  Serial.print(F(",\"hist_ms\":")); Serial.print(1000L * ACCEL_BUCKET / LIS_RATE_HZ);
  Serial.print(F(",\"hist\":["));    // graph points this second, X/Y/Z in mg
  for (uint8_t i = 0; i < accHistN; i++) {
    if (i) Serial.print(',');
    Serial.print('['); Serial.print(accHist[i][0]); Serial.print(',');
    Serial.print(accHist[i][1]); Serial.print(','); Serial.print(accHist[i][2]); Serial.print(']');
  }
  Serial.print(F("],\"tilt_cg\":["));  // the same 100 ms windows averaged, in centi-g (100 = 1 g)
  for (uint8_t i = 0; i < accHistN; i++) {
    if (i) Serial.print(',');
    Serial.print('['); Serial.print(accTilt[i][0]); Serial.print(',');
    Serial.print(accTilt[i][1]); Serial.print(','); Serial.print(accTilt[i][2]); Serial.print(']');
  }
  Serial.print(F("],\"bringup\":{\"found\":")); printBool(abu.found);
  Serial.print(F(",\"who_ok\":"));   printBool(abu.whoOk);
  Serial.print(F(",\"cfg_ok\":"));   printBool(abu.cfgOk);
  Serial.print(F(",\"one_g\":"));    printBool(abu.oneG);
  Serial.print(F(",\"moved\":"));    printBool(abu.moved);
  Serial.print(F(",\"pass\":"));     printBool(abu.pass);
  Serial.print(F("}}"));

  Serial.print(F(",\"i2c_log\":["));
  for (uint8_t i = 0; i < i2cLogCount; i++) {
    if (i) Serial.print(',');
    I2cMsg &m = i2cLog[i];
    Serial.print(F("{\"op\":\"")); Serial.print(m.op);
    Serial.print(F("\",\"addr\":\"0x")); printHexByte(m.addr);
    Serial.print(F("\",\"bytes\":["));
    for (uint8_t j = 0; j < m.len; j++) { if (j) Serial.print(','); Serial.print(m.data[j]); }
    Serial.print(F("]}"));
  }
  Serial.print(']');

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
  micCloseWindow();         // this second's sound numbers
  accelCloseWindow();       // this second's motion numbers

  if (ok) state = nextState(state, tempC() * 9.0f / 5.0f + 32.0f);
  else    state = ALARM_OFF;

  lastReadOk = ok;
  sendReport(presence, crcOk, ok);
  accelResetWindow();
}

// ================= Arduino entry points =================

void setup() {
  Serial.begin(115200);
  loadUid();
  loadDeviceId();
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(MIC_DO_PIN, INPUT);
  Wire.begin();
#if defined(WIRE_HAS_TIMEOUT) || defined(ARDUINO_ARCH_AVR)   // AVR core 1.8.3+ has it
  Wire.setWireTimeout(3000, true);  // a bad I2C connection can't hang the sketch
#endif

  Serial.print(F("{\"event\":\"boot_id\",\"id\":\"")); Serial.print(deviceId);
  Serial.print(F("\",\"uid\":\"")); Serial.print(deviceUid); Serial.println(F("\"}"));

  runBringUp();
  runMicBringUp();
  runAccelBringUp();
  accelResetWindow();
  owCommand(OW_CONVERT_T);          // first conversion for the first report
  lastBringUp = lastRead = lastAccelBringUp = millis();
}

void loop() {
  handleSerialCommands();
  sampleMic();                 // keep the microphone window filling between reports
  unsigned long now = millis();
  accelTick(now);              // empty the accelerometer's FIFO (200 samples per second)

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

  // Same for the accelerometer: not found, or reads failing (cable pulled) -> try again
  if ((!accAddr || !accOk || !abu.cfgOk) && now - lastAccelBringUp >= RETRY_MS) {
    runAccelBringUp();
    lastAccelBringUp = millis();
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
