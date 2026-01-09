import serial
import requests
import json
import time

# ================= CONFIGURATION =================
SERIAL_PORT = "COM3"   # <--- CHANGE THIS to your ESP32 Port
BAUD_RATE = 115200
API_URL = "http://localhost:8000"
# =================================================

def connect_serial():
    try:
        print(f"🔌 Connecting to {SERIAL_PORT}...")
        return serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    except Exception as e:
        print(f"❌ Connection Failed: {e}")
        return None

def main():
    ser = connect_serial()
    print("✅ Bridge Started. Waiting for data...")
    
    while True:
        # 1. Reconnection Logic
        if ser is None or not ser.is_open:
            print("⚠️ Device disconnected. Retrying...")
            time.sleep(2)
            ser = connect_serial()
            continue

        # 2. READ: Hardware -> Dashboard
        if ser.in_waiting > 0:
            try:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                
                # Only process valid JSON lines
                if line.startswith("{") and line.endswith("}"):
                    data = json.loads(line)
                    print(f"📥 Received: {data}")

                    # Forward to appropriate Backend Endpoint
                    if data.get('type') == 'heartbeat':
                        requests.post(f"{API_URL}/heartbeat", json=data)
                    
                    elif data.get('type') == 'sos':
                         # Activate SOS on Dashboard
                         requests.post(f"{API_URL}/sos/on", json={
                            "node_id": data['node_id'],
                            "active": True
                         })
                         # Log the alert message
                         requests.post(f"{API_URL}/receive-message", json={
                            "packet_id": str(time.time()),
                            "type": "message",
                            "from_node": data['node_id'],
                            "to": "BROADCAST",
                            "payload": "🚨 SOS ALERT RECEIVED",
                            "rssi": data['rssi'],
                            "timestamp": time.time(),
                            "is_direct": data.get('is_direct', False)
                         })

                    elif data.get('type') == 'message':
                        requests.post(f"{API_URL}/receive-message", json={
                            "packet_id": str(time.time()),
                            "type": "message",
                            "from_node": data['node_id'],
                            "to": "BROADCAST",
                            "payload": data['payload'],
                            "rssi": data['rssi'],
                            "timestamp": time.time(),
                            "is_direct": data.get('is_direct', False)
                        })

            except json.JSONDecodeError:
                pass # Ignore debug prints from Arduino
            except Exception as e:
                print(f"Error: {e}")

        # 3. WRITE: Dashboard -> Hardware (Outbox)
        try:
            res = requests.get(f"{API_URL}/outbox")
            if res.status_code == 200:
                outbox_item = res.json()
                if outbox_item:
                    command = outbox_item.get('payload', '')
                    print(f"📤 Sending Command: {command}")
                    ser.write(f"{command}\n".encode())
        except Exception:
            pass
            
        time.sleep(0.05) 

if __name__ == "__main__":
    main()