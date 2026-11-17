// KitSensors.h -- Lab 4: drivers for the 37-sensor kit (HiLetgo / KY-0xx modules)
//
// Pick your two sensors at the top of TempAlarm_Net.ino:
//     #define KIT_SENSOR_1  KY018      // slot 1
//     #define KIT_SENSOR_2  KY040      // slot 2
// Use KitNone for an empty slot. Only the two you pick are compiled into the Uno.
// Wiring for every module: student/KitSensors.md (it depends on the slot).
//
// Each driver reports, once per second inside the board's JSON report:
//   "text"   the reading in words      "val"    one number (for the dashboard sparkline)
//   "bar"    0-100 (optional)          "active" is it triggered right now?
//   "checks" bring-up checks: [label, true/false], or null = check it by eye
//   "raw"    the raw numbers behind the reading
// An output (LED, relay, laser, buzzer, IR LED) follows the other slot's input when you
// pick one input and one output (button -> relay, flame -> buzzer...); otherwise it runs a
// demo pattern so you can see it work.
//
// The digital modules are read with pin-change interrupts: short events (a knock, a magnet
// passing, an IR code) are caught even while the Uno is busy sending its report.

#pragma once
#include <Arduino.h>

float kitRefTempC();   // the DS18B20 reading in C, or NAN (defined in TempAlarm_Net.ino)

// ---------------- Slots: the free pins (D2, D3, D9, A0, A4, A5, D13 are taken) ----------------
struct KitPins { uint8_t slot, a, b, c, an, an2; };
//                         slot  A   B   C   analog  2nd analog (joystick Y only)
#define KIT_SLOT1 KitPins{ 1,    4,  5,  6,  A1,     A3 }
#define KIT_SLOT2 KitPins{ 2,    7, 10, 11,  A2,     A3 }

// ---------------- JSON helpers (print straight to Serial, no RAM buffers) ----------------
static bool kFirst;
static void kKey(const __FlashStringHelper *k) { Serial.print(F(",\"")); Serial.print(k); Serial.print(F("\":")); }
static void kNum(const __FlashStringHelper *k, long v) { kKey(k); Serial.print(v); }
static void kFlt(const __FlashStringHelper *k, float v, uint8_t d) { kKey(k); if (isnan(v)) Serial.print(F("null")); else Serial.print(v, d); }
static void kBool(const __FlashStringHelper *k, bool v) { kKey(k); Serial.print(v ? F("true") : F("false")); }
static void kStr(const __FlashStringHelper *k, const __FlashStringHelper *v) { kKey(k); Serial.print('"'); Serial.print(v); Serial.print('"'); }
static void kTextBegin() { kKey(F("text")); Serial.print('"'); }
static void kTextEnd() { Serial.print('"'); }
static void kSep() { if (!kFirst) Serial.print(','); kFirst = false; }
// checks: [label, true | false | null]; null = "confirm it by eye"
static void kChecksBegin() { Serial.print(F(",\"checks\":[")); kFirst = true; }
static void kCheck(const __FlashStringHelper *label, int8_t state) {
  kSep(); Serial.print(F("[\"")); Serial.print(label); Serial.print(F("\","));
  Serial.print(state > 0 ? F("true") : state == 0 ? F("false") : F("null")); Serial.print(']');
}
static void kChecksEnd() { Serial.print(']'); }
#define LOOK (-1)   // a check the student confirms by eye
// raw: {"name": value, ...}
static void kRawBegin() { Serial.print(F(",\"raw\":{")); kFirst = true; }
static void kRawNum(const __FlashStringHelper *k, long v) { kSep(); Serial.print('"'); Serial.print(k); Serial.print(F("\":")); Serial.print(v); }
static void kRawFlt(const __FlashStringHelper *k, float v, uint8_t d) { kSep(); Serial.print('"'); Serial.print(k); Serial.print(F("\":")); if (isnan(v)) Serial.print(F("null")); else Serial.print(v, d); }
static void kRawStr(const __FlashStringHelper *k, const __FlashStringHelper *v) { kSep(); Serial.print('"'); Serial.print(k); Serial.print(F("\":\"")); Serial.print(v); Serial.print('"'); }
static void kRawEnd() { Serial.print('}'); }
static void kPin(uint8_t p) {
  if (p >= A0) { Serial.print('A'); Serial.print(p - A0); } else { Serial.print('D'); Serial.print(p); }
}
static void kHex(uint32_t v, uint8_t digits) {
  Serial.print(F("0x"));
  for (int8_t i = digits - 1; i >= 0; i--) Serial.print((uint8_t)((v >> (4 * i)) & 0xF), HEX);
}
// wiring: "S->A1, middle->5V, -->GND"
static void kWirePin(const __FlashStringHelper *label, uint8_t pin) { kSep(); Serial.print(label); Serial.print(F(" -> ")); kPin(pin); }
static void kWireTxt(const __FlashStringHelper *label, const __FlashStringHelper *to) { kSep(); Serial.print(label); Serial.print(F(" -> ")); Serial.print(to); }

// Pin-change interrupt for any pin (D0-D13, A0-A5)
static void kitPcint(uint8_t pin) {
  *digitalPinToPCMSK(pin) |= _BV(digitalPinToPCMSKbit(pin));
  *digitalPinToPCICR(pin) |= _BV(digitalPinToPCICRbit(pin));
}

// 10 k NTC thermistor (B = 3950) in a divider with a 10 k resistor. Which side of the divider
// the thermistor sits on differs between boards, so work it out both ways (see KY013).
static float kNtcC(int raw, bool ntcOnGndSide) {
  if (raw <= 2 || raw >= 1021) return NAN;
  float r = ntcOnGndSide ? 10000.0f * raw / (1023.0f - raw) : 10000.0f * (1023.0f - raw) / raw;
  return 1.0f / (1.0f / 298.15f + log(r / 10000.0f) / 3950.0f) - 273.15f;
}

#define KIT_ID(kyStr, nameStr) \
  const __FlashStringHelper *ky() override { return F(kyStr); } \
  const __FlashStringHelper *name() override { return F(nameStr); }

// ============================== Base class ==============================
class KitSensor {
public:
  KitPins p;
  explicit KitSensor(KitPins pins) : p(pins) {}
  virtual const __FlashStringHelper *ky() = 0;
  virtual const __FlashStringHelper *name() = 0;
  virtual bool none() { return false; }
  virtual bool isOutput() { return false; }
  virtual void wiring() = 0;                        // prints the pin list
  virtual void begin() {}
  virtual void tick(unsigned long now) {}           // every pass of loop()
  virtual void onPinChange() {}                     // from the pin-change interrupt
  virtual bool active() { return false; }           // input: triggered right now
  virtual void drive(bool on, unsigned long now) {} // output: follow the other slot's input
  virtual void demo(unsigned long now) {}           // output: no input to follow
  virtual void closeWindow() {}                     // once per second, before the report
  virtual void fields() {}                          // ,"text":..,"val":..,"checks":[..],"raw":{..}
};

class KitNone : public KitSensor {
public:
  explicit KitNone(KitPins pins) : KitSensor(pins) {}
  KIT_ID("none", "empty slot")
  bool none() override { return true; }
  void wiring() override {}
};

