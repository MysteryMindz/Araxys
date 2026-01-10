/**
 * ESP32 Mesh System (FIXED JSON & BRIDGE SUPPORT)
 * - Added "is_direct" field for Frontend Host Detection
 * - Compatible with Python Bridge
 */

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

// ================= USER CONFIGURATION =================
#define WIFI_CHANNEL 1
#define NODE_ID 'B'        // <--- CHANGE THIS IF NEEDED ('A', 'B', 'C', 'D')

// --- PINS ---
#define ROUTE_LED 32       
#define BUZZER_PIN 25      
#define STOP_BTN_PIN 27    

#define MAX_HOPS 5
#define DEDUP_SIZE 50 

// --- DTN CONFIGURATION ---
#define DTN_BUFFER_SIZE 10       
#define DTN_RETRY_DELAY 5000     
#define DTN_PACKET_TTL 60000     
#define RSSI_THRESHOLD -80       

// --- HEARTBEAT CONFIGURATION ---
#define HEARTBEAT_INTERVAL 2000 
#define TYPE_DATA 0
#define TYPE_HEARTBEAT 1

// ================= PACKET STRUCTURE =================
typedef struct __attribute__((packed)) {
  uint32_t packetId; 
  uint8_t packetType; 
  char src;
  char dest;
  char lastHop;      
  uint8_t hopCount;  
  bool isSOS;
  char msg[64];
} Packet;

// --- DTN ENTRY STRUCTURE ---
typedef struct {
  Packet pkt;             
  unsigned long storedAt; 
  bool active;            
} DTNEntry;

// ================= GLOBALS =================
uint32_t seenPackets[DEDUP_SIZE];
int seenIndex = 0;
DTNEntry dtnBuffer[DTN_BUFFER_SIZE]; 
unsigned long lastDtnCheck = 0;

unsigned long lastHeartbeatTimer = 0;

// PEER MAC ADDRESSES
uint8_t macA[] = {0x20, 0xE7, 0xC8, 0xB4, 0xC1, 0x3C}; 
uint8_t macB[] = {0xD4, 0xE9, 0xF4, 0xE1, 0xB1, 0xFC}; 
uint8_t macC[] = {0x44, 0x1D, 0x64, 0xBD, 0x5E, 0x74};
uint8_t macD[] = {0xFC, 0xE8, 0xC0, 0xE0, 0xE1, 0x60}; 

// Flags
volatile bool isSirenActive = false; 
volatile bool triggerBeep = false;
unsigned long routeLedTimer = 0;
bool routeLedActive = false;

// ================= HELPER: JSON PRINTER (FIXED) =================
void jsonPrint(String type, Packet* pkt, int rssi) {
  Serial.print("{");
  Serial.print("\"type\":\""); Serial.print(type); Serial.print("\",");
  
  if (type == "heartbeat") {
      // Logic for Heartbeats
      char sender = (pkt == NULL) ? NODE_ID : pkt->src;
      int hops = (pkt == NULL) ? 1 : pkt->hopCount;
      bool sosState = (pkt == NULL) ? isSirenActive : pkt->isSOS;
      
      // CRITICAL FIX: "is_direct" tells the frontend THIS is the host node
      bool isDirect = (pkt == NULL);

      Serial.print("\"node_id\":\""); Serial.print(sender); Serial.print("\",");
      Serial.print("\"battery\":100,"); 
      Serial.print("\"rssi\":"); Serial.print(rssi); Serial.print(",");
      Serial.print("\"hop_count\":"); Serial.print(hops); Serial.print(",");
      Serial.print("\"is_direct\":"); Serial.print(isDirect ? "true" : "false"); Serial.print(",");
      Serial.print("\"sos\":"); Serial.print(sosState ? "true" : "false");
  } else {
      // Logic for Messages/SOS
      Serial.print("\"packet_id\":\""); Serial.print(pkt->packetId); Serial.print("\",");
      Serial.print("\"from_node\":\""); Serial.print(pkt->src); Serial.print("\",");
      Serial.print("\"to\":\""); Serial.print(pkt->dest); Serial.print("\",");
      Serial.print("\"payload\":\""); Serial.print(pkt->msg); Serial.print("\",");
      Serial.print("\"hop_count\":"); Serial.print(pkt->hopCount); Serial.print(",");
      Serial.print("\"rssi\":"); Serial.print(rssi); 
  }
  Serial.println("}");
}

// ================= HELPER FUNCTIONS =================

