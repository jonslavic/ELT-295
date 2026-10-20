You are writing a 4-lab series for ELT-295 (intro electronics/embedded systems, Circuit Check training context). Audience: students at lab benches, plus an instructor guide. Tone: hands-on lab manual, with direct numbered steps and plain language. Define technical terms on first use and include a short glossary at the end. Frame the whole series as a BOARD BRING-UP exercise: verify the hardware is alive, then identify it, then check that it produces sane data, before trusting anything it reports.

## What the system is
Each student builds a small networked sensor station on an Arduino Uno: a temperature alarm plus a light sensor. An Uno has no network hardware, so the data path is:

Uno (USB serial) -> student PC's browser (bridge page) -> classroom network -> instructor PC (server) -> live class dashboard (projected)

Software constraint to state up front: students install nothing except the Arduino IDE (for uploading firmware) and one IDE library. The bridge page runs in Chrome or Edge (both ship with Windows). Firefox and Safari do not support the Web Serial API it uses. Show this as a warning callout.

## Hardware per student
- Arduino Uno + USB cable
- HW-506 DS18B20 digital temperature sensor (1-Wire)
- HW-503 light sensor: 4-pin LDR (photoresistor) module with pins VCC, GND, AO (analog out), DO (digital out) and a small blue potentiometer that sets the DO threshold
- HW-508 passive buzzer
- Jumper wires (optionally a breadboard and a 4.7 kOhm resistor for troubleshooting)

## Wiring (Lab 1)
| Part | Pin | Uno |
|---|---|---|
| HW-506 | VCC / + | 5V |
| HW-506 | GND / - | GND |
| HW-506 | S / DQ / OUT | D2 |
| HW-503 | VCC | 5V |
| HW-503 | GND | GND |
| HW-503 | AO | A0 |
| HW-503 | DO | D3 |
| HW-508 | S | D9 |
| HW-508 | - | GND |
| HW-508 | middle pin | not connected (it has no VCC pin) |

Safety/quality notes to include:
- The DS18B20 is DIGITAL (1-Wire), so it goes to D2, NOT an analog pin. The HW-503's AO output is ANALOG, so it goes to A0. Contrast the two on purpose: this is a good teaching moment (digital bus vs. analog voltage).
- Check the silkscreen before powering. If a sensor chip gets warm, unplug it immediately, because power is reversed.
- Teach a pre-power visual/continuity check like a PCB bring-up.
- Use the board's own silkscreen as the source of truth for HW-503 pin order: it varies between vendors.

## Firmware (Lab 2): sketch `TempAlarm_Net/TempAlarm_Net.ino`
Library needed: OneWire (Paul Stoffregen), installed via Arduino IDE Library Manager. Board is Arduino Uno. Serial runs at 115200 baud.

Temperature + alarm behavior:
- Alarm: below 77 F silent, 77 F and above beeping, 80 F and above solid tone (constants at the top of the sketch, editable). The light sensor does not affect the alarm.
- The sketch talks to the DS18B20 directly over 1-Wire (no helper library for the sensor) so every bus transaction is visible.
- BRING-UP runs on boot and re-runs every 3 seconds while the temperature sensor is failing, so a student who fixes a loose wire watches the board recover live. The onboard LED is solid when the temperature sensor is healthy. The Serial Monitor prints:
  - Stage 1, 1-Wire bus: the data line idles HIGH, and the sensor answers with a presence pulse. (Analogous to a power/continuity check.)
  - Stage 2, identification: read the ROM code, check family code 0x28 = DS18B20, check the ROM CRC. (Is it the part we expect, and is the bus clean?)
  - Stage 3, functional: power mode, scratchpad read plus CRC, a real temperature conversion, range check. A reading of 85 C is flagged because it is the chip's power-on default (the conversion never ran). (Does it produce sane data, not just a pulse?)
  - Light sensor check (printed after the three stages): AO is within range (not pinned at 0 or 1023, which means open, shorted or unpowered) and stable (8 quick samples that agree; a floating input jumps around). Then the student runs the functional test by hand: COVER THE SENSOR with a hand and the value must swing by 100+ counts. The dashboard shows "Responds to light" as PASS once that has happened.