// ============================== Digital switches ==============================
// One signal pin that changes level when something happens. The idle level is learned at
// boot; every change away from it is an event (debounced).
class KitSwitch : public KitSensor {
public:
  uint16_t debounceMs;
  uint8_t idle = HIGH;
  bool steady = true;
  volatile uint8_t level = HIGH;
  volatile uint16_t events = 0;
  volatile unsigned long lastEv = 0;
  uint16_t evWin = 0, evPrev = 0;
  KitSwitch(KitPins pins, uint16_t deb) : KitSensor(pins), debounceMs(deb) {}
  virtual const __FlashStringHelper *eventWord() { return F("events"); }
  void wiring() override { kWireTxt(F("-"), F("GND")); kWireTxt(F("middle"), F("5V")); kWirePin(F("S"), p.a); }
  void begin() override {
    pinMode(p.a, INPUT_PULLUP);
    uint8_t last = digitalRead(p.a), flips = 0;
    for (uint8_t i = 0; i < 50; i++) {            // 50 ms: a floating or chattering line flips
      uint8_t v = digitalRead(p.a);
      if (v != last) flips++;
      last = v;
      delay(1);
    }
    idle = level = last;
    steady = flips < 3;
    kitPcint(p.a);
  }
  void onPinChange() override {
    uint8_t v = digitalRead(p.a);
    if (v == level) return;
    level = v;
    if (v != idle) {
      unsigned long t = millis();
      if (t - lastEv >= debounceMs) { events++; lastEv = t; }
    }
  }
  bool active() override {
    noInterrupts(); uint8_t lv = level; unsigned long le = lastEv; uint16_t e = events; interrupts();
    return lv != idle || (e && millis() - le < 300);
  }
  void closeWindow() override {
    noInterrupts(); uint16_t e = events; interrupts();
    evWin = e - evPrev; evPrev = e;
  }
  void fields() override {
    noInterrupts(); uint16_t e = events; uint8_t lv = level; interrupts();
    kTextBegin(); Serial.print(lv != idle ? F("ACTIVE") : F("idle")); Serial.print(F(", "));
    Serial.print(e); Serial.print(' '); Serial.print(eventWord()); Serial.print(F(" since boot")); kTextEnd();
    kNum(F("val"), evWin);
    kBool(F("active"), active());
    kChecksBegin();
    kCheck(F("Steady idle level at boot (not floating)"), steady);
    kCheck(F("Responded: the signal changed at least once"), e > 0);
    kChecksEnd();
    kRawBegin();
    kRawStr(F("pin_now"), lv ? F("HIGH") : F("LOW"));
    kRawStr(F("idle_level"), idle ? F("HIGH") : F("LOW"));
    kRawNum(F("events_this_second"), evWin);
    kRawNum(F("events_total"), e);
    kRawEnd();
  }
};

class KY002 : public KitSwitch { public: explicit KY002(KitPins p) : KitSwitch(p, 40) {}
  KIT_ID("KY-002", "Vibration (shock) switch") const __FlashStringHelper *eventWord() override { return F("shakes"); } };
class KY003 : public KitSwitch { public: explicit KY003(KitPins p) : KitSwitch(p, 20) {}
  KIT_ID("KY-003", "Hall magnetic sensor (on/off)") const __FlashStringHelper *eventWord() override { return F("magnet passes"); } };
class KY004 : public KitSwitch { public: explicit KY004(KitPins p) : KitSwitch(p, 30) {}
  KIT_ID("KY-004", "Push button (key switch)") const __FlashStringHelper *eventWord() override { return F("presses"); } };
class KY010 : public KitSwitch { public: explicit KY010(KitPins p) : KitSwitch(p, 5) {}
  KIT_ID("KY-010", "Photo interrupter (light gate)") const __FlashStringHelper *eventWord() override { return F("beam breaks"); } };
class KY017 : public KitSwitch { public: explicit KY017(KitPins p) : KitSwitch(p, 50) {}
  KIT_ID("KY-017", "Mercury tilt switch") const __FlashStringHelper *eventWord() override { return F("tilts"); } };
class KY020 : public KitSwitch { public: explicit KY020(KitPins p) : KitSwitch(p, 50) {}
  KIT_ID("KY-020", "Ball tilt switch") const __FlashStringHelper *eventWord() override { return F("tilts"); } };
class KY021 : public KitSwitch { public: explicit KY021(KitPins p) : KitSwitch(p, 20) {}
  KIT_ID("KY-021", "Mini reed switch") const __FlashStringHelper *eventWord() override { return F("magnet passes"); } };
class KY031 : public KitSwitch { public: explicit KY031(KitPins p) : KitSwitch(p, 40) {}
  KIT_ID("KY-031", "Knock sensor") const __FlashStringHelper *eventWord() override { return F("knocks"); } };
class KY033 : public KitSwitch { public: explicit KY033(KitPins p) : KitSwitch(p, 5) {}
  KIT_ID("KY-033", "Line tracking sensor") const __FlashStringHelper *eventWord() override { return F("line changes"); }
  void wiring() override { kWireTxt(F("G"), F("GND")); kWireTxt(F("V+"), F("5V")); kWirePin(F("S"), p.a); } };
class KY032 : public KitSwitch { public: explicit KY032(KitPins p) : KitSwitch(p, 20) {}
  KIT_ID("KY-032", "Obstacle avoidance (IR)") const __FlashStringHelper *eventWord() override { return F("obstacles"); }
  void wiring() override { kWireTxt(F("GND"), F("GND")); kWireTxt(F("+"), F("5V")); kWirePin(F("OUT"), p.a);
                           kWireTxt(F("EN"), F("not connected (leave the jumper on)")); } };

