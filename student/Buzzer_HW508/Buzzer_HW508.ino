// ELT295 Lab - HW-508 passive buzzer module on Arduino Uno
// A PASSIVE buzzer has no built-in oscillator: it only sounds when driven with a
// square wave, so use tone(). Plain digitalWrite(HIGH) gives just a click.
//
// Wiring (verify against the silkscreen on your board):
//   Module S (signal) -> Uno D9
//   Module middle pin -> not connected (leave unwired)
//   Module -  (GND)   -> Uno GND
//   The signal pin powers the buzzer; there is no VCC pin on this module.
//   2 kHz is near its resonant frequency (loudest).

const int BUZZER_PIN = 9;

void setup() {
  Serial.begin(9600);
  Serial.println("HW-508 buzzer test");
}

void loop() {
  // Beep at a few frequencies (Hz)
  const int freqs[] = {262, 330, 392, 523, 1000, 2000};  // C4, E4, G4, C5, 1k, 2k
  for (int i = 0; i < 6; i++) {
    Serial.print("Tone: ");
    Serial.print(freqs[i]);
    Serial.println(" Hz");
    tone(BUZZER_PIN, freqs[i], 300);  // 300 ms
    delay(400);
  }
  noTone(BUZZER_PIN);
  delay(1500);
}
