import serial
import requests
import json
import time
import sys

# ================= CONFIGURATION =================
SERIAL_PORT = "COM6"  # <--- CHECK PORT
BAUD_RATE = 115200
API_URL = "http://localhost:8000"

def main():
    print(f"🔌 Connecting to {SERIAL_PORT}...")
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1) 
        print(f"✅ Connected!")
    except Exception as e:
        print(f"❌ Connection Failed: {e}")
        return

    last_outbox_check = 0

    while True:
        try:
            # 1. READ SERIAL (Fast Loop)
            if ser.in_waiting > 0:
                try:
                    # Read line and force decode
                    raw_line = ser.readline().decode('utf-8', errors='ignore').strip()
                    
                    # Only parse valid JSON lines
                    if raw_line.startswith('{') and raw_line.endswith('}'):
                        data = json.loads(raw_line)
                        data["timestamp"] = time.time()
                        
                        endpoint = "/heartbeat" if data.get("type") == "heartbeat" else "/receive-message"
                        requests.post(f"{API_URL}{endpoint}", json=data, timeout=0.2)

                        if data.get("type") == "message":
                            print(f"📩 MSG: {data.get('payload')}")
                            
                except Exception:
                    # Ignore partial lines or garbage
                    pass 

            # 2. CHECK OUTBOX (SENDING) - Every 0.2s
            if time.time() - last_outbox_check > 0.2:
                try:
                    resp = requests.get(f"{API_URL}/outbox", timeout=0.1)
                    if resp.status_code == 200:
                        data = resp.json()
                        if data:
                            payload = data.get("payload", "")
                            target = data.get("to", "BROADCAST")
                            
                            cmd = payload
                            if payload not in ["SOS_ON", "SOS_OFF"] and target != "BROADCAST":
                                cmd = f"{target}:{payload}"

                            print(f"📤 SENDING: {cmd}")
                            ser.write((cmd + "\n").encode())
                except:
                    pass
                last_outbox_check = time.time()

        except KeyboardInterrupt:
            ser.close()
            sys.exit()

if __name__ == "__main__":
    main()