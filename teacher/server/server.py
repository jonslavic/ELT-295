#!/usr/bin/env python3
"""
ELT-295 Classroom IoT - Teacher Dashboard Server
--------------------------------------------------
Runs on the TEACHER's computer. Students never install anything: each
student opens student/bridge.html in Chrome or Edge (already on every
Windows PC), which uses the browser's built-in Web Serial API to read
their Uno's JSON directly and POST it here. This server just collects
those reports and serves the live dashboard webpage.

Usage:
    python server.py [--host 0.0.0.0] [--port 8000] [--tunnel | --public-url URL]

Then, on the teacher's machine, find its LAN IP (Windows: `ipconfig`,
look for IPv4 Address) and give students that address, e.g.
    http://192.168.1.42:8000
as the dashboard URL, and the same address as the "server URL" field
in bridge.html.

If the network blocks students from reaching the teacher's PC directly
(blocked ports, client isolation, different subnets), start with --tunnel:
the server launches a Cloudflare quick tunnel (cloudflared) and students
use the https://<random>.trycloudflare.com address it prints instead of
an IP. Only outgoing HTTPS to Cloudflare is needed, which schools allow.
The address changes every time the tunnel starts, so students download a
fresh bridge each class. --public-url sets a fixed address instead (for
example a named Cloudflare tunnel you set up yourself).
"""

import argparse
import atexit
import os
import re
import shutil
import socket
import subprocess
import threading
import time
import webbrowser

from flask import Flask, Response, jsonify, render_template, request

app = Flask(__name__)
app.config["TEMPLATES_AUTO_RELOAD"] = True  # dashboard edits show up on refresh, no restart

# device_id -> { last_seen: epoch seconds, data: <last posted JSON payload> }
devices = {}
devices_lock = threading.Lock()

OFFLINE_AFTER_SEC = 6  # if we haven't heard from a board in this long, mark it offline
EXPIRE_AFTER_SEC = 180  # ...and drop it from the dashboard entirely after this long (--expire-min)
PUBLIC_URL = "http://localhost:8000"  # address students reach this server at (set in main)
OTHER_URLS = []     # this PC's other network addresses, in case PUBLIC_URL picked the wrong adapter
LOCAL_ONLY = False  # True when started with --host 127.0.0.1 (other computers can't connect)
TUNNEL = False      # True when PUBLIC_URL is a Cloudflare tunnel rather than this PC's IP


@app.after_request
def add_cors_headers(resp):
    # bridge.html is opened as a local file (origin "null") or from another
    # host on the LAN, so the browser needs explicit permission to POST here.
    resp.headers["Access-Control-Allow-Origin"] = "*"
    resp.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS"
    resp.headers["Access-Control-Allow-Headers"] = "Content-Type"
    # Chrome's Private Network Access preflight: a saved bridge.html posting to a LAN address
    resp.headers["Access-Control-Allow-Private-Network"] = "true"
    return resp


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/report", methods=["POST", "OPTIONS"])
def report():
    if request.method == "OPTIONS":
        return ("", 204)  # CORS preflight

    payload = request.get_json(silent=True)
    if not payload or "id" not in payload:
        return jsonify({"error": "expected JSON body with an 'id' field"}), 400

    # One card per physical board: key on its permanent hardware ID when the firmware sends one
    # (so renaming a board updates its card), else fall back to the name (older firmware).
    dev_key = str(payload.get("uid") or payload["id"])
    with devices_lock:
        devices[dev_key] = {
            "last_seen": time.time(),
            "data": payload,
        }
    return jsonify({"status": "ok"})


# Live tilt: bridges post a board's latest gravity vector up to 5x per second (only when it
# moved), separate from the once-per-second report. key -> {"t": [x,y,z] g, "at": time, "seq": n}
tilts = {}
TILT_LIVE_SEC = 3


