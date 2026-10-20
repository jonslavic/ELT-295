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
    python server.py [--host 0.0.0.0] [--port 5000]

Then, on the teacher's machine, find its LAN IP (Windows: `ipconfig`,
look for IPv4 Address) and give students that address, e.g.
    http://192.168.1.42:5000
as the dashboard URL, and the same address as the "server URL" field
in bridge.html.
"""

import argparse
import os
import socket
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
PUBLIC_URL = "http://localhost:5000"  # address students reach this server at (set in main)


@app.after_request
def add_cors_headers(resp):
    # bridge.html is opened as a local file (origin "null") or from another
    # host on the LAN, so the browser needs explicit permission to POST here.
    resp.headers["Access-Control-Allow-Origin"] = "*"
    resp.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS"
    resp.headers["Access-Control-Allow-Headers"] = "Content-Type"
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

    dev_id = str(payload["id"])
    with devices_lock:
        devices[dev_id] = {
            "last_seen": time.time(),
            "data": payload,
        }
    return jsonify({"status": "ok"})


@app.route("/api/devices")
def api_devices():
    now = time.time()
    out = []
    with devices_lock:
        for dev_id in [k for k, e in devices.items() if now - e["last_seen"] > EXPIRE_AFTER_SEC]:
            del devices[dev_id]
        for dev_id, entry in devices.items():
            age = now - entry["last_seen"]
            out.append({
                "id": dev_id,
                "online": age <= OFFLINE_AFTER_SEC,
                "seconds_since_seen": round(age, 1),
                "data": entry["data"],
            })
    out.sort(key=lambda d: d["id"])
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


@app.route("/bridge.html")
def bridge_download():
    # Hand out the student bridge with this server's address already filled in.
    # It must be saved and opened as a file: Web Serial won't run from plain http://<lan-ip>.
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "student", "bridge.html")
    with open(path, encoding="utf-8") as f:
        html = f.read().replace("__SERVER_URL__", PUBLIC_URL)
    return Response(html, mimetype="text/html",
                    headers={"Content-Disposition": "attachment; filename=ELT295-bridge.html"})


def main():
    global EXPIRE_AFTER_SEC, PUBLIC_URL
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--expire-min", type=float, default=EXPIRE_AFTER_SEC / 60,
                        help="remove boards from the dashboard after this many minutes without a report (default 3)")
    parser.add_argument("--no-browser", action="store_true", help="don't open the dashboard in a browser on start")
    args = parser.parse_args()

    EXPIRE_AFTER_SEC = args.expire_min * 60

    local_only = args.host in ("127.0.0.1", "localhost")
    PUBLIC_URL = f"http://{'localhost' if local_only else lan_ip()}:{args.port}"

    print("=" * 60)
    print(f" Teacher dashboard : http://localhost:{args.port}")
    print(f" Students download : {PUBLIC_URL}/bridge.html")
    print("   (the file already knows this server's address; they just double-click it)")
    print("=" * 60)
    if not args.no_browser:
        threading.Timer(1.0, webbrowser.open, args=(f"http://localhost:{args.port}",)).start()
    app.run(host=args.host, port=args.port, debug=False, threaded=True)


if __name__ == "__main__":
    main()
