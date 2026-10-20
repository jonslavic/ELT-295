// ELT295 Lab - Networked temperature alarm: HW-506 (DS18B20) + HW-508 passive buzzer
//
// Same alarm behavior as TempAlarm, plus one JSON line per second on Serial
// (115200 baud) that bridge/bridge.html forwards to the teacher's dashboard.
//
//   Below 77 F          -> silent
//   77 F and above      -> beeping
//   78 F and above      -> solid tone
//   Hysteresis (1 F):   solid -> beeping below 77 F, beeping -> silent below 76 F
//
// Naming your board: in the Serial Monitor (or the bridge page) send  ID:YourName
//   The name is saved to EEPROM and survives power cycles.
//   Unnamed boards appear on the dashboard as uno-XXXX.
//
// Wiring:
//   HW-506 VCC -> 5V, GND -> GND, signal -> D2
//   HW-508 S   -> D9, - -> GND, middle pin not connected (no VCC pin)
//
// Libraries: OneWire (Paul Stoffregen), DallasTemperature (Miles Burton)

#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>

const int ONE_WIRE_PIN = 2;
const int BUZZER_PIN   = 9;

const float BEEP_ON_F  = 77.0;   // start beeping
const float SOLID_ON_F = 78.0;   // go solid
const float HYST_F     = 1.0;    // hysteresis band

const int           TONE_HZ = 2000;
const unsigned long BEEP_MS = 250;   // beep on / off time
const unsigned long READ_MS = 1000;  // temperature read + report interval

#define EEPROM_ID_ADDR   0
#define EEPROM_ID_MAXLEN 20
char deviceId[EEPROM_ID_MAXLEN + 1];

enum AlarmState { ALARM_OFF, ALARM_BEEP, ALARM_SOLID };
AlarmState state = ALARM_OFF;

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

unsigned long lastRead = 0;
unsigned long lastBeepToggle = 0;
bool beepOn = false;

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

void sendReport(bool ok, float tC, float tF) {
  Serial.print(F("{\"id\":\""));     Serial.print(deviceId);
  Serial.print(F("\",\"uptime_ms\":")); Serial.print(millis());
  Serial.print(F(",\"ds18b20\":{\"ok\":")); Serial.print(ok ? F("true") : F("false"));
  if (ok) {
    Serial.print(F(",\"temp_c\":")); Serial.print(tC, 2);
    Serial.print(F(",\"temp_f\":")); Serial.print(tF, 2);
  }
  Serial.print(F("},\"alarm\":{\"state\":\"")); Serial.print(stateName(state));
  Serial.print(F("\",\"beep_on_f\":"));  Serial.print(BEEP_ON_F, 1);
  Serial.print(F(",\"solid_on_f\":"));   Serial.print(SOLID_ON_F, 1);
  Serial.println(F("}}"));
}

// ================= Arduino entry points =================

void setup() {
  Serial.begin(115200);
  loadDeviceId();
  sensors.begin();
  sensors.setWaitForConversion(false);  // don't block the loop (keeps beeping on time)
  sensors.requestTemperatures();
  pinMode(BUZZER_PIN, OUTPUT);

  Serial.print(F("{\"event\":\"boot_id\",\"id\":\"")); Serial.print(deviceId); Serial.println(F("\"}"));
}

void loop() {
  handleSerialCommands();
  unsigned long now = millis();

  // Read temperature once per interval (conversion was started last time)
  if (now - lastRead >= READ_MS) {
    lastRead = now;
    float tC = sensors.getTempCByIndex(0);
    sensors.requestTemperatures();  // start next conversion

    if (tC == DEVICE_DISCONNECTED_C) {
      state = ALARM_OFF;
      sendReport(false, 0, 0);
    } else {
      float tF = tC * 9.0 / 5.0 + 32.0;
      state = nextState(state, tF);
      sendReport(true, tC, tF);
    }
  }

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
