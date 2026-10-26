# ELT 295: Instructor files

The dashboard server that collects every student's board data, plus the brief for writing the labs.

| File | Purpose |
|---|---|
| `Start_Dashboard.bat` | One-click launcher: starts the server and opens the dashboard |
| `server/server.py` | Flask server: receives reports, serves the dashboard, hands out the student bridge |
| `server/templates/index.html` | The dashboard page |
| `LabSeriesBrief.md` | Brief for writing the 4 labs (instructor and student versions) |

## One-time setup

1. Install Python 3 and run `py -3 -m pip install -r server/requirements.txt`.
2. Make sure your PC and the students' PCs are on the same network.

## Each class

1. Double-click `Start_Dashboard.bat`. The dashboard opens in your browser, and the window prints the **student download link**, e.g. `http://192.168.1.42:8080/bridge.html`. Windows Firewall may ask to allow Python: choose **Private networks**.
2. Project the dashboard and give students the address shown in the blue **Students connect to** banner at the top (with a Copy button and a link to download a bridge that already has it). If your PC has more than one network address, the other ones are listed under it. A yellow warning appears if the server was started in local-only mode.
3. Students upload `TempAlarm_Net`, download the bridge, and connect. Each board's card appears live.
4. Close the launcher window to stop the server.

To use your own board too, run a bridge tab on your PC. Download it from `http://localhost:8080/bridge.html`.

## What the dashboard shows

- One card per board: thermometer and gauge (°F with °C below, alarm thresholds as colored zones), a speaker icon (silent / beeping / solid), a sound meter, and bring-up PASS/FAIL.
- Expandable panels per board: bring-up checks, sensor details (ROM, labeled scratchpad), 1-Wire messages, and the microphone.
- A live XYZ graph for any board that reports accelerometer data.
- A board that stops reporting greys out after about 6 seconds and is removed after 3 minutes.

## Options

```
py -3 server/server.py [--host 127.0.0.1] [--port 8080] [--expire-min 3] [--no-browser]
```

- `--port N`: the port the server uses (default 8080). If your network blocks it, try another, then download a fresh bridge from the new address. A firewall rule for one port doesn't cover another, so allow Python for Private networks or add a rule for the new port.
- `--host 127.0.0.1`: this PC only (no firewall prompt), for testing alone.
- `--expire-min N`: minutes before a silent board is removed.

## Notes

- The student bridge file lives in `../student/bridge.html`. The server fills in its own address and serves it, so students only need to save and open it. Browsers allow Web Serial only from local files, `localhost` or HTTPS, which is why students open the saved file rather than browsing to the server.
- Restarting the server clears the board list; cards return as boards report again.
- `LabSeriesBrief.md` marks the HW-496 wiring and the expected output as not yet confirmed on hardware.
