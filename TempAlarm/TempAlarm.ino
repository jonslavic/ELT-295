// ELT295 Lab - Temperature alarm: HW-506 (DS18B20) + HW-508 passive buzzer
//
//   Below 77 F          -> silent
//   77 F and above      -> beeping
//   78 F and above      -> solid tone
//   Hysteresis (1 F):   solid -> beeping below 77 F, beeping -> silent below 76 F
//   (thresholds are BEEP_ON_F / SOLID_ON_F / HYST_F below)
//
// Wiring:
//   HW-506 VCC -> 5V, GND -> GND, signal -> D2
//   HW-508 S   -> D9, - -> GND, middle pin not connected (no VCC pin)
//
// Libraries: OneWire (Paul Stoffregen), DallasTemperature (Miles Burton)

#include <OneWire.h>
#include <DallasTemperature.h>

const int ONE_WIRE_PIN = 2;
const int BUZZER_PIN   = 9;

const float BEEP_ON_F  = 77.0;   // start beeping
const float SOLID_ON_F = 78.0;   // go solid
const float HYST_F     = 1.0;    // hysteresis band

const int           TONE_HZ      = 2000;
const unsigned long BEEP_MS      = 250;   // beep on / off time
const unsigned long READ_MS      = 1000;  // temperature read interval

enum AlarmState { ALARM_OFF, ALARM_BEEP, ALARM_SOLID };
AlarmState state = ALARM_OFF;

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

unsigned long lastRead = 0;
unsigned long lastBeepToggle = 0;
bool beepOn = false;

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

void setup() {
  Serial.begin(9600);
  sensors.begin();
  sensors.setWaitForConversion(false);  // don't block the loop (keeps beeping on time)
  sensors.requestTemperatures();
  pinMode(BUZZER_PIN, OUTPUT);
}

void loop() {
  unsigned long now = millis();

  // Read temperature once per interval (conversion was started last time)
  if (now - lastRead >= READ_MS) {
    lastRead = now;
    float tC = sensors.getTempCByIndex(0);
    sensors.requestTemperatures();  // start next conversion

    if (tC == DEVICE_DISCONNECTED_C) {
      state = ALARM_OFF;
      Serial.println("Sensor not found - check wiring");
    } else {
      float tF = tC * 9.0 / 5.0 + 32.0;
      state = nextState(state, tF);
      Serial.print("Temp: ");
      Serial.print(tF, 1);
      Serial.print(" F  State: ");
      Serial.println(state == ALARM_OFF ? "OFF" : state == ALARM_BEEP ? "BEEP" : "SOLID");
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