char identifyNode(const uint8_t *mac) {
  if (memcmp(mac, macA, 6) == 0) return 'A';
  if (memcmp(mac, macB, 6) == 0) return 'B';
  if (memcmp(mac, macC, 6) == 0) return 'C';
  if (memcmp(mac, macD, 6) == 0) return 'D';
  return '?';
}

bool isDuplicate(uint32_t id) {
  for (int i = 0; i < DEDUP_SIZE; i++) {
    if (seenPackets[i] == id) return true;
  }
  return false;
}

void addToSeen(uint32_t id) {
  seenPackets[seenIndex] = id;
  seenIndex = (seenIndex + 1) % DEDUP_SIZE;
}

// --- DTN HELPERS ---
void storeForLater(Packet p) {
  int slot = -1;
  unsigned long oldestTime = millis();
  
  for (int i = 0; i < DTN_BUFFER_SIZE; i++) {
    if (!dtnBuffer[i].active) {
      slot = i;
      break; 
    }
    if (dtnBuffer[i].storedAt < oldestTime) {
      oldestTime = dtnBuffer[i].storedAt;
      slot = i;
    }
  }
  if (slot != -1) {
    dtnBuffer[slot].pkt = p;
    dtnBuffer[slot].storedAt = millis();
    dtnBuffer[slot].active = true;
  }
}

void processDTN() {
  unsigned long now = millis();
  if (now - lastDtnCheck < DTN_RETRY_DELAY) return;
  lastDtnCheck = now;

  for (int i = 0; i < DTN_BUFFER_SIZE; i++) {
    if (dtnBuffer[i].active) {
      if (now - dtnBuffer[i].storedAt > DTN_PACKET_TTL) {
        dtnBuffer[i].active = false;
        continue;
      }
      
      uint8_t *allMacs[] = {macA, macB, macC, macD};
      char allIds[] = {'A', 'B', 'C', 'D'};
      delay(random(5, 15)); 

      for(int j=0; j<4; j++) {
        if(allIds[j] == NODE_ID) continue;
        esp_now_send(allMacs[j], (uint8_t*)&dtnBuffer[i].pkt, sizeof(Packet));
      }
      
      digitalWrite(ROUTE_LED, HIGH);
      delay(10);
      digitalWrite(ROUTE_LED, LOW);
    }
  }
}

// ================= DATA RECEIVE LOGIC =================

void OnDataRecv(const esp_now_recv_info_t * info, const uint8_t *data, int len) {
  if (len != sizeof(Packet)) return;
  
  Packet pkt;
  memcpy(&pkt, data, sizeof(pkt));

  int rssi = info->rx_ctrl->rssi;
  char sender = identifyNode(info->src_addr);

  // --- 1. OUTPUT JSON FOR BRIDGE ---
  if (pkt.packetType == TYPE_HEARTBEAT) {
      jsonPrint("heartbeat", &pkt, rssi);
  } else {
      String typeStr = pkt.isSOS ? "sos" : "message";
      jsonPrint(typeStr, &pkt, rssi);
  }

  // --- 2. HARDWARE LOGIC ---
  if (pkt.packetType != TYPE_HEARTBEAT) {
      if (isDuplicate(pkt.packetId)) return;
      addToSeen(pkt.packetId);
      
      if (pkt.dest == NODE_ID || pkt.dest == '*') {
        if (pkt.isSOS) {
            isSirenActive = true; 
        } else {
            triggerBeep = true;
        }
        if (pkt.dest != '*') return; 
      }
  } else {
      return; 
  }

  // --- 3. RELAY LOGIC ---
  if (pkt.hopCount < MAX_HOPS) {
    pkt.hopCount++;        
    pkt.lastHop = NODE_ID; 

    bool shouldRelayNow = false;

    if (pkt.isSOS) {
        shouldRelayNow = true;
    } else if (rssi >= RSSI_THRESHOLD || rssi == 0) { 
        shouldRelayNow = true;
    } else {
        storeForLater(pkt);
        shouldRelayNow = false;
    }

    if (shouldRelayNow) {
        delay(random(5, 20)); 
        digitalWrite(ROUTE_LED, HIGH);
        routeLedActive = true;
        routeLedTimer = millis();

        uint8_t *allMacs[] = {macA, macB, macC, macD};
        char allIds[] = {'A', 'B', 'C', 'D'};

        for(int i=0; i<4; i++) {
          if(allIds[i] == NODE_ID) continue; 
          if(allIds[i] == sender) continue;
          esp_now_send(allMacs[i], (uint8_t*)&pkt, sizeof(pkt));
        }
    }
  }
}

// ================= SETUP =================

