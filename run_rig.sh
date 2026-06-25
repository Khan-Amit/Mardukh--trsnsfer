#!/bin/bash
# ============================================================
# 🚀 MARDUK RIG — ONE-CLICK LAUNCHER
# ============================================================

echo "════════════════════════════════════════════════════════════"
echo "⚡ MARDUK REAL RIG — LAUNCHER"
echo "════════════════════════════════════════════════════════════"

# Step 1: Compile the rig
echo "🔨 Compiling rig..."
g++ -std=c++17 -O3 -march=native -pthread -o marduk_rig marduk_rig_complete.cpp

if [ $? -ne 0 ]; then
    echo "❌ Compilation failed. Check your code."
    exit 1
fi
echo "✅ Compiled successfully."

# Step 2: Run the rig in the background
echo "⛏️ Starting rig in background..."
./marduk_rig &
RIG_PID=$!
echo "✅ Rig running with PID: $RIG_PID"

# Step 3: Serve the status file
echo "📡 Starting HTTP server on port 8080..."
python3 -m http.server 8080 &
HTTP_PID=$!
echo "✅ HTTP server running with PID: $HTTP_PID"

echo ""
echo "════════════════════════════════════════════════════════════"
echo "✅ RIG IS RUNNING!"
echo "📡 ATM URL: http://localhost:8080/index.html"
echo "📤 Wallet: 45ktWDeTNtUc..."
echo "⏹️ To stop: Press Ctrl+C"
echo "════════════════════════════════════════════════════════════"

# Wait for user to stop
wait $RIG_PID
wait $HTTP_PID