- Each board has a name, set by sending `ID:YourName` (Serial Monitor or the bridge page). It is saved to EEPROM and survives power cycles. Unnamed boards show as uno-XXXX.
- Once per second the board sends one JSON line containing: id, uptime, ds18b20 {ok, presence, crc_ok, rom, power, the 9 raw scratchpad bytes, resolution, raw_temp register, temp_c, temp_f, bring-up pass/fail flags}, light {raw 0-1023, pct, do pin level, noise spread, min/max since boot, bring-up flags}, alarm {state OFF/BEEP/SOLID, thresholds}, and ow_log (the 1-Wire transactions of that second).

Worked examples:
- Temperature = raw 16-bit register / 16. Example: bytes 0x6F, 0x01 give raw 367, which divided by 16 is 22.94 C = 73.29 F. Reference command bytes: 0xCC Skip ROM, 0x44 Convert T, 0xBE Read Scratchpad, 0xB4 Read Power Supply.
- Light: the Uno's 10-bit ADC returns 0-1023 for 0-5 V, so volts = raw x 5 / 1023. Example: raw 512 is 2.50 V.

Light-sensor calibration exercise (good for Lab 2): HW-503 modules differ in polarity. Some read HIGHER when covered, some LOWER. Students run the cover test, note which way it goes, and set `LIGHT_INVERT` (true/false) in the sketch so the dashboard's brightness % goes up with more light. They also turn the blue potentiometer with a small screwdriver and watch the DO pin flip at a different light level (the DO pin is a comparator output: AO compared against the pot setting). Discuss analog value vs. digital threshold.

Test steps: pinch the temperature sensor to warm it and watch the temperature rise, hear the beep at 77 F and the solid tone at 80 F, cover the light sensor and watch the value swing, then pull the DS18B20 signal wire and watch Stage 1 fail and recover when replugged.

Troubleshooting table to include (symptom -> likely cause):
- Stage 1 line idle LOW -> signal shorted to GND, VCC/GND reversed, or no power.
- No presence pulse -> signal not on D2, no 5V, no GND; try a 4.7k resistor between signal and 5V.
- ROM CRC bad -> marginal/noisy connection.
- Family code not 0x28 -> different part (0x10 = DS18S20, 0x22 = DS1822).
- Reading 85 C -> conversion never ran.
- Light AO pinned at 0 or 1023 -> AO not on A0, VCC/GND missing, or shorted.
- Light value noisy/jumping -> AO wire loose (floating input).
- Light value never changes when covered -> wrong pin, or covering does not block light (use a fingertip over the photoresistor disc).
- Brightness % goes DOWN in the light -> set LIGHT_INVERT to true.
- Garbled Serial Monitor -> set 115200 baud.
- "OneWire.h not found" -> install the library.
- Upload fails -> wrong COM port or board selected.

