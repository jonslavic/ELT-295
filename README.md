# ELT 295 Labs

Arduino Uno sketches for ELT 295 lab work.

## Sketches

| Folder | Purpose |
|---|---|
| `TempSensor_DS18B20/` | Reads the HW-506 (DS18B20) digital temperature sensor and prints °C/°F to the Serial Monitor |
| `Buzzer_HW508/` | Tests the HW-508 passive buzzer (S → D9, - → GND, middle pin unconnected) |
| `TempAlarm/` | DS18B20 + buzzer: beeps at 77 °F, solid at 78 °F, 1 °F hysteresis (edit the constants at the top to change) |
| `DS18B20_Diagnostic/` | Troubleshooting: reports line idle level, presence pulse and ROM code |
| `TempSensor/` | Analog TMP36 on A0 (for analog-sensor labs) |

## HW-506 (DS18B20) setup

The DS18B20 is a digital 1-Wire sensor. It does not output an analog voltage, so it connects to D2, not A0.

| Module pin | Uno pin |
|---|---|
| VCC / + | 5V |
| GND / - | GND |
| S / DQ / OUT | D2 |

Check the silkscreen on your board before powering it. If the chip gets warm, unplug it; power and ground are reversed.

### Libraries

Install in the Arduino IDE (Library Manager, Ctrl+Shift+I):

- **OneWire** (Paul Stoffregen)
- **DallasTemperature** (Miles Burton), and choose "Install all" for dependencies

### Run

1. Open `TempSensor_DS18B20/TempSensor_DS18B20.ino`.
2. Select **Tools → Board → Arduino Uno** and your COM port, then Upload.
3. Open **Tools → Serial Monitor** at **9600** baud.

Expected output: `Temp: 22.50 C / 72.50 F` once per second. Pinching the sensor should raise the reading.

### Troubleshooting

- "Sensor not found" or `-127 C`: power the module from 5V (not 3.3V), check the signal pin goes to D2, and run `DS18B20_Diagnostic`. If needed, add a 4.7 kΩ resistor between signal and 5V.
- Diagnostic shows `Presence pulse: YES` and family code `0x28`: the sensor is good.
- Garbled Serial Monitor text: set baud to 9600.
- `OneWire.h` or `DallasTemperature.h` not found: install the libraries above.

## Classroom dashboard (teacher view)

`TempAlarm_Net/` is `TempAlarm` plus one JSON line per second at 115200 baud (name, temperature, alarm state). Each student's browser forwards it to the teacher's computer.

1. **Teacher:** `py -3 server/server.py` (needs `pip install -r server/requirements.txt`), then find the LAN IP with `ipconfig` and open `http://<ip>:5000`. Allow Python through the firewall on Private networks.
2. **Student:** upload `TempAlarm_Net`, close the Serial Monitor, open `bridge/bridge.html` in Chrome or Edge, enter the teacher's URL, click **Connect**, and set a name.
3. Each board appears as a card (online/offline, °F/°C, alarm OFF/BEEP/SOLID). The SHT40 firmware in `arduino_firmware/` uses the same pipeline.

Note: `server/`, `bridge/` and `arduino_firmware/` are in `.gitignore`.
