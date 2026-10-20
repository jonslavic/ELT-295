// Diagnostic for HW-506 / DS18B20 on D2. Open Serial Monitor at 9600 baud.
// Needs only the OneWire library.

#include <OneWire.h>

const int PIN = 2;
OneWire ow(PIN);

void setup() {
  Serial.begin(9600);
}

void loop() {
  // 1) Idle level of the line with only the Uno's weak internal pull-up.
  //    HIGH = line is free to float high. LOW = something (LED, short, wrong pin) pulls it down.
  pinMode(PIN, INPUT_PULLUP);
  delay(5);
  Serial.print("Idle level (internal pull-up): ");
  Serial.println(digitalRead(PIN) ? "HIGH" : "LOW");

  // 2) 1-Wire reset: returns 1 if a device answers with a presence pulse.
  uint8_t present = ow.reset();
  Serial.print("Presence pulse: ");
  Serial.println(present ? "YES (sensor answered)" : "NO");

  // 3) If something answered, try to read its ROM address.
  if (present) {
    byte addr[8];
    ow.reset_search();
    if (ow.search(addr)) {
      Serial.print("ROM: ");
      for (int i = 0; i < 8; i++) {
        if (addr[i] < 16) Serial.print('0');
        Serial.print(addr[i], HEX);
        Serial.print(' ');
      }
      Serial.println();
      Serial.println(addr[0] == 0x28 ? "Family code 0x28 = DS18B20" : "Unexpected family code");
    }
  }
  Serial.println("---");
  delay(1500);
}