// ============================== Analog sensors ==============================
// One analog pin. Sampled every 2 ms; keeps this second's min/max and the extremes since boot.
class KitAnalog : public KitSensor {
public:
  int boot = 0, last = 0, lo = 1023, hi = 0, wMin = 1023, wMax = 0, rMin = 0, rMax = 0;
  unsigned long lastUs = 0;
  explicit KitAnalog(KitPins pins) : KitSensor(pins) {}
  void wiring() override { kWireTxt(F("-"), F("GND")); kWireTxt(F("middle"), F("5V")); kWirePin(F("S"), p.an); }
  void begin() override {
    pinMode(p.an, INPUT);
    long s = 0;
    for (uint8_t i = 0; i < 16; i++) { s += analogRead(p.an); delay(2); }
    boot = last = s / 16;
  }
  void sample() {
    int v = analogRead(p.an);
    last = v;
    if (v < wMin) wMin = v;
    if (v > wMax) wMax = v;
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  void tick(unsigned long) override {
    unsigned long us = micros();
    if (us - lastUs >= 2000) { lastUs = us; sample(); }
  }
  void closeWindow() override {
    rMin = wMax ? wMin : last; rMax = wMax ? wMax : last;
    wMin = 1023; wMax = 0;
  }
  bool inRange() { return last > 5 && last < 1018; }
  void rawCommon() {
    kRawNum(F("adc_now"), last);
    kRawFlt(F("volts_now"), last * 5.0f / 1023, 2);
    kRawNum(F("min_this_second"), rMin);
    kRawNum(F("max_this_second"), rMax);
    kRawNum(F("at_boot"), boot);
    kRawNum(F("lowest_since_boot"), lo);
    kRawNum(F("highest_since_boot"), hi);
  }
  void textCounts() { Serial.print(last); Serial.print(F(" counts (")); Serial.print(last * 5.0f / 1023, 2); Serial.print(F(" V)")); }
};

class KY018 : public KitAnalog { public: explicit KY018(KitPins p) : KitAnalog(p) {}
  KIT_ID("KY-018", "Photoresistor (light)")
  bool active() override { return abs(last - boot) > 150; }
  void fields() override {
    kTextBegin(); textCounts(); Serial.print(F(", ")); Serial.print(last - boot); Serial.print(F(" from start")); kTextEnd();
    kNum(F("val"), last); kNum(F("bar"), (long)last * 100 / 1023); kBool(F("active"), active());
    kChecksBegin();
    kCheck(F("Signal not stuck at 0 V or 5 V"), inRange());
    kCheck(F("Responds: cover it or shine a light (moved 100+ counts)"), hi - lo >= 100);
    kChecksEnd();
    kRawBegin(); rawCommon(); kRawEnd();
  }
};

class KY035 : public KitAnalog { public: explicit KY035(KitPins p) : KitAnalog(p) {}
  KIT_ID("KY-035", "Analog Hall sensor (magnet strength)")
  bool active() override { return abs(last - boot) > 40; }
  void fields() override {
    int d = last - boot;
    kTextBegin(); Serial.print(d > 0 ? F("+") : F("")); Serial.print(d); Serial.print(F(" counts from rest"));
    Serial.print(abs(d) > 40 ? (d > 0 ? F(" (one pole)") : F(" (other pole)")) : F(" (no magnet)")); kTextEnd();
    kNum(F("val"), d); kNum(F("bar"), min(100L, (long)abs(d) * 100 / 400)); kBool(F("active"), active());
    kChecksBegin();
    kCheck(F("Rest level near mid-supply (300-700 counts)"), boot >= 300 && boot <= 700);
    kCheck(F("Responds to a magnet (moved 40+ counts)"), hi - boot >= 40 || boot - lo >= 40);
    kCheck(F("Both poles seen (flip the magnet)"), hi - boot >= 40 && boot - lo >= 40);
    kChecksEnd();
    kRawBegin(); rawCommon(); kRawEnd();
  }
};

// KY-013: the analog temperature sensor is a thermistor (resistance falls as it warms) in a
// divider with a 10 k resistor. Boards differ in which side the thermistor is on, so the
// temperature is worked out both ways and the one that agrees with the DS18B20 wins.
class KY013 : public KitAnalog { public: explicit KY013(KitPins p) : KitAnalog(p) {}
  KIT_ID("KY-013", "Analog temperature (thermistor)")
  int8_t side = -1;   // -1 unknown, 1 thermistor on the GND side, 0 on the 5V side
  float tempC() {
    float a = kNtcC(last, true), b = kNtcC(last, false), ref = kitRefTempC();
    if (!isnan(ref) && !isnan(a) && !isnan(b)) side = fabs(a - ref) <= fabs(b - ref);
    if (side < 0) side = !isnan(a) && a > 0 && a < 50 ? 1 : 0;
    return side ? a : b;
  }
  bool active() override { return abs(last - boot) > 20; }
  void fields() override {
    float t = tempC(), ref = kitRefTempC();
    kTextBegin(); if (isnan(t)) Serial.print(F("no reading")); else { Serial.print(t, 1); Serial.print(F(" C (")); Serial.print(t * 9 / 5 + 32, 1); Serial.print(F(" F)")); } kTextEnd();
    kFlt(F("val"), t, 1); kBool(F("active"), active());
    kChecksBegin();
    kCheck(F("Signal not stuck at 0 V or 5 V"), inRange());
    kCheck(F("Reading plausible (0-50 C)"), !isnan(t) && t > 0 && t < 50);
    kCheck(F("Within 3 C of the DS18B20"), isnan(ref) ? LOOK : (int8_t)(!isnan(t) && fabs(t - ref) <= 3));
    kCheck(F("Responds: pinch it to warm it (moved 20+ counts)"), hi - lo >= 20);
    kChecksEnd();
    kRawBegin(); rawCommon();
    kRawFlt(F("temp_if_thermistor_on_gnd_side"), kNtcC(last, true), 1);
    kRawFlt(F("temp_if_thermistor_on_5v_side"), kNtcC(last, false), 1);
    kRawStr(F("using"), side ? F("thermistor on the GND side") : F("thermistor on the 5V side"));
    kRawFlt(F("ds18b20_c"), ref, 2);
    kRawEnd();
  }
};

// KY-039: heartbeat. An IR LED shines through your fingertip onto a phototransistor; each
// heartbeat changes the light a tiny bit. The slow level is removed and beats are counted.
class KY039 : public KitAnalog { public: explicit KY039(KitPins p) : KitAnalog(p) {}
  KIT_ID("KY-039", "Heartbeat (finger pulse)")
  float slow = 0, fast = 0, amp = 0;
  bool above = false;
  unsigned long lastBeat = 0, lastSampleMs = 0;
  uint16_t beats = 0, iv[4] = {0, 0, 0, 0};
  uint8_t ivN = 0;
  void begin() override { KitAnalog::begin(); slow = fast = boot; }
  void tick(unsigned long now) override {
    KitAnalog::tick(now);
    if (now - lastSampleMs < 10) return;           // 100 samples per second
    lastSampleMs = now;
    slow += (last - slow) / 32;                    // follows the slow level (ambient light, finger pressure)
    fast += (last - fast) / 3;                     // light smoothing
    float ac = fast - slow;
    amp = max(amp * 0.995f, fabs(ac));             // recent pulse size
    float thr = max(1.5f, amp * 0.5f);
    if (!above && ac > thr && now - lastBeat > 300) {   // a beat: at most 200 per minute
      above = true;
      if (lastBeat && now - lastBeat < 2000) { iv[ivN++ & 3] = now - lastBeat; }
      lastBeat = now;
      beats++;
    } else if (above && ac < 0) {
      above = false;
    }
  }
  float bpm() {
    if (!lastBeat || millis() - lastBeat > 3000) return NAN;
    uint32_t s = 0; uint8_t n = 0;
    for (uint8_t i = 0; i < 4; i++) if (iv[i]) { s += iv[i]; n++; }
    return n ? 60000.0f * n / s : NAN;
  }
  bool active() override { return millis() - lastBeat < 150; }
  void fields() override {
    float b = bpm();
    kTextBegin(); if (isnan(b)) Serial.print(F("no pulse yet: rest a fingertip between the LED and the sensor, keep still"));
                  else { Serial.print(b, 0); Serial.print(F(" beats per minute")); } kTextEnd();
    kFlt(F("val"), b, 0); kBool(F("active"), active());
    kChecksBegin();
    kCheck(F("Signal not stuck at 0 V or 5 V"), inRange());
    kCheck(F("Pulse-like signal (beats detected)"), beats >= 3);
    kCheck(F("Rate plausible (40-180 per minute)"), !isnan(b) && b >= 40 && b <= 180);
    kChecksEnd();
    kRawBegin(); rawCommon(); kRawFlt(F("pulse_size_counts"), amp, 1); kRawNum(F("beats_total"), beats); kRawEnd();
  }
};

// KY-023: joystick = two potentiometers (X on one analog pin, Y on another) plus a button.
class KY023 : public KitSensor { public: explicit KY023(KitPins p) : KitSensor(p) {}
  KIT_ID("KY-023", "Joystick (X, Y, button)")
  int x = 512, y = 512, cx = 512, cy = 512, xlo = 1023, xhi = 0, ylo = 1023, yhi = 0;
  volatile uint8_t sw = HIGH; volatile uint16_t presses = 0; volatile unsigned long lastPress = 0;
  unsigned long lastUs = 0;
  void wiring() override { kWireTxt(F("GND"), F("GND")); kWireTxt(F("+5V"), F("5V")); kWirePin(F("VRx"), p.an); kWirePin(F("VRy"), p.an2); kWirePin(F("SW"), p.a); }
  void begin() override {
    pinMode(p.a, INPUT_PULLUP);
    cx = analogRead(p.an); cy = analogRead(p.an2);
    sw = digitalRead(p.a);
    kitPcint(p.a);
  }
  void onPinChange() override {
    uint8_t v = digitalRead(p.a);
    if (v == sw) return;
    sw = v;
    unsigned long t = millis();
    if (v == LOW && t - lastPress > 30) { presses++; lastPress = t; }
  }
  void tick(unsigned long) override {
    unsigned long us = micros();
    if (us - lastUs < 5000) return;
    lastUs = us;
    x = analogRead(p.an); y = analogRead(p.an2);
    xlo = min(xlo, x); xhi = max(xhi, x); ylo = min(ylo, y); yhi = max(yhi, y);
  }
  int pct(int v, int c) { return v >= c ? (long)(v - c) * 100 / max(1, 1023 - c) : -(long)(c - v) * 100 / max(1, c); }
  bool active() override { return abs(pct(x, cx)) > 30 || abs(pct(y, cy)) > 30 || sw == LOW; }
  void fields() override {
    int px = pct(x, cx), py = pct(y, cy);
    kTextBegin(); Serial.print(F("X ")); Serial.print(px); Serial.print(F("%, Y ")); Serial.print(py); Serial.print('%');
    if (sw == LOW) Serial.print(F(", button DOWN")); kTextEnd();
    kNum(F("val"), max(abs(px), abs(py))); kNum(F("bar"), min(100, max(abs(px), abs(py)))); kBool(F("active"), active());
    kChecksBegin();
    kCheck(F("X centred at rest (400-624 counts)"), cx >= 400 && cx <= 624);
    kCheck(F("Y centred at rest (400-624 counts)"), cy >= 400 && cy <= 624);
    kCheck(F("X reaches both ends"), xlo < 100 && xhi > 923);
    kCheck(F("Y reaches both ends"), ylo < 100 && yhi > 923);
    kCheck(F("Button pressed (push the stick down)"), presses > 0);
    kChecksEnd();
    kRawBegin(); kRawNum(F("x_adc"), x); kRawNum(F("y_adc"), y); kRawNum(F("x_centre"), cx); kRawNum(F("y_centre"), cy);
    kRawNum(F("x_min"), xlo); kRawNum(F("x_max"), xhi); kRawNum(F("y_min"), ylo); kRawNum(F("y_max"), yhi);
    kRawStr(F("button"), sw == LOW ? F("pressed (LOW)") : F("up (HIGH)")); kRawNum(F("presses"), presses); kRawEnd();
  }
};

// ============================== Analog + digital (comparator) modules ==============================
// 4-pin modules: AO (the sensor's voltage) and DO (an LM393 comparator: AO vs. the blue pot).
class KitAD : public KitAnalog {
public:
  uint8_t doIdle = HIGH;
  volatile uint8_t doLevel = HIGH;
  volatile uint16_t doEdges = 0;
  explicit KitAD(KitPins pins) : KitAnalog(pins) {}
  void wiring() override { kWirePin(F("AO"), p.an); kWireTxt(F("G"), F("GND")); kWireTxt(F("+"), F("5V")); kWirePin(F("DO"), p.a); }
  void begin() override {
    KitAnalog::begin();
    pinMode(p.a, INPUT_PULLUP);
    doIdle = doLevel = digitalRead(p.a);
    kitPcint(p.a);
  }
  void onPinChange() override {
    uint8_t v = digitalRead(p.a);
    if (v == doLevel) return;
    doLevel = v;
    doEdges++;
  }
  bool doActive() { return doLevel != doIdle; }
  bool active() override { return doActive() || abs(last - boot) > 50; }
  void doChecks() { kCheck(F("DO flipped (turn the blue pot slowly, or trigger the sensor)"), doEdges > 0); }
  void doRaw() {
    kRawStr(F("do_now"), doLevel ? F("HIGH") : F("LOW"));
    kRawStr(F("do_at_boot"), doIdle ? F("HIGH") : F("LOW"));
    kRawNum(F("do_changes"), doEdges);
  }
  void standardFields(const __FlashStringHelper *respondsLabel, int respondsCounts) {
    kTextBegin(); textCounts(); Serial.print(F(", ")); Serial.print(last - boot); Serial.print(F(" from start, DO "));
    Serial.print(doLevel ? F("HIGH") : F("LOW")); kTextEnd();
    kNum(F("val"), last); kNum(F("bar"), (long)last * 100 / 1023); kBool(F("active"), active());
    kChecksBegin();
    kCheck(F("AO not stuck at 0 V or 5 V"), inRange());
    kCheck(respondsLabel, hi - lo >= respondsCounts);
    doChecks();
    kChecksEnd();
    kRawBegin(); rawCommon(); doRaw(); kRawEnd();
  }
};

class KY024 : public KitAD { public: explicit KY024(KitPins p) : KitAD(p) {}
  KIT_ID("KY-024", "Linear Hall sensor (magnet, AO + DO)")
  void fields() override { standardFields(F("Responds to a magnet (AO moved 40+ counts)"), 40); } };
class KY025 : public KitAD { public: explicit KY025(KitPins p) : KitAD(p) {}
  KIT_ID("KY-025", "Reed switch module (magnet, AO + DO)")
  void fields() override { standardFields(F("Responds to a magnet (AO moved 100+ counts)"), 100); } };
class KY026 : public KitAD { public: explicit KY026(KitPins p) : KitAD(p) {}
  KIT_ID("KY-026", "Flame sensor (infrared)")
  void fields() override { standardFields(F("Responds: a TV remote or lighter flame nearby moves AO 50+ counts"), 50); } };
class KY036 : public KitAD { public: explicit KY036(KitPins p) : KitAD(p) {}
  KIT_ID("KY-036", "Metal touch sensor")
  void fields() override { standardFields(F("Responds: touch the wire tip (AO moved 50+ counts)"), 50); } };

// KY-028: a thermistor divider (like KY-013) plus a comparator, so it has a temperature on AO
// and a "too hot" switch on DO set by the pot.
class KY028 : public KitAD { public: explicit KY028(KitPins p) : KitAD(p) {}
  KIT_ID("KY-028", "Digital temperature (thermistor + comparator)")
  int8_t side = -1;
  float tempC() {
    float a = kNtcC(last, true), b = kNtcC(last, false), ref = kitRefTempC();
    if (!isnan(ref) && !isnan(a) && !isnan(b)) side = fabs(a - ref) <= fabs(b - ref);
    if (side < 0) side = !isnan(a) && a > 0 && a < 50 ? 1 : 0;
    return side ? a : b;
  }
  void fields() override {
    float t = tempC(), ref = kitRefTempC();
    kTextBegin(); if (isnan(t)) Serial.print(F("no reading")); else { Serial.print(t, 1); Serial.print(F(" C")); }
    Serial.print(F(", DO ")); Serial.print(doLevel ? F("HIGH") : F("LOW")); kTextEnd();
    kFlt(F("val"), t, 1); kBool(F("active"), doActive());
    kChecksBegin();
    kCheck(F("AO not stuck at 0 V or 5 V"), inRange());
    kCheck(F("Within 3 C of the DS18B20 (roughly: the thermistor is not calibrated)"), isnan(ref) ? LOOK : (int8_t)(!isnan(t) && fabs(t - ref) <= 3));
    kCheck(F("Responds: pinch the black bead to warm it (moved 20+ counts)"), hi - lo >= 20);
    doChecks();
    kChecksEnd();
    kRawBegin(); rawCommon(); doRaw();
    kRawFlt(F("temp_if_thermistor_on_gnd_side"), kNtcC(last, true), 1);
    kRawFlt(F("temp_if_thermistor_on_5v_side"), kNtcC(last, false), 1);
    kRawFlt(F("ds18b20_c"), ref, 2);
    kRawEnd();
  }
};

// KY-037 / KY-038: microphone modules, the same idea as the HW-496 from Lab 2. Sampled as fast
// as the loop allows; loudness = peak-to-peak swing over the second.
class KitSound : public KitAD { public: explicit KitSound(KitPins p) : KitAD(p) {}
  int peak = 0;
  void tick(unsigned long) override { sample(); }
  bool active() override { return doActive() || (wMax > wMin && wMax - wMin > 100); }
  void closeWindow() override { KitAD::closeWindow(); if (rMax - rMin > peak) peak = rMax - rMin; }
  void fields() override {
    int p2p = rMax - rMin;
    kTextBegin(); Serial.print(F("loudness ")); Serial.print(p2p); Serial.print(F(" counts peak-to-peak (loudest ")); Serial.print(peak);
    Serial.print(F("), DO ")); Serial.print(doLevel ? F("HIGH") : F("LOW")); kTextEnd();
    kNum(F("val"), p2p); kNum(F("bar"), min(100L, (long)p2p * 100 / 512)); kBool(F("active"), active());
    kChecksBegin();
    kCheck(F("AO bias near mid-supply (not at a rail)"), boot >= 50 && boot <= 973);
    kCheck(F("Responds to a clap (swing 100+ counts)"), peak >= 100);
    doChecks();
    kChecksEnd();
    kRawBegin(); rawCommon(); kRawNum(F("loudest_swing"), peak); doRaw(); kRawEnd();
  }
};
class KY037 : public KitSound { public: explicit KY037(KitPins p) : KitSound(p) {} KIT_ID("KY-037", "Big sound sensor (large microphone)") };
class KY038 : public KitSound { public: explicit KY038(KitPins p) : KitSound(p) {} KIT_ID("KY-038", "Small sound sensor (small microphone)") };

// ============================== Special inputs ==============================
// KY-040: rotary encoder. Two switches (CLK, DT) open and close a quarter-step apart as you turn
// (quadrature): which one changes first gives the direction. Plus a push button (SW).
class KY040 : public KitSensor { public: explicit KY040(KitPins p) : KitSensor(p) {}
  KIT_ID("KY-040", "Rotary encoder")
  volatile int16_t steps = 0;           // quadrature transitions (4 per click on most KY-040s)
  volatile uint8_t ab = 0, sw = HIGH;
  volatile uint16_t presses = 0, transitions = 0;
  volatile unsigned long lastMove = 0, lastPress = 0;
  bool idleHigh = false;
  void wiring() override { kWirePin(F("CLK"), p.a); kWirePin(F("DT"), p.b); kWirePin(F("SW"), p.c); kWireTxt(F("+"), F("5V")); kWireTxt(F("GND"), F("GND")); }
  void begin() override {
    pinMode(p.a, INPUT_PULLUP); pinMode(p.b, INPUT_PULLUP); pinMode(p.c, INPUT_PULLUP);
    ab = (digitalRead(p.a) << 1) | digitalRead(p.b);
    idleHigh = ab == 3;
    sw = digitalRead(p.c);
    kitPcint(p.a); kitPcint(p.b); kitPcint(p.c);
  }
  void onPinChange() override {
    static const int8_t dir[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };
    uint8_t now = (digitalRead(p.a) << 1) | digitalRead(p.b);
    if (now != ab) {
      int8_t d = dir[(ab << 2) | now];
      steps += d; transitions++;
      ab = now;
      lastMove = millis();
    }
    uint8_t s = digitalRead(p.c);
    if (s != sw) {
      sw = s;
      unsigned long t = millis();
      if (s == LOW && t - lastPress > 30) { presses++; lastPress = t; }
    }
  }
  bool active() override { return sw == LOW || millis() - lastMove < 300; }
  void fields() override {
    noInterrupts(); int16_t st = steps; uint16_t pr = presses, tr = transitions; uint8_t s = sw, a = ab; interrupts();
    kTextBegin(); Serial.print(F("position ")); Serial.print(st / 4); Serial.print(F(" clicks, button "));
    Serial.print(s == LOW ? F("DOWN") : F("up")); Serial.print(F(", ")); Serial.print(pr); Serial.print(F(" presses")); kTextEnd();
    kNum(F("val"), st / 4); kBool(F("active"), active());
    kChecksBegin();
    kCheck(F("CLK and DT both HIGH at rest (pull-ups working)"), idleHigh);
    kCheck(F("Turned: the position changed"), tr > 0);
    kCheck(F("Clockwise counts up (if not, swap CLK and DT)"), LOOK);
    kCheck(F("Button pressed (push the knob)"), pr > 0);
    kChecksEnd();
    kRawBegin(); kRawNum(F("quadrature_steps"), st); kRawNum(F("transitions"), tr);
    kRawNum(F("clk"), a >> 1); kRawNum(F("dt"), a & 1); kRawStr(F("sw"), s ? F("HIGH") : F("LOW")); kRawNum(F("presses"), pr); kRawEnd();
  }
};

// KY-022: infrared receiver (38 kHz). Its output goes LOW while it sees IR flashing at 38 kHz.
// Decodes the NEC protocol that many TV remotes (and the KY-005 code below) use: a 9 ms
// start mark, a 4.5 ms space, then 32 bits where a short space is 0 and a long space is 1.
class KY022 : public KitSensor { public: explicit KY022(KitPins p) : KitSensor(p) {}
  KIT_ID("KY-022", "Infrared receiver (remote control)")
  volatile uint8_t level = HIGH, state = 0, nbits = 0;
  volatile uint32_t data = 0, lastCode = 0;
  volatile uint16_t edges = 0, frames = 0, repeats = 0, bad = 0;
  volatile unsigned long lastEdgeUs = 0, lastFrameMs = 0;
  uint16_t framesPrev = 0, framesWin = 0;
  bool idleHigh = false;
  void wiring() override { kWireTxt(F("-"), F("GND")); kWireTxt(F("middle"), F("5V")); kWirePin(F("S"), p.a); }
  void begin() override {
    pinMode(p.a, INPUT_PULLUP);
    delay(5);
    idleHigh = digitalRead(p.a) == HIGH;
    level = digitalRead(p.a);
    lastEdgeUs = micros();
    kitPcint(p.a);
  }
  static bool near(unsigned long d, unsigned long target) { return d > target * 2 / 3 && d < target * 4 / 3; }
  void onPinChange() override {
    uint8_t v = digitalRead(p.a);
    if (v == level) return;
    unsigned long us = micros(), d = us - lastEdgeUs;
    lastEdgeUs = us;
    uint8_t was = level;       // the level that just ended, and lasted d microseconds
    level = v;
    edges++;
    switch (state) {
      case 0: if (was == LOW && near(d, 9000)) state = 1; break;                 // start mark
      case 1:
        if (was == HIGH && near(d, 4500)) { state = 2; nbits = 0; data = 0; }  // start space
        else if (was == HIGH && near(d, 2250)) { repeats++; lastFrameMs = millis(); state = 0; }  // "key held"
        else state = 0;
        break;
      case 2: state = (was == LOW && d > 300 && d < 900) ? 3 : 0; break;          // bit mark
      case 3:                                                                      // bit space
        if (was == HIGH && d > 300 && d < 900) { nbits++; state = 2; }
        else if (was == HIGH && d > 1300 && d < 2000) { data |= 1UL << nbits; nbits++; state = 2; }
        else { state = 0; bad++; break; }
        if (nbits == 32) {
          state = 0;
          if ((uint8_t)(data >> 16) == (uint8_t)~(data >> 24)) { lastCode = data; frames++; lastFrameMs = millis(); }
          else bad++;
        }
        break;
    }
  }
  bool active() override { return frames + repeats && millis() - lastFrameMs < 300; }
  void closeWindow() override { framesWin = frames - framesPrev; framesPrev = frames; }
  void fields() override {
    noInterrupts(); uint32_t c = lastCode; uint16_t f = frames, e = edges, r = repeats, b = bad; interrupts();
    uint8_t addr = c & 0xFF, cmd = (c >> 16) & 0xFF;
    kTextBegin();
    if (!f) { Serial.print(e ? F("IR seen but no NEC code yet: try another remote") : F("no IR yet: point a TV remote at it and press a button")); }
    else { Serial.print(F("address ")); kHex(addr, 2); Serial.print(F(", command ")); kHex(cmd, 2); Serial.print(F(" (")); Serial.print(f); Serial.print(F(" codes)")); }
    kTextEnd();
    kNum(F("val"), framesWin); kBool(F("active"), active());
    kChecksBegin();
    kCheck(F("Output idles HIGH (receiver powered)"), idleHigh);
    kCheck(F("IR light seen (the output pulsed)"), e > 0);
    kCheck(F("Decoded a valid NEC code"), f > 0);
    kChecksEnd();
    kRawBegin(); kRawNum(F("edges"), e); kRawNum(F("codes"), f); kRawNum(F("repeat_codes"), r); kRawNum(F("rejected"), b);
    kSep(); Serial.print(F("\"last_32_bits\":\"")); kHex(c, 8); Serial.print('"');
    kRawNum(F("address"), addr); kRawNum(F("command"), cmd); kRawEnd();
  }
};

// KY-015: DHT11 temperature + humidity. One data wire, its own timing protocol: the Uno pulls
// the line LOW for 20 ms to wake it; the DHT11 answers LOW 80 us + HIGH 80 us, then sends
// 40 bits. Each bit is a LOW then a HIGH: a short HIGH is 0, a long HIGH is 1. Read every 2 s.
class KY015 : public KitSensor { public: explicit KY015(KitPins p) : KitSensor(p) {}
  KIT_ID("KY-015", "DHT11 temperature + humidity")
  uint8_t d[5] = {0, 0, 0, 0, 0};
  bool answered = false, sumOk = false, everOk = false;
  uint16_t reads = 0, fails = 0;
  unsigned long lastRead = 0;
  void wiring() override { kWirePin(F("S"), p.a); kWireTxt(F("middle (+)"), F("5V")); kWireTxt(F("-"), F("GND")); }
  void begin() override { pinMode(p.a, INPUT_PULLUP); lastRead = millis(); }
  // Count loop passes while the line stays at `level` (a relative time, no timer needed)
  static uint16_t pulse(volatile uint8_t *in, uint8_t mask, bool level) {
    uint16_t n = 0;
    while (((*in & mask) != 0) == level) if (++n >= 3000) return 0;
    return n;
  }
  void read() {
    uint8_t mask = digitalPinToBitMask(p.a);
    volatile uint8_t *in = portInputRegister(digitalPinToPort(p.a));
    uint8_t b[5] = {0, 0, 0, 0, 0};
    pinMode(p.a, OUTPUT); digitalWrite(p.a, LOW);
    delay(20);
    uint8_t sreg = SREG; cli();                     // ~5 ms of tight timing
    pinMode(p.a, INPUT_PULLUP);
    bool ok = pulse(in, mask, true) && pulse(in, mask, false) && pulse(in, mask, true);
    answered = ok;
    for (uint8_t i = 0; ok && i < 40; i++) {
      uint16_t lo = pulse(in, mask, false), hi = pulse(in, mask, true);
      if (!lo || !hi) { ok = false; break; }
      if (hi > lo) b[i / 8] |= 0x80 >> (i % 8);      // long HIGH = 1
    }
    SREG = sreg;
    reads++;
    sumOk = ok && (uint8_t)(b[0] + b[1] + b[2] + b[3]) == b[4];
    if (sumOk) { memcpy(d, b, 5); everOk = true; } else fails++;
  }
  void tick(unsigned long now) override { if (now - lastRead >= 2000) { lastRead = now; read(); } }
  float hum() { return everOk ? d[0] + d[1] / 10.0f : NAN; }
  float temp() { return everOk ? (d[2] & 0x7F) + d[3] / 10.0f : NAN; }
  bool active() override { return everOk && hum() > 80; }
  void fields() override {
    float h = hum(), t = temp(), ref = kitRefTempC();
    kTextBegin(); if (!everOk) Serial.print(F("no reading yet"));
    else { Serial.print(h, 0); Serial.print(F(" %RH, ")); Serial.print(t, 1); Serial.print(F(" C")); } kTextEnd();
    kFlt(F("val"), h, 0); if (everOk) kNum(F("bar"), (long)h); kBool(F("active"), active());
    kChecksBegin();
    kCheck(F("Sensor answered (response pulse)"), answered);
    kCheck(F("Checksum OK (bytes 0-3 add up to byte 4)"), sumOk);
    kCheck(F("Humidity plausible (5-95 %)"), everOk && h >= 5 && h <= 95);
    kCheck(F("Within 3 C of the DS18B20"), isnan(ref) || !everOk ? LOOK : (int8_t)(fabs(t - ref) <= 3));
    kCheck(F("Responds: breathe on it (humidity rises)"), LOOK);
    kChecksEnd();
    kRawBegin();
    kSep(); Serial.print(F("\"bytes\":["));
    for (uint8_t i = 0; i < 5; i++) { if (i) Serial.print(','); Serial.print(d[i]); }
    Serial.print(']');
    kRawNum(F("reads"), reads); kRawNum(F("failed_reads"), fails); kRawFlt(F("ds18b20_c"), ref, 2);
    kRawEnd();
  }
};

// KY-027: magic light cup. A mercury tilt switch (S) and an LED (L) on one board. Tilt it and
// the LED fades up; stand it upright and it fades down (use two boards and pour the light).
class KY027 : public KitSwitch { public: explicit KY027(KitPins p) : KitSwitch(p, 50) {}
  KIT_ID("KY-027", "Magic light cup (tilt switch + LED)")
  int bright = 0;
  unsigned long lastFade = 0;
  const __FlashStringHelper *eventWord() override { return F("tilts"); }
  void wiring() override { kWireTxt(F("G"), F("GND")); kWireTxt(F("+"), F("5V")); kWirePin(F("S"), p.a); kWirePin(F("L"), p.b); }
  void begin() override { KitSwitch::begin(); pinMode(p.b, OUTPUT); }
  void tick(unsigned long now) override {
    if (now - lastFade < 10) return;
    lastFade = now;
    bool tilted = level != idle;
    bright = constrain(bright + (tilted ? 3 : -3), 0, 255);
    analogWrite(p.b, bright);
  }
  bool active() override { return level != idle; }
  void fields() override {
    noInterrupts(); uint16_t e = events; interrupts();
    kTextBegin(); Serial.print(level != idle ? F("tilted") : F("upright")); Serial.print(F(", LED "));
    Serial.print((long)bright * 100 / 255); Serial.print(F("%")); kTextEnd();
    kNum(F("val"), (long)bright * 100 / 255); kNum(F("bar"), (long)bright * 100 / 255); kBool(F("active"), active());
    kChecksBegin();
    kCheck(F("Switch changed (tilt the board)"), e > 0);
    kCheck(F("LED fades up when tilted, down when upright"), LOOK);
    kChecksEnd();
    kRawBegin(); kRawStr(F("switch"), level ? F("HIGH") : F("LOW")); kRawNum(F("tilts"), e); kRawNum(F("led_pwm_0_255"), bright); kRawEnd();
  }
};

// ============================== Outputs ==============================
class KitOutput : public KitSensor {
public:
  bool following = false, on = false;
  uint16_t changes = 0;
  explicit KitOutput(KitPins pins) : KitSensor(pins) {}
  bool isOutput() override { return true; }
  void followText() { Serial.print(following ? F(" (following the other sensor)") : F(" (demo pattern)")); }
};

// One pin switches the module on and off: laser, active buzzer, relay, 7-colour flashing LED
class KitOnOff : public KitOutput {
public:
  explicit KitOnOff(KitPins pins) : KitOutput(pins) {}
  virtual unsigned long demoOnMs() { return 1000; }
  virtual unsigned long demoPeriodMs() { return 2000; }
  void wiring() override { kWirePin(F("S"), p.a); kWireTxt(F("middle"), F("5V")); kWireTxt(F("-"), F("GND")); }
  void begin() override { pinMode(p.a, OUTPUT); digitalWrite(p.a, LOW); }
  void set(bool v) { if (v != on) { on = v; changes++; digitalWrite(p.a, v ? HIGH : LOW); } }
  void drive(bool v, unsigned long) override { following = true; set(v); }
  void demo(unsigned long now) override { following = false; set(now % demoPeriodMs() < demoOnMs()); }
  bool active() override { return on; }
  virtual const __FlashStringHelper *lookCheck() { return F("You can see it switch on and off"); }
  void fields() override {
    kTextBegin(); Serial.print(on ? F("ON") : F("OFF")); followText(); kTextEnd();
    kNum(F("val"), on); kBool(F("active"), on);
    kChecksBegin(); kCheck(lookCheck(), LOOK); kChecksEnd();
    kRawBegin(); kRawStr(F("pin"), on ? F("HIGH") : F("LOW")); kRawNum(F("switches"), changes); kRawEnd();
  }
};
class KY008 : public KitOnOff { public: explicit KY008(KitPins p) : KitOnOff(p) {}
  KIT_ID("KY-008", "Laser (red, 650 nm)")
  void wiring() override { kWirePin(F("S"), p.a); kWireTxt(F("middle"), F("not connected")); kWireTxt(F("-"), F("GND")); }
  const __FlashStringHelper *lookCheck() override { return F("The dot switches on and off (never look into the beam)"); } };
class KY012 : public KitOnOff { public: explicit KY012(KitPins p) : KitOnOff(p) {}
  KIT_ID("KY-012", "Active buzzer (beeps by itself)")
  unsigned long demoOnMs() override { return 80; }       // a short chirp every 3 s: it is loud
  unsigned long demoPeriodMs() override { return 3000; }
  const __FlashStringHelper *lookCheck() override { return F("You hear it beep"); } };
class KY019 : public KitOnOff { public: explicit KY019(KitPins p) : KitOnOff(p) {}
  KIT_ID("KY-019", "Relay (5 V coil)")
  void wiring() override { kWirePin(F("S"), p.a); kWireTxt(F("+"), F("5V")); kWireTxt(F("-"), F("GND")); kWireTxt(F("screw terminals"), F("nothing (never mains)")); }
  const __FlashStringHelper *lookCheck() override { return F("It clicks and its red LED lights"); } };
class KY034 : public KitOnOff { public: explicit KY034(KitPins p) : KitOnOff(p) {}
  KIT_ID("KY-034", "7-colour flashing LED")
  unsigned long demoOnMs() override { return 4000; }
  unsigned long demoPeriodMs() override { return 6000; }
  void wiring() override { kWirePin(F("S"), p.a); kWireTxt(F("middle"), F("not connected")); kWireTxt(F("-"), F("GND")); }
  const __FlashStringHelper *lookCheck() override { return F("It cycles colours by itself while ON"); } };

// Colour LEDs. Slot 1 colours on D5/D6 and slot 2 on D10/D11 can dim (PWM); the third
// (blue) pin is on/off only. While the alarm sounds, D11's dimming stops (the buzzer uses its timer).
class KitColor : public KitOutput {
public:
  uint8_t r = 0, g = 0, bl = 0;
  uint8_t step = 0;
  unsigned long lastStep = 0;
  explicit KitColor(KitPins pins) : KitOutput(pins) {}
  virtual bool hasBlue() { return true; }
  void begin() override { pinMode(p.b, OUTPUT); pinMode(p.c, OUTPUT); if (hasBlue()) pinMode(p.a, OUTPUT); show(0, 0, 0); }
  void show(uint8_t R, uint8_t G, uint8_t B) {
    if (R != r || G != g || B != bl) changes++;
    r = R; g = G; bl = B;
    analogWrite(p.b, R); analogWrite(p.c, G);
    if (hasBlue()) digitalWrite(p.a, B > 127 ? HIGH : LOW);
  }
  void drive(bool v, unsigned long) override { following = true; on = v; show(v ? 0 : 255, v ? 255 : 0, 0); }  // green when the input fires, red otherwise
  void demo(unsigned long now) override {
    following = false;
    if (now - lastStep < 700) return;
    lastStep = now;
    static const uint8_t seq[8][3] = { {255,0,0}, {255,255,0}, {0,255,0}, {0,255,255}, {0,0,255}, {255,0,255}, {255,255,255}, {0,0,0} };
    uint8_t n = hasBlue() ? 8 : 4;
    uint8_t i = step++ % n;
    if (!hasBlue()) { static const uint8_t two[4][3] = { {255,0,0}, {255,255,0}, {0,255,0}, {0,0,0} }; show(two[i][0], two[i][1], 0); }
    else show(seq[i][0], seq[i][1], seq[i][2]);
    on = r || g || bl;
  }
  bool active() override { return r || g || bl; }
  const __FlashStringHelper *colorName() {
    bool R = r > 127, G = g > 127, B = bl > 127;
    if (R && G && B) return F("WHITE");
    if (R && G) return F("YELLOW");
    if (R && B) return F("MAGENTA");
    if (G && B) return F("CYAN");
    if (R) return F("RED");
    if (G) return F("GREEN");
    if (B) return F("BLUE");
    return F("OFF");
  }
  void fields() override {
    kTextBegin(); Serial.print(colorName()); followText(); kTextEnd();
    kNum(F("val"), (r > 127) * 4 + (g > 127) * 2 + (bl > 127)); kBool(F("active"), active());
    kChecksBegin();
    kCheck(F("The LED shows the colour named here (if red and green are swapped, swap those wires)"), LOOK);
    kChecksEnd();
    kRawBegin(); kRawNum(F("red_pwm"), r); kRawNum(F("green_pwm"), g); if (hasBlue()) kRawNum(F("blue_on"), bl > 127); kRawNum(F("changes"), changes); kRawEnd();
  }
};
class KY016 : public KitColor { public: explicit KY016(KitPins p) : KitColor(p) {}
  KIT_ID("KY-016", "RGB LED (5 mm)")
  void wiring() override { kWirePin(F("R"), p.b); kWirePin(F("G"), p.c); kWirePin(F("B"), p.a); kWireTxt(F("-"), F("GND")); } };
class KY009 : public KitColor { public: explicit KY009(KitPins p) : KitColor(p) {}
  KIT_ID("KY-009", "RGB LED (SMD)")
  void wiring() override { kWirePin(F("R (via 220 ohm)"), p.b); kWirePin(F("G (via 220 ohm)"), p.c); kWirePin(F("B (via 220 ohm)"), p.a); kWireTxt(F("-"), F("GND")); } };
class KY011 : public KitColor { public: explicit KY011(KitPins p) : KitColor(p) {}
  KIT_ID("KY-011", "Two-colour LED (5 mm, red/green)")
  bool hasBlue() override { return false; }
  void wiring() override { kWireTxt(F("-"), F("GND")); kWirePin(F("middle: red (via 220 ohm)"), p.b); kWirePin(F("S: green (via 220 ohm)"), p.c); } };
class KY029 : public KitColor { public: explicit KY029(KitPins p) : KitColor(p) {}
  KIT_ID("KY-029", "Two-colour LED (3 mm, red/green)")
  bool hasBlue() override { return false; }
  void wiring() override { kWireTxt(F("-"), F("GND")); kWirePin(F("middle: red (via 220 ohm)"), p.b); kWirePin(F("S: green (via 220 ohm)"), p.c); } };

// KY-005: infrared transmitter. Sends a NEC code (the same format as most TV remotes):
// address 0x29, command = a counter. The 38 kHz flashing is made in software.
class KY005 : public KitOutput { public: explicit KY005(KitPins p) : KitOutput(p) {}
  KIT_ID("KY-005", "Infrared transmitter (IR LED)")
  uint8_t cmd = 0;
  uint16_t sent = 0;
  unsigned long lastSend = 0;
  void wiring() override { kWirePin(F("S"), p.a); kWireTxt(F("middle"), F("5V")); kWireTxt(F("-"), F("GND")); }
  void begin() override { pinMode(p.a, OUTPUT); digitalWrite(p.a, LOW); }
  void mark(uint16_t us) {               // IR LED flashing at ~38 kHz for `us` microseconds
    uint8_t mask = digitalPinToBitMask(p.a);
    volatile uint8_t *out = portOutputRegister(digitalPinToPort(p.a));
    uint8_t sreg = SREG; cli();
    for (uint16_t t = 0; t < us; t += 26) { *out |= mask; delayMicroseconds(8); *out &= ~mask; delayMicroseconds(16); }
    SREG = sreg;
  }
  void send(uint8_t address, uint8_t command) {   // ~70 ms
    uint32_t frame = address | ((uint32_t)(uint8_t)~address << 8) | ((uint32_t)command << 16) | ((uint32_t)(uint8_t)~command << 24);
    mark(9000); delayMicroseconds(4500);
    for (uint8_t i = 0; i < 32; i++) { mark(562); delayMicroseconds(frame & (1UL << i) ? 1687 : 562); }
    mark(562);
    sent++;
  }
  void drive(bool v, unsigned long now) override {    // send while the input is active
    following = true; on = v;
    if (v && now - lastSend >= 500) { lastSend = now; send(0x29, ++cmd); }
  }
  void demo(unsigned long now) override {
    following = false;
    if (now - lastSend >= 1000) { lastSend = now; on = true; send(0x29, ++cmd); }
  }
  bool active() override { return millis() - lastSend < 100; }
  void fields() override {
    kTextBegin(); Serial.print(F("sent ")); Serial.print(sent); Serial.print(F(" codes, last: address 0x29, command ")); kHex(cmd, 2); followText(); kTextEnd();
    kNum(F("val"), cmd); kBool(F("active"), active());
    kChecksBegin();
    kCheck(F("A phone camera shows the IR LED flashing (your eye can't see IR)"), LOOK);
    kCheck(F("A KY-022 receiver (another team's) decodes address 0x29"), LOOK);
    kChecksEnd();
    kRawBegin(); kRawNum(F("codes_sent"), sent); kRawNum(F("address"), 0x29); kRawNum(F("command"), cmd); kRawNum(F("carrier_hz"), 38000); kRawEnd();
  }
};

// ============================== The two slots ==============================
#ifndef KIT_SENSOR_1
#define KIT_SENSOR_1 KitNone
#endif
#ifndef KIT_SENSOR_2
#define KIT_SENSOR_2 KitNone
#endif
KIT_SENSOR_1 kit1(KIT_SLOT1);
KIT_SENSOR_2 kit2(KIT_SLOT2);

ISR(PCINT0_vect) { kit1.onPinChange(); kit2.onPinChange(); }   // D8-D13
ISR(PCINT1_vect) { kit1.onPinChange(); kit2.onPinChange(); }   // A0-A5
ISR(PCINT2_vect) { kit1.onPinChange(); kit2.onPinChange(); }   // D0-D7

static void kitPrintBoot(KitSensor &s) {
  if (s.none()) return;
  Serial.print(F("---- KIT SENSOR (slot ")); Serial.print(s.p.slot); Serial.print(F("): "));
  Serial.print(s.ky()); Serial.print(' '); Serial.print(s.name()); Serial.println(F(" ----"));
  Serial.print(F("  wiring: ")); kFirst = true; s.wiring(); Serial.println();
  Serial.println(F("  its readings and checks are on the bridge page and the dashboard."));
  Serial.println();
}

void kitBegin() {
  kit1.begin();
  kit2.begin();
  kitPrintBoot(kit1);
  kitPrintBoot(kit2);
}

// Outputs follow the other slot's input; on their own they run a demo
static void kitDriveOutput(KitSensor &out, KitSensor &other, unsigned long now) {
  if (!out.isOutput()) return;
  if (!other.none() && !other.isOutput()) out.drive(other.active(), now);
  else out.demo(now);
}

void kitLoop(unsigned long now) {
  kit1.tick(now);
  kit2.tick(now);
  kitDriveOutput(kit1, kit2, now);
  kitDriveOutput(kit2, kit1, now);
}

void kitCloseWindow() { kit1.closeWindow(); kit2.closeWindow(); }

static void kitReportOne(KitSensor &s, bool &first) {
  if (s.none()) return;
  if (!first) Serial.print(',');
  first = false;
  Serial.print(F("{\"slot\":")); Serial.print(s.p.slot);
  kStr(F("ky"), s.ky());
  kStr(F("name"), s.name());
  kStr(F("kind"), s.isOutput() ? F("output") : F("input"));
  kKey(F("wiring")); Serial.print('"'); kFirst = true; s.wiring(); Serial.print('"');
  s.fields();
  Serial.print('}');
}

// ,"kit":[{...},{...}]
void kitReport() {
  Serial.print(F(",\"kit\":["));
  bool first = true;
  kitReportOne(kit1, first);
  kitReportOne(kit2, first);
  Serial.print(']');
}