@app.route("/api/tilt", methods=["POST", "OPTIONS"])
def tilt_report():
    if request.method == "OPTIONS":
        return ("", 204)  # CORS preflight
    p = request.get_json(silent=True) or {}
    t = p.get("t")
    key = p.get("uid") or p.get("id")
    if not key or not isinstance(t, list) or len(t) != 3 or not all(isinstance(v, (int, float)) for v in t):
        return jsonify({"error": "expected {uid, t: [x, y, z]}"}), 400
    with devices_lock:
        seq = tilts.get(str(key), {}).get("seq", 0) + 1
        tilts[str(key)] = {"t": [float(v) for v in t], "at": time.time(), "seq": seq}
    return jsonify({"status": "ok"})


@app.route("/api/tilts")
def api_tilts():
    # The dashboard polls this 10x per second (from localhost, not through the tunnel).
    now = time.time()
    with devices_lock:
        for k in [k for k, e in tilts.items() if now - e["at"] > TILT_LIVE_SEC]:
            del tilts[k]
        return jsonify({k: {"t": e["t"], "seq": e["seq"]} for k, e in tilts.items()})


@app.route("/api/devices")
def api_devices():
    now = time.time()
    out = []
    with devices_lock:
        for dev_id in [k for k, e in devices.items() if now - e["last_seen"] > EXPIRE_AFTER_SEC]:
            del devices[dev_id]
        for dev_key, entry in devices.items():
            age = now - entry["last_seen"]
            out.append({
                "key": dev_key,                                   # stable identity (hardware ID)
                "id": str(entry["data"].get("id", dev_key)),      # display name, may change
                "uid": entry["data"].get("uid"),
                "online": age <= OFFLINE_AFTER_SEC,
                "seconds_since_seen": round(age, 1),
                "data": entry["data"],
            })
    out.sort(key=lambda d: (d["id"].lower(), d["key"]))
    return jsonify(out)


def lan_ip():
    # Connecting a UDP socket sends nothing; it just makes the OS pick the outgoing LAN interface.
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return "localhost"
    finally:
        s.close()


def local_ips():
    # Every IPv4 address this PC has (WiFi, Ethernet, VPN, virtual adapters), minus loopback/link-local.
    ips = []
    try:
        for ip in socket.gethostbyname_ex(socket.gethostname())[2]:
            if not ip.startswith(("127.", "169.254.")) and ip not in ips:
                ips.append(ip)
    except OSError:
        pass
    return ips


@app.route("/api/info")
def api_info():
    # What the dashboard banner shows: the address students type into the bridge.
    return jsonify({"url": PUBLIC_URL, "others": OTHER_URLS, "local_only": LOCAL_ONLY, "tunnel": TUNNEL})


@app.route("/bridge.html")
def bridge_download():
    # Hand out the student bridge with this server's address already filled in.
    # It must be saved and opened as a file: Web Serial won't run from plain http://<lan-ip>.
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "student", "bridge.html")
    with open(path, encoding="utf-8") as f:
        html = f.read().replace("__SERVER_URL__", PUBLIC_URL)
    return Response(html, mimetype="text/html",
                    headers={"Content-Disposition": "attachment; filename=ELT295-bridge.html"})


def pick_port(host, port, tries=20):
    # If another program already holds the port, use the next free one instead of failing.
    for p in range(port, port + tries):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            s.bind((host, p))
            return p
        except OSError:
            continue
        finally:
            s.close()
    return port  # nothing free nearby: let Flask report the real error


def find_cloudflared():
    # On PATH, next to this server / the launcher, or where `winget install Cloudflare.cloudflared` puts it.
    here = os.path.dirname(os.path.abspath(__file__))
    candidates = [shutil.which("cloudflared"),
                  os.path.join(here, "cloudflared.exe"),
                  os.path.join(here, "..", "cloudflared.exe"),
                  os.path.join(here, "..", "runtime", "cloudflared.exe"),  # Start_Dashboard.bat downloads it here
                  os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), "cloudflared", "cloudflared.exe"),
                  os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"), "cloudflared", "cloudflared.exe")]
    return next((c for c in candidates if c and os.path.isfile(c)), None)


