#!/usr/bin/env python3
# ============================================================
# 🚀 MARDUK SERVER — Auto-start Rig + Serve ATM
# ============================================================
#
# Run: python3 marduk_server.py
# Open: http://localhost:8080
#
# ============================================================

import os
import subprocess
import json
import time
import signal
import threading
from flask import Flask, send_from_directory, jsonify, request
from flask_cors import CORS

app = Flask(__name__)
CORS(app)

# --- CONFIG ---
RIG_CPP = "marduk_rig_complete.cpp"   # your C++ source
RIG_BIN = "marduk_rig"                # compiled binary
STATUS_FILE = "miner_status.json"     # status file written by rig
RIG_PROCESS = None
RIG_LOCK = threading.Lock()

# --- Helper: compile rig ---
def compile_rig():
    if not os.path.exists(RIG_CPP):
        return False, f"❌ {RIG_CPP} not found!"
    try:
        subprocess.run(
            ["g++", "-std=c++17", "-O3", "-march=native", "-pthread", "-o", RIG_BIN, RIG_CPP],
            check=True,
            capture_output=True
        )
        return True, "✅ Compiled successfully"
    except subprocess.CalledProcessError as e:
        return False, f"❌ Compilation failed: {e.stderr.decode()}"

# --- API: start rig ---
@app.route('/start', methods=['POST'])
def start_rig():
    global RIG_PROCESS
    with RIG_LOCK:
        if RIG_PROCESS is not None and RIG_PROCESS.poll() is None:
            return jsonify({"status": "already_running", "message": "Rig is already running"})

        # compile if needed
        if not os.path.exists(RIG_BIN):
            ok, msg = compile_rig()
            if not ok:
                return jsonify({"status": "error", "message": msg})

        # start rig
        try:
            RIG_PROCESS = subprocess.Popen(
                ["./" + RIG_BIN],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                preexec_fn=os.setsid  # so we can kill the whole process group
            )
            # wait a bit for it to start
            time.sleep(2)
            return jsonify({"status": "started", "message": "Rig started"})
        except Exception as e:
            return jsonify({"status": "error", "message": str(e)})

# --- API: stop rig ---
@app.route('/stop', methods=['POST'])
def stop_rig():
    global RIG_PROCESS
    with RIG_LOCK:
        if RIG_PROCESS is None or RIG_PROCESS.poll() is not None:
            return jsonify({"status": "not_running", "message": "Rig is not running"})
        try:
            os.killpg(os.getpgid(RIG_PROCESS.pid), signal.SIGTERM)
            RIG_PROCESS = None
            return jsonify({"status": "stopped", "message": "Rig stopped"})
        except Exception as e:
            return jsonify({"status": "error", "message": str(e)})

# --- API: get status ---
@app.route('/status')
def get_status():
    if os.path.exists(STATUS_FILE):
        try:
            with open(STATUS_FILE, 'r') as f:
                data = json.load(f)
                return jsonify(data)
        except:
            return jsonify({"hashrate": 0, "shares": 0, "earnings": 0, "pool": "Unknown"})
    return jsonify({"hashrate": 0, "shares": 0, "earnings": 0, "pool": "Unknown"})

# --- Serve HTML and static files ---
@app.route('/')
@app.route('/index.html')
def serve_index():
    return send_from_directory('.', 'index.html')

# --- Serve any other static files ---
@app.route('/<path:path>')
def serve_static(path):
    return send_from_directory('.', path)

# --- Main ---
if __name__ == '__main__':
    print("════════════════════════════════════════════════════════════")
    print("🚀 MARDUK SERVER — Starting...")
    print("📡 Serving at: http://localhost:8080")
    print("🛑 Press Ctrl+C to stop")
    print("════════════════════════════════════════════════════════════")
    app.run(host='0.0.0.0', port=8080, debug=False, threaded=True)
