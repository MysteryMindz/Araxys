#Araxys
🕸️ Decentralized Mesh Observer (V-Vortex '26)

![Status](https://img.shields.io/badge/Status-Prototype-green)
![Stack](https://img.shields.io/badge/Stack-ESP32%20|%20FastAPI%20|%20Vue.js-blue)

A decentralized, offline-first mesh network designed for emergency communication
in denied environments. This system operates independently of Wi-Fi/Cellular
infrastructure using ESP-NOW and provides a real-time "God View" dashboard.

🚀 Features
* Decentralized Mesh: Self-healing ESP-NOW network (No Router needed).
* Multi-Hop Routing: Custom flooding algorithm with deduplication.
* Bidirectional SOS: Trigger SOS from hardware (Physical Button) or Dashboard.
* Real-time Telemetry: Live Battery, RSSI (Signal), and Heartbeat monitoring.

🛠️ Tech Stack
* Hardware: ESP32 (ESP-NOW Protocol)
* Middleware: Python (PySerial + FastAPI)
* Frontend: HTML5 + Vue.js 3 + CSS

💻 Quick Start
1. Firmware: Flash `firmware.ino` to ESP32 nodes (Change `NODE_ID` for each).
2. Backend: Run `uvicorn main:app --reload`.
3. Bridge: Connect Node A via USB and run `python bridge.py`.
4. Dashboard: Open `static.html` in browser.

================================================================================
