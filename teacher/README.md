# ELT 295: Instructor files

The dashboard server that collects every student's board data, plus the brief for writing the labs.

| File | Purpose |
|---|---|
| `Start_Dashboard.bat` | One-click launcher: sets up Python and the Cloudflare tunnel if needed, starts the server and opens the dashboard |
| `server/server.py` | Flask server: receives reports, serves the dashboard, hands out the student bridge |
| `server/templates/index.html` | The dashboard page |
| `LabSeriesBrief.md` | Brief for writing the 4 labs (instructor and student versions) |

## One-time setup

Nothing to install. The first time you double-click `Start_Dashboard.bat` it downloads, into `teacher/runtime/` (about 30 MB, internet needed once):

- a portable Python 3.12 from python.org, with Flask (skipped if a working Python with Flask is already installed);
- `cloudflared` from Cloudflare's GitHub releases (skipped if it is already installed).

Later runs reuse them. If the setup breaks, delete `teacher/runtime/` and run the launcher again.

## Each class

1. Double-click `Start_Dashboard.bat`. The dashboard opens in your browser, and the window prints the **student download link**, e.g. `https://some-random-words.trycloudflare.com/bridge.html`. This is a Cloudflare tunnel to your PC, so students don't need to reach your PC's IP or port (the school network blocks most ports). The address is new every time the launcher starts, so students download a fresh bridge each class.
2. Project the dashboard and give students the address shown in the blue **Students connect to** banner at the top (with a Copy button and a link to download a bridge that already has it). If your PC has more than one network address, the other ones are listed under it. A yellow warning appears if the server was started in local-only mode.
3. Students upload `TempAlarm_Net`, download the bridge, and connect. Each board's card appears live.
4. Close the launcher window to stop the server.

To use your own board too, run a bridge tab on your PC. Download it from `http://localhost:8000/bridge.html`.

## What the dashboard shows

- One card per board: thermometer and gauge (°F with °C below, alarm thresholds as colored zones), a speaker icon (silent / beeping / solid), a sound meter, and bring-up PASS/FAIL.
- Expandable panels per board: bring-up checks, sensor details (ROM, labeled scratchpad), 1-Wire messages, and the microphone.
- A 3D view of each accelerometer that tilts with the real board (X/Y/Z arrows, roll and pitch in degrees), updated live: the board sends its tilt 10 times a second and the bridge forwards it (at most 5 per second, only when it moved, plus one per second while still), so it follows the board within a fraction of a second and shows a green "live" mark. Without live updates (an older bridge) it plays back the once-per-second report instead, about a second behind. Gravity is the reference, so it needs no zeroing; turning the board flat on the table (yaw) does not show, because an accelerometer cannot sense it.
- A live X/Y/Z accelerometer graph (last 10 seconds, 10 points per second, each the most extreme sample of its 100 ms; the scale grows from +/-2 g up to +/-16 g when someone bangs the board) with the largest jolt each second, a Motion row (peak g), and an expandable LIS3DHTR panel with its bring-up checks and raw registers. A board without the accelerometer shows "no accelerometer".
- A board that stops reporting greys out after about 6 seconds and is removed after 3 minutes.

## Options

```
Start_Dashboard.bat [--no-tunnel] [--port 8000] [--expire-min 3] [--no-browser] [--public-url URL]
```

- `--no-tunnel`: skip Cloudflare and give students this PC's IP address (`http://<ip>:8000`), as before. Only works where the network lets students reach this PC on that port. Windows Firewall may ask to allow Python: choose **Private networks**.
- `--public-url URL`: give students a fixed address you set up yourself (for example a named Cloudflare tunnel) instead of a new random one each time.

- `--port N`: the port the server uses (default 8000; if it is busy the server takes the next free one and says so). On a managed laptop, outgoing connections to most ports can be blocked by policy: 8000 worked in testing where 8081, 5000 and most others were denied. If your network blocks it, try another, then download a fresh bridge from the new address. A firewall rule for one port doesn't cover another, so allow Python for Private networks or add a rule for the new port.
- `--host 127.0.0.1`: this PC only (no firewall prompt), for testing alone.
- `--expire-min N`: minutes before a silent board is removed.

## Notes

- If the tunnel can't start (no internet, or Cloudflare blocked), the server says so and falls back to the IP address.
- The student bridge file lives in `../student/bridge.html`. The server fills in its own address and serves it, so students only need to save and open it. Browsers allow Web Serial only from local files, `localhost` or HTTPS, which is why students open the saved file rather than browsing to the server.
- Restarting the server clears the board list; cards return as boards report again.
- `LabSeriesBrief.md` marks the HW-496 wiring and the expected output as not yet confirmed on hardware.
