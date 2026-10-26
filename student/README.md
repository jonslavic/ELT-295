# ELT 295: Student files

Arduino Uno sketches for the labs, plus `bridge.html`, which sends your board's data to the class dashboard.

## Sketches

| Folder | Purpose |
|---|---|
| `Buzzer_HW508/` | Tests the HW-508 passive buzzer (S → D9, - → GND, middle pin unconnected) |
| `TempSensor_DS18B20/` | Reads the HW-506 (DS18B20) temperature sensor and prints °C/°F to the Serial Monitor |
| `DS18B20_Diagnostic/` | Troubleshooting: reports line idle level, presence pulse and ROM code |
| `TempAlarm/` | DS18B20 + buzzer alarm (constants at the top set the thresholds) |
| `TempAlarm_Net/` | The class sketch: alarm + 1-Wire bring-up + HW-496 microphone, reporting to the dashboard |

## Wiring

| Part | Part pin | Uno pin |
|---|---|---|
| HW-506 (DS18B20) | VCC / + | 5V |
| | GND / - | GND |
| | S / DQ / OUT | D2 |
| HW-508 buzzer | S | D9 |
| | - | GND |
| | middle pin | not connected |
| HW-496 microphone | VCC | 5V |
| | GND | GND |
| | AO | A0 |
| | DO | D3 |

The DS18B20 is a digital 1-Wire sensor, so it goes to D2, not an analog pin. Check each board's silkscreen before powering it. If a chip gets warm, unplug it; power and ground are reversed.

## Libraries

Install in the Arduino IDE (Library Manager, Ctrl+Shift+I):

- **OneWire** (Paul Stoffregen), needed by every DS18B20 sketch
- **DallasTemperature** (Miles Burton, choose "Install all"), needed by `TempSensor_DS18B20` and `TempAlarm` only

## Upload a sketch

1. Open the sketch's `.ino` file in the Arduino IDE.
2. Select **Tools → Board → Arduino Uno** and your COM port, then Upload.
3. Open **Tools → Serial Monitor**. Baud is **9600** for the single-sensor sketches and **115200** for `TempAlarm_Net`.

## Connect to the class dashboard

1. Upload `TempAlarm_Net`, then **close the Serial Monitor** (only one program can use the USB port).
2. Download the bridge from your instructor's address, `http://<teacher-ip>:8080/bridge.html`. It already has that address filled in.
3. Double-click the saved file. It must open in **Chrome or Edge** (Firefox and Safari don't support Web Serial).
4. First time only: click **Connect** and choose your Uno's COM port. After that it reconnects by itself, including when you replug the cable.
5. Type a name and click **Set name**. It's saved on the board.
6. Find your card on the projected dashboard.

The bridge page shows your board's raw data live: the temperature sensor's scratchpad and bring-up checks, the 1-Wire bus traffic, the microphone's bias, min/max and loudness, and the raw line the Uno sent.

## Board bring-up (`TempAlarm_Net`)

The board checks itself at boot, in order:

1. **1-Wire bus:** the data line idles HIGH, and the sensor answers with a presence pulse.
2. **Identification:** ROM code, family code `0x28` (DS18B20) and ROM CRC.
3. **Function:** scratchpad CRC, a real conversion, and a plausible result.
4. **Microphone:** AO bias near mid-supply. Then clap next to it; the loudness must jump.

If the temperature sensor fails, the board retries every 3 seconds, so fixing a loose wire shows up live.

## Troubleshooting

- "Sensor not found" or `-127 C`: power the module from 5V (not 3.3V), check the signal wire goes to D2, and run `DS18B20_Diagnostic`. If needed, add a 4.7 kΩ resistor between signal and 5V.
- Diagnostic shows `Presence pulse: YES` and family code `0x28`: the sensor is good.
- Reading of 85 °C: the sensor's power-on default, so a conversion never ran.
- Garbled Serial Monitor text: match the baud rate to the sketch (see above).
- `OneWire.h` or `DallasTemperature.h` not found: install the libraries above.
- Bridge says "Failed to open serial port": close the Serial Monitor and replug the board.
- Bridge shows no bring-up panels: the board has an older sketch; re-upload `TempAlarm_Net`.
- Microphone loudness never moves: check AO is on A0 and clap right next to it. The blue pot only sets where DO trips; it doesn't change AO.