def start_tunnel(port, timeout=45):
    """Start a Cloudflare quick tunnel to this server; return its https URL, or None."""
    exe = find_cloudflared()
    if not exe:
        print(" Cloudflare tunnel: cloudflared not found. Start_Dashboard.bat downloads it automatically;")
        print(" or install it with  winget install --id Cloudflare.cloudflared  and start again.")
        return None
    print(" Starting Cloudflare tunnel (takes a few seconds)...")
    proc = subprocess.Popen([exe, "tunnel", "--no-autoupdate", "--url", f"http://localhost:{port}"],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                            encoding="utf-8", errors="replace")
    found = threading.Event()
    result = {}

    def pump():
        # cloudflared prints the address in its log; keep draining the pipe so it never blocks.
        for line in proc.stdout:
            m = re.search(r"https://[-a-z0-9]+\.trycloudflare\.com", line)
            if m and not found.is_set():
                result["url"] = m.group(0)
                found.set()
    threading.Thread(target=pump, daemon=True).start()

    if not found.wait(timeout) or proc.poll() is not None:
        proc.terminate()
        print(" Cloudflare tunnel did not start (no internet, or Cloudflare blocked?). Using the LAN address.")
        return None
    atexit.register(proc.terminate)  # close the tunnel with the server
    return result["url"]


def main():
    global EXPIRE_AFTER_SEC, PUBLIC_URL, OTHER_URLS, LOCAL_ONLY, TUNNEL
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--expire-min", type=float, default=EXPIRE_AFTER_SEC / 60,
                        help="remove boards from the dashboard after this many minutes without a report (default 3)")
    parser.add_argument("--no-browser", action="store_true", help="don't open the dashboard in a browser on start")
    parser.add_argument("--tunnel", action="store_true",
                        help="give students a Cloudflare https://...trycloudflare.com address instead of this PC's IP")
    parser.add_argument("--public-url", help="address students connect to, if you have your own (e.g. a named tunnel)")
    args = parser.parse_args()

    EXPIRE_AFTER_SEC = args.expire_min * 60

    wanted = args.port
    args.port = pick_port(args.host, wanted)
    if args.port != wanted:
        print(f" Port {wanted} is already in use by another program, so using {args.port} instead.")

    LOCAL_ONLY = args.host in ("127.0.0.1", "localhost")
    PUBLIC_URL = f"http://{'localhost' if LOCAL_ONLY else lan_ip()}:{args.port}"
    if not LOCAL_ONLY:
        OTHER_URLS = [u for u in (f"http://{ip}:{args.port}" for ip in local_ips()) if u != PUBLIC_URL]
    public = args.public_url.strip().rstrip("/") if args.public_url else None
    if not public and args.tunnel:
        public = start_tunnel(args.port)
    if public:
        if not re.match(r"https?://", public, re.I):
            public = "https://" + public
        OTHER_URLS = [PUBLIC_URL] + OTHER_URLS  # the LAN address still works where the network allows it
        PUBLIC_URL, TUNNEL = public, True

    print("=" * 60)
    print(f" Teacher dashboard : http://localhost:{args.port}")
    print(f" Students connect  : {PUBLIC_URL}")
    print(f" Students download : {PUBLIC_URL}/bridge.html")
    print("   (the file already knows this server's address; they just double-click it)")
    for u in OTHER_URLS:
        print(f" Other address     : {u}   (use this if the one above is the wrong network)")
    if LOCAL_ONLY:
        print(" NOTE: local-only mode (--host 127.0.0.1): other computers cannot connect.")
    print("=" * 60)
    if not args.no_browser:
        threading.Timer(1.0, webbrowser.open, args=(f"http://localhost:{args.port}",)).start()
    app.run(host=args.host, port=args.port, debug=False, threaded=True)


if __name__ == "__main__":
    main()
