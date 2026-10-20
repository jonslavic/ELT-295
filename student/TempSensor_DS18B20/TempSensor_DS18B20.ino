// ELT295 Lab - HW-506 (DS18B20) digital temperature sensor on Arduino Uno
// The DS18B20 is a 1-Wire digital sensor: it does NOT produce an analog voltage,
// so it connects to a digital pin (D2 here), not A0.
//
// Wiring (verify against the silkscreen on your board before powering):
//   Module VCC / + -> Uno 5V  (3.3V failed on the test module: the on-board LED
//                              loads the signal line and the sensor was not found)
//   Module GND / - -> Uno GND
//   Module DQ / S / OUT (signal) -> Uno D2
//   Needs a ~4.7k pull-up between signal and VCC; most modules include one.
//   If the sensor is not found or reads -127 C, add one externally.
//   Troubleshooting: run ../DS18B20_Diagnostic first.
//
// Libraries (Sketch -> Include Library -> Manage Libraries):
//   "OneWire" by Paul Stoffregen
//   "DallasTemperature" by Miles Burton

#include <OneWire.h>
#include <DallasTemperature.h>

const int ONE_WIRE_PIN = 2;

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);

void setup() {
  Serial.begin(9600);
  sensors.begin();
  Serial.print("DS18B20 devices found: ");
  Serial.println(sensors.getDeviceCount());
}

void loop() {
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);

  if (tempC == DEVICE_DISCONNECTED_C) {
    Serial.println("Sensor not found - check wiring / pull-up resistor");
  } else {
    float tempF = tempC * 9.0 / 5.0 + 32.0;
    Serial.print("Temp: ");
    Serial.print(tempC, 2);
    Serial.print(" C / ");
    Serial.print(tempF, 2);
    Serial.println(" F");
  }
  delay(1000);
}
