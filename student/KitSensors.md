# Lab 4: Two new sensors from the 37-sensor kit

You add **two modules you haven't used yet** to the board you built in Labs 1–3. Everything you already have (temperature sensor, buzzer, microphone, accelerometer) stays connected. The new modules report to the bridge page and the class dashboard with their own bring-up checks, just like the temperature sensor.

> **To be confirmed on hardware:** pin orders on these cheap modules vary between batches, and the kit's boards are not labelled with KY numbers. **The silkscreen printed on your board is the source of truth.** If it disagrees with this guide, follow the board and tell your instructor.

## 1. Pick your two modules

Find your two modules in the table in section 4. Then open `TempAlarm_Net.ino` and change these two lines near the top:

```cpp
#define KIT_SENSOR_1  KY018      // slot 1: photoresistor
#define KIT_SENSOR_2  KY040      // slot 2: rotary encoder
```

Write `KY` and the number with no dash (`KY018`, not `KY-018`). Use `KitNone` for an empty slot. Upload, then check the Serial Monitor: at startup it prints each module's name and the exact wiring for its slot.

**Pick one input and one output** (a button and a relay, a flame sensor and a buzzer) and the output follows the input automatically. An output on its own runs a demo pattern so you can see it work.

## 2. The two slots

Each slot is a fixed set of free pins. The pins from Labs 1–3 (D2, D3, D9, D13, A0, A4, A5) are never used.

| Slot pin | Slot 1 | Slot 2 | Used for |
|---|---|---|---|
| **A** | D4 | D7 | the signal of most digital modules (S, OUT, DO, CLK) |
| **B** | D5 | D10 | a second signal (encoder DT, LED red, cup LED) |
| **C** | D6 | D11 | a third signal (encoder SW, LED green) |
| **Analog** | A1 | A2 | the signal of analog modules (S, AO, joystick X) |
| **Analog 2** | A3 | A3 | joystick Y only (so only one joystick at a time) |

Every module also needs **GND**, and most need **5V**. The Uno has three GND pins and one 5V pin; use a breadboard rail to share 5V between modules.

## 3. Before you power it (bring-up rule)

1. Read the silkscreen: find **GND / − / G**, **5V / + / VCC / middle**, and the signal pin.
2. Check that no module puts 5V onto a GND pin. If a chip gets warm, unplug the USB cable immediately.
3. **LEDs without a resistor on the board** (KY-009, KY-011, KY-029): put a **220 Ω resistor** in series with each colour pin. If you can see small resistors on the module, you can skip them.
4. **Relay (KY-019):** leave the screw terminals empty. **Never** connect mains voltage.
5. **Laser (KY-008):** never look into the beam or point it at anyone.

## 4. Every module in the kit

Pins are written *module pin → Uno pin (slot 1 / slot 2)*. "middle" is the centre pin of a 3-pin module.

### Already used in Labs 1–3

| Module | What it is | Notes |
|---|---|---|
| KY-001 (HW-506) | DS18B20 temperature, 1-Wire | Lab 1. Pick something else. |
| KY-006 (HW-508) | Passive buzzer | Lab 1 alarm. Pick something else. |
| KY-037 or KY-038 | Sound sensor | One of these is the Lab 2 microphone (HW-496). The **other** one is allowed. |

### Digital switches: one signal that changes when something happens

All of these: **− → GND, middle → 5V, S → D4 / D7**. The checks are "steady idle level at boot" and "the signal changed at least once".

| Module | How to recognise it | Make it respond |
|---|---|---|
| **KY-002** Vibration switch | Small metal cylinder (spring inside) | Tap or shake the board |
| **KY-003** Hall magnetic (on/off) | Small black 3-leg chip (A3144), board LED | Bring a magnet's pole to the flat face |
| **KY-004** Push button | Square tactile button | Press it |
| **KY-010** Photo interrupter | U-shaped slot (light gate) | Slide paper through the slot |
| **KY-017** Mercury tilt switch | Glass/metal capsule with a blob inside | Tilt it past level |
| **KY-020** Ball tilt switch | Small metal can that rattles | Tilt it |
| **KY-021** Mini reed switch | Small glass tube with two reeds | Hold a magnet alongside the tube |
| **KY-031** Knock sensor | Spring in a clear/black housing | Knock on the table next to it |
| **KY-033** Line tracking | Black/blue sensor (LED + phototransistor) facing down. Pins **G → GND, V+ → 5V, S → D4 / D7** | Move it over black tape on white paper (5–10 mm away) |
| **KY-032** Obstacle avoidance | Two clear/black "eyes" facing forward, 4 pins. **GND → GND, + → 5V, OUT → D4 / D7, EN → not connected** (leave the jumper on) | Hand 5–15 cm in front; adjust the pot for range |

### Analog sensors: a voltage that changes smoothly

All of these: **− → GND, middle → 5V, S → A1 / A2**.

| Module | How to recognise it | What you'll see |
|---|---|---|
| **KY-018** Photoresistor | Squiggly-track light-dependent resistor | Counts move as you cover it or shine a phone torch. Check: moves 100+ counts |
| **KY-013** Analog temperature | Black bead (thermistor), no chip | Temperature in °C. Check: within 3 °C of your DS18B20. The sketch works out which way round the thermistor is wired by comparing with the DS18B20, which is a good discussion point |
| **KY-035** Analog Hall | Black 3-leg chip (49E), no pot | Rest level near 512; a magnet pushes it up for one pole and down for the other |
| **KY-039** Heartbeat | IR LED + phototransistor facing each other | Rest a fingertip lightly between them, keep still, dim room. Shows beats per minute. This module is finicky: patience |

