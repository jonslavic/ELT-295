// ELT295 Lab - Analog temperature sensor on A0 (Arduino Uno)
// Assumes a TMP36 (10 mV/degC, 500 mV offset at 0 degC) powered from 3.3V.
//
// Wiring:
//   Sensor pin 1 (Vs)   -> Uno 3.3V
//   Sensor pin 2 (Vout) -> Uno A0
//   Sensor pin 3 (GND)  -> Uno GND
//   Optional (better accuracy): jumper 3.3V -> AREF and set USE_AREF_3V3 to true.
//   Never connect 5V to AREF while analogReference(EXTERNAL) is not set (and
//   never apply external voltage to AREF with the default/INTERNAL reference).

const int   SENSOR_PIN   = A0;
const bool  USE_AREF_3V3 = false;   // true if 3.3V is jumpered to AREF
const float VREF         = USE_AREF_3V3 ? 3.3 : 5.0;  // ADC reference voltage
const float MV_PER_DEG_C = 10.0;    // TMP36 slope
const float OFFSET_MV    = 500.0;   // TMP36 offset at 0 degC
const int   NUM_SAMPLES  = 16;      // averaging to reduce noise

void setup() {
  Serial.begin(9600);
  if (USE_AREF_3V3) {
    analogReference(EXTERNAL);
  }
}

void loop() {
  // Average several readings
  long sum = 0;
  for (int i = 0; i < NUM_SAMPLES; i++) {
    sum += analogRead(SENSOR_PIN);
    delay(2);
  }
  float counts = sum / (float)NUM_SAMPLES;

  float volts = counts * VREF / 1023.0;
  float tempC = (volts * 1000.0 - OFFSET_MV) / MV_PER_DEG_C;
  float tempF = tempC * 9.0 / 5.0 + 32.0;

  Serial.print("ADC: ");
  Serial.print(counts, 1);
  Serial.print("  V: ");
  Serial.print(volts, 3);
  Serial.print("  Temp: ");
  Serial.print(tempC, 1);
  Serial.print(" C / ");
  Serial.print(tempF, 1);
  Serial.println(" F");

  delay(500);
}