void setup() {
  Serial.begin(115200);
  
  pinMode(ROUTE_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(STOP_BTN_PIN, INPUT_PULLUP); 

  digitalWrite(ROUTE_LED, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  esp_wifi_set_max_tx_power(78); 

  if (esp_now_init() != ESP_OK) ESP.restart();

  esp_now_register_recv_cb(OnDataRecv);

  esp_now_peer_info_t peerInfo = {};
  peerInfo.channel = WIFI_CHANNEL;
  peerInfo.encrypt = false;

  auto registerNode = [&](uint8_t* mac, char id) {
    if (NODE_ID != id) {
      memcpy(peerInfo.peer_addr, mac, 6);
      esp_now_add_peer(&peerInfo);
    }
  };

  registerNode(macA, 'A');
  registerNode(macB, 'B');
  registerNode(macC, 'C');
  registerNode(macD, 'D');

  Serial.printf("✅ NODE %c READY\n", NODE_ID);
}

// ================= SIREN LOGIC =================
void handleInfiniteSiren() {
  if (digitalRead(STOP_BTN_PIN) == LOW) {
      isSirenActive = false;
      noTone(BUZZER_PIN);
      Serial.println("🔕 SIREN SILENCED");
      return;
  }
  tone(BUZZER_PIN, 1500); delay(200);
  tone(BUZZER_PIN, 2500); delay(200);
}

void playBeep() {
  tone(BUZZER_PIN, 2500); delay(150); noTone(BUZZER_PIN);
  delay(100);
  tone(BUZZER_PIN, 2500); delay(150); noTone(BUZZER_PIN);
}

// ================= LOOP =================

void loop() {
  if (isSirenActive) handleInfiniteSiren();

  if (triggerBeep) {
    playBeep();
    triggerBeep = false;
  }
  
  processDTN();
  
  unsigned long now = millis();
  
  // --- HEARTBEAT & BRIDGE SYNC ---
  if (now - lastHeartbeatTimer > HEARTBEAT_INTERVAL) {
    lastHeartbeatTimer = now;

    // 1. Tell Bridge "I am alive" -> This now includes "is_direct": true
    jsonPrint("heartbeat", NULL, -10);

    // 2. Send Packet to Mesh
    Packet hb;
    hb.packetId = micros() + random(0xFFFF);
    hb.packetType = TYPE_HEARTBEAT; 
    hb.src = NODE_ID;
    hb.dest = '*';        
    hb.lastHop = NODE_ID;
    hb.hopCount = 1;      
    hb.isSOS = isSirenActive;
    strcpy(hb.msg, "HB");

    uint8_t *allMacs[] = {macA, macB, macC, macD};
    char allIds[] = {'A', 'B', 'C', 'D'};
    
    for(int i=0; i<4; i++) {
      if(allIds[i] == NODE_ID) continue;
      esp_now_send(allMacs[i], (uint8_t*)&hb, sizeof(hb));
    }
  }

  // --- SERIAL INPUT (HANDLES MESSAGES FROM BRIDGE) ---
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    if (input == "SOS_OFF") {
        isSirenActive = false;
        noTone(BUZZER_PIN);
    } else if (input.length() > 0) {
        // Prepare Packet
        Packet pkt;
        pkt.packetId = micros() + random(0xFFFF); 
        pkt.packetType = TYPE_DATA; 
        pkt.src = NODE_ID;
        pkt.dest = '*'; 
        pkt.lastHop = NODE_ID;
        pkt.hopCount = 1; 
        
        if (input.startsWith("SOS")) {
          pkt.isSOS = true;
          sprintf(pkt.msg, "%s", input.c_str());
        } else if (input.indexOf(':') > 0) {
          pkt.dest = input.charAt(0);
          pkt.isSOS = false;
          String msgContent = input.substring(2);
          msgContent.toCharArray(pkt.msg, 64);
        } else {
           // Default Broadcast
           pkt.isSOS = false;
           input.toCharArray(pkt.msg, 64);
        }

        addToSeen(pkt.packetId);
        
        uint8_t *allMacs[] = {macA, macB, macC, macD};
        char allIds[] = {'A', 'B', 'C', 'D'};

        for(int i=0; i<4; i++) {
          if(allIds[i] == NODE_ID) continue;
          esp_now_send(allMacs[i], (uint8_t*)&pkt, sizeof(pkt));
        }
    }
  }

  if (routeLedActive && (millis() - routeLedTimer > 100)) {
    digitalWrite(ROUTE_LED, LOW);
    routeLedActive = false;
  }
}