**KY-023 Joystick** (5 pins): **GND → GND, +5V → 5V, VRx → A1 / A2, VRy → A3, SW → D4 / D7**. Shows X and Y in % from centre and the button. Checks: centred at rest, each axis reaches both ends, button pressed.

### Analog + digital modules: sensor plus comparator (blue pot)

These have 4 pins: **AO → A1 / A2, G → GND, + → 5V, DO → D4 / D7**. AO is the sensor's voltage; DO is a comparator that flips when AO crosses the level set by the blue pot (the same idea as the Lab 2 microphone).

| Module | How to recognise it | Make it respond |
|---|---|---|
| **KY-024** Linear Hall | Black 3-leg chip + LM393 + pot | Bring a magnet close; turn the pot until DO flips |
| **KY-025** Reed switch module | Glass reed tube + LM393 + pot | Hold a magnet alongside the tube |
| **KY-026** Flame sensor | Dark (black) IR LED-shaped sensor | Point a TV remote at it and press a button (or a lighter flame at 20 cm+, instructor only) |
| **KY-028** Digital temperature | Black bead thermistor + LM393 + pot | Pinch the bead. Temperature is rough (not calibrated); DO is a "too hot" switch set by the pot |
| **KY-036** Metal touch | Bent wire on a transistor + LM393 + pot | Touch the wire tip |
| **KY-037 / KY-038** Sound | Microphone (big or small) + LM393 + pot | Clap. Loudness = peak-to-peak swing, like Lab 2 |

### Special inputs

| Module | Pins | Notes |
|---|---|---|
| **KY-040** Rotary encoder | **CLK → D4 / D7, DT → D5 / D10, SW → D6 / D11, + → 5V, GND → GND** | Position in clicks and button presses. If turning clockwise counts down, swap CLK and DT. CLK and DT are two switches a quarter-step apart (quadrature): which one changes first tells the direction |
| **KY-022** IR receiver | **− → GND, middle → 5V, S → D4 / D7** | Point a TV remote at it. Decodes NEC codes (most remotes; some brands use other codes and show "IR seen" only). A team with a KY-005 can send to you: address 0x29 |
| **KY-015** DHT11 temp + humidity | **S → D4 / D7, middle → 5V, − → GND** | Blue grille sensor. Read every 2 s; shows the 5 raw bytes and checks the checksum (bytes 0–3 add up to byte 4). Breathe on it: humidity rises |
| **KY-027** Magic light cup | **G → GND, + → 5V, S → D4 / D7, L → D5 / D10** | Mercury switch + LED on one board: tilt it and the LED fades up. Pick it for both slots to use the pair |

### Outputs

| Module | Pins | What it does |
|---|---|---|
| **KY-016** RGB LED (5 mm) | **R → D5 / D10, G → D6 / D11, B → D4 / D7, − → GND** | Cycles colours; with an input it's green when the input fires and red otherwise. Blue is on/off only |
| **KY-009** RGB LED (SMD) | Same as KY-016, **with a 220 Ω resistor on R, G and B** | Same as KY-016. Pin order on the board varies: read the silkscreen |
| **KY-011** Two-colour LED (5 mm) | **− → GND, middle (red) → D5 / D10, S (green) → D6 / D11**, 220 Ω on each | Red, yellow (both), green. If red and green are swapped, swap the two wires |
| **KY-029** Two-colour LED (3 mm) | Same as KY-011 | Same as KY-011 |
| **KY-034** 7-colour flash LED | **S → D4 / D7, − → GND**, middle not connected | Cycles colours by itself while switched on |
| **KY-008** Laser | **S → D4 / D7, − → GND**, middle not connected | Switches on and off. **Never look into the beam** |
| **KY-012** Active buzzer | **S → D4 / D7, middle → 5V, − → GND** | Short chirp every 3 s (it's loud). An active buzzer beeps by itself when powered, unlike Lab 1's passive one, which needs a tone |
| **KY-019** Relay | **S → D4 / D7, + → 5V, − → GND**; screw terminals empty | Clicks and its LED lights. **Never mains** |
| **KY-005** IR transmitter | **S → D4 / D7, middle → 5V, − → GND** | Sends a NEC code (address 0x29) every second. Your eye can't see it: look through a phone camera. Another team's KY-022 will decode it |

## 5. What you'll see

- **Serial Monitor at startup:** a `KIT SENSOR (slot 1)` block with the module's name and its wiring for that slot.
- **Bridge page:** one panel per module with its reading, the wiring, the checks (PASS / FAIL, or **LOOK** = confirm it by eye) and the raw numbers behind the reading.
- **Dashboard:** at the bottom of your card, each module's reading, a one-minute sparkline, and a checks summary you can expand.

## 6. Troubleshooting

| Symptom | Likely cause |
|---|---|
| `'KY18' does not name a type` (or similar) when compiling | Typo: write `KY018`, three digits, no dash |
| Module shows "not stuck at 0 V or 5 V" FAIL | Signal on the wrong pin, or 5V/GND missing (analog modules go to A1 / A2, not D4) |
| "Steady idle level" FAIL | The signal wire is loose, or on the wrong pin |
| Digital module never changes | Wrong slot pin; for comparator modules turn the blue pot slowly until the module's own LED just switches |
| LED colours swapped | Swap the two colour wires (pin order varies) |
| Encoder counts the wrong way | Swap CLK and DT |
| IR receiver sees IR but decodes nothing | That remote doesn't use NEC; try another remote or a KY-005 |
| DHT11 "Sensor answered" FAIL | S not on D4 / D7, or no 5V |
| Nothing at all for a module | The `#define` still says `KitNone`, or you didn't re-upload |