## Connecting to the class (Lab 3)
INSTRUCTOR one-time setup (before class):
1. Double-click `Start_Dashboard.bat` (needs Python 3 and `pip install -r server/requirements.txt` once). The dashboard opens in the instructor's browser, and the console prints the student download address, e.g. http://<instructor-ip>:5000/bridge.html.
2. Allow Python through Windows Firewall when prompted (choose Private networks). The instructor PC and all student PCs must be on the same network.
3. Project the dashboard and write the download address on the board.
Dashboard facts for the instructor guide:
- One card per board: thermometer plus semicircle gauge (F with C below, alarm thresholds shown as colored zones), a speaker icon (muted = silent, flashing waves = beeping, steady red waves = solid), a light-level bar (yellow, %), bring-up PASS/FAIL, and expandable panels (Bring-up checks, Sensor details with the labeled scratchpad, 1-Wire messages, Light sensor).
- A board that stops reporting greys out after about 6 seconds and is removed after 3 minutes (the instructor can change this with `--expire-min`).
- The instructor can run a board and bridge on the same PC.
STUDENT procedure:
1. Close the Arduino IDE Serial Monitor (only one program can hold the USB port).
2. Download the bridge file from the instructor's address (or the "Student bridge" link at the top of the dashboard). It is a single file with the instructor's address already filled in.
3. Double-click the saved file (opens in Chrome or Edge). The file must be saved and opened, NOT browsed to over http, because browsers only allow serial access from local files, localhost, or https. Explain this briefly as a "why".
4. First time only: click Connect and choose the Uno's COM port in the browser popup. After that it reconnects automatically, including when the cable is replugged.
5. Set the board name with the name field (Set name).
6. The bridge page shows live RAW DATA: the temperature sensor registers and labeled scratchpad, the bring-up checklist, the 1-Wire bus traffic with commands decoded, the light sensor's raw ADC value, voltage and DO pin, and the raw JSON line, plus an activity log of reports sent. Find your card on the projected dashboard.
Class-moment callout: as each team connects, their card appears live; have everyone cover their light sensor at once and watch the class's light bars drop. Deliverable: screenshot of the class dashboard with your card online, plus your bridge raw-data panels.
Troubleshooting: "Failed to open serial port" -> something else holds the port (usually the Serial Monitor), close it and replug; the first line of a connection can be a clipped partial JSON line (harmless); a card with no bring-up, 1-Wire or light panels means the board still runs an older sketch, so re-upload TempAlarm_Net.

## Lab 4: please propose and write ONE of these (recommend #1), or a short menu with a recommendation
1. Fault-finding challenge (recommended; it fits the bring-up theme): the instructor secretly breaks one board (DS18B20 signal wire off, VCC/GND swapped, wrong pin, a loose joint, light-sensor AO unplugged or moved to the wrong pin, sensor disconnected mid-run). Students diagnose it from the bridge/dashboard evidence alone: which check fails and why, then fix it and watch it recover. Include an instructor fault-injection checklist and a student diagnosis worksheet.
2. Decode and verify: students hand-decode a captured JSON line (raw bytes -> temperature; ADC count -> volts -> brightness %), verify the CRC calculation, and explain what each logged 1-Wire transaction does.
3. Alarm design: students change thresholds/hysteresis in the sketch, predict the alarm behavior, re-upload, and verify on the dashboard. The sketch has a hysteresis constant, which makes a good discussion of chatter. Stretch: add a light-based "dark" rule, using the DO pin or a threshold on the AO value.
4. Accelerometer extension (future): a separate firmware (`arduino_firmware`) exists for a Grove SHT40 (temperature/humidity) plus LIS3DHTR 3-axis accelerometer on I2C. It uses the same bridge/dashboard, and the dashboard already draws a live X/Y/Z graph for it. I2C bring-up stages: bus scan, WHO_AM_I identification (expect 0x33), and a ~1 g functional check. Note the two firmwares are separate sketches today, not one combined image. Treat this as an optional extension or a follow-on series, not part of the 4 labs unless the design team prefers.

## Format requested
For each lab provide: objective, estimated time, materials, a student procedure (numbered), an instructor notes box (setup, timing, common failures, answers), a deliverable, and figure/asset requests (wiring diagram, console callouts, dashboard mockup showing online and offline cards, pipeline diagram of Uno -> browser -> server -> dashboard). Keep the student pages free of instructor answers.

## Open items to flag in the documents (not yet verified)
The TempAlarm_Net sketch has been tested only with simulated data and has not yet been run on real hardware. Mark the expected Serial Monitor output, and the HW-503 wiring and polarity (which way the raw value moves when covered), as "to be confirmed on hardware" until they are.
