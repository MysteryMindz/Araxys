#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

// ================= USER CONFIGURATION =================
#define WIFI_CHANNEL 1
#define NODE_ID 'C'      

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
#define HEARTBEAT_INTERVAL 20000 // 20 Seconds
#define NODE_TIMEOUT 25000       // 25s grace period
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

// --- NEIGHBOR ENTRY STRUCTURE ---
typedef struct {
  char id;
  int lastRssi;
  unsigned long lastSeen;
  bool isOnline;
} Neighbor;

// ================= GLOBALS =================
uint32_t seenPackets[DEDUP_SIZE];
int seenIndex = 0;
DTNEntry dtnBuffer[DTN_BUFFER_SIZE]; 
unsigned long lastDtnCheck = 0;

// Heartbeat Globals
unsigned long lastHeartbeatTimer = 0;

// Neighbor Table (Index 0=A, 1=B, 2=C, 3=D)
Neighbor neighborTable[4] = {
  {'A', 0, 0, false},
  {'B', 0, 0, false},
  {'C', 0, 0, false},
  {'D', 0, 0, false}
};

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

// --- NEIGHBOR TABLE UPDATER (SILENT) ---
void updateNeighbor(char id, int rssi) {
  int idx = -1;
  if (id == 'A') idx = 0;
  else if (id == 'B') idx = 1;
  else if (id == 'C') idx = 2;
  else if (id == 'D') idx = 3;

  if (idx != -1) {
    neighborTable[idx].lastRssi = rssi;
    neighborTable[idx].lastSeen = millis();
    neighborTable[idx].isOnline = true;
    // No print here - we wait for the table print
  }
}

// --- SYNCHRONIZED TABLE PRINTER ---
void printNetworkStatus() {
  unsigned long now = millis();
  
  Serial.println("\n================ NETWORK STATUS ================");
  Serial.println("| NODE |   STATUS   |  RSSI  | LAST SEEN (s) |");
  Serial.println("|------|------------|--------|---------------|");

  for (int i = 0; i < 4; i++) {
    // Skip myself
    if (neighborTable[i].id == NODE_ID) continue;

    // Check Timeout
    bool online = neighborTable[i].isOnline;
    unsigned long timeSince = (now - neighborTable[i].lastSeen) / 1000;
    
    if (online && (now - neighborTable[i].lastSeen > NODE_TIMEOUT)) {
        online = false;
        neighborTable[i].isOnline = false;
    }

    // Format the row
    String status = online ? "🟢 ONLINE " : "🔴 OFFLINE";
    String rssiStr = online ? String(neighborTable[i].lastRssi) : " -- ";
    
    Serial.printf("|  %c   | %s |  %s   |      %lu      |\n", 
                  neighborTable[i].id, 
                  status.c_str(), 
                  rssiStr.c_str(), 
                  timeSince);
  }
  Serial.println("================================================\n");
}

void sendHeartbeatPacket() {
  Packet hb;
  hb.packetId = micros() + random(0xFFFF);
  hb.packetType = TYPE_HEARTBEAT; 
  hb.src = NODE_ID;
  hb.dest = '*';       
  hb.lastHop = NODE_ID;
  hb.hopCount = 1;     
  hb.isSOS = false;
  strcpy(hb.msg, "HB");

  uint8_t *allMacs[] = {macA, macB, macC, macD};
  char allIds[] = {'A', 'B', 'C', 'D'};
  
  for(int i=0; i<4; i++) {
    if(allIds[i] == NODE_ID) continue;
    esp_now_send(allMacs[i], (uint8_t*)&hb, sizeof(hb));
  }
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

  dtnBuffer[slot].pkt = p;
  dtnBuffer[slot].storedAt = millis();
  dtnBuffer[slot].active = true;
  Serial.printf("💾 DTN STORED: ID %u (Slot %d)\n", p.packetId, slot);
}

void processDTN() {
  unsigned long now = millis();
  if (now - lastDtnCheck < DTN_RETRY_DELAY) return;
  lastDtnCheck = now;

  for (int i = 0; i < DTN_BUFFER_SIZE; i++) {
    if (dtnBuffer[i].active) {
      if (now - dtnBuffer[i].storedAt > DTN_PACKET_TTL) {
        dtnBuffer[i].active = false;
        Serial.printf("💀 DTN EXPIRED: ID %u\n", dtnBuffer[i].pkt.packetId);
        continue;
      }

      Serial.printf("♻️ DTN RETRY: ID %u\n", dtnBuffer[i].pkt.packetId);
      
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

  // --- 1. HEARTBEAT CHECK ---
  if (pkt.packetType == TYPE_HEARTBEAT) {
    updateNeighbor(sender, rssi);
    return; // Silent return, no logs
  }

  // --- NORMAL DATA PROCESSING BELOW ---
  if (isDuplicate(pkt.packetId)) return;
  addToSeen(pkt.packetId);
  
  Serial.println("-----------------------------");
  Serial.printf("📥 RECV: %c -> %c | Msg: %s\n", pkt.src, pkt.dest, pkt.msg);
  Serial.printf("   VIA: %c (RSSI: %d)\n", pkt.lastHop, rssi);
  Serial.printf("   HOPS: %d (%s)\n", pkt.hopCount, (pkt.hopCount == 1) ? "Direct" : "Relayed");

  if (pkt.dest == NODE_ID || pkt.dest == '*') {
    Serial.println("✅ DESTINATION REACHED");
    if (pkt.isSOS) {
        isSirenActive = true; 
    } else {
        triggerBeep = true;
    }
    if (pkt.dest != '*') return; 
  }

  if (pkt.hopCount < MAX_HOPS) {
    pkt.hopCount++;        
    pkt.lastHop = NODE_ID; 

    bool shouldRelayNow = false;

    if (pkt.isSOS) {
        Serial.println("🚨 SOS - IMMEDIATE RELAY");
        shouldRelayNow = true;
    } else if (rssi >= RSSI_THRESHOLD || rssi == 0) { 
        Serial.println("🔄 RELAYING...");
        shouldRelayNow = true;
    } else {
        Serial.printf("🐢 WEAK SIGNAL (%d) - STORING\n", rssi);
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
  } else {
      Serial.println("❌ MAX HOPS EXCEEDED");
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

  if (esp_now_init() != ESP_OK) {
    ESP.restart();
  }

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

  // Init table timestamps
  for(int i=0; i<4; i++) {
    neighborTable[i].lastSeen = millis(); 
  }

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
  for(int freq = 1000; freq < 3500; freq += 40) {
    if (digitalRead(STOP_BTN_PIN) == LOW) { 
        isSirenActive = false;
        noTone(BUZZER_PIN);
        Serial.println("🔕 SIREN SILENCED");
        return;
    }
    tone(BUZZER_PIN, freq);
    delay(4); 
  }
  for(int freq = 3500; freq > 1000; freq -= 40) {
    if (digitalRead(STOP_BTN_PIN) == LOW) { 
        isSirenActive = false;
        noTone(BUZZER_PIN);
        Serial.println("🔕 SIREN SILENCED");
        return;
    }
    tone(BUZZER_PIN, freq);
    delay(4); 
  }
}

void playBeep() {
  tone(BUZZER_PIN, 2500); 
  delay(150);
  noTone(BUZZER_PIN);
  delay(100);
  tone(BUZZER_PIN, 2500); 
  delay(150);
  noTone(BUZZER_PIN);
}

// ================= LOOP =================

void loop() {
  if (isSirenActive) handleInfiniteSiren();

  if (triggerBeep) {
    playBeep();
    triggerBeep = false;
  }
  
  processDTN();
  
  // --- SYNCHRONIZED STATUS SYSTEM ---
  unsigned long now = millis();
  
  if (now - lastHeartbeatTimer > HEARTBEAT_INTERVAL) {
    sendHeartbeatPacket();
    printNetworkStatus();
    
    lastHeartbeatTimer = now;
  }

  // --- SERIAL INPUT ---
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() > 0) sendUserMessage(input);
  }

  if (routeLedActive && (millis() - routeLedTimer > 100)) {
    digitalWrite(ROUTE_LED, LOW);
    routeLedActive = false;
  }
}

void sendUserMessage(String input) {
  Packet pkt;
  pkt.packetId = micros() + random(0xFFFF); 
  pkt.packetType = TYPE_DATA; 
  pkt.src = NODE_ID;
  pkt.dest = '*'; 
  pkt.lastHop = NODE_ID;
  pkt.hopCount = 1; 
  
  if (input.startsWith("SOS")) {
    pkt.dest = '*';
    pkt.isSOS = true;
    sprintf(pkt.msg, "%s", input.c_str());
  } else if (input.indexOf(':') > 0) {
    pkt.dest = input.charAt(0);
    pkt.isSOS = false;
    String msgContent = input.substring(2);
    msgContent.toCharArray(pkt.msg, 64);
  } else {
    Serial.println("Format: 'C:Hello' or 'SOS'");
    return;
  }

  addToSeen(pkt.packetId);
  Serial.printf("📤 SENDING to %c (ID: %u)\n", pkt.dest, pkt.packetId);

  uint8_t *allMacs[] = {macA, macB, macC, macD};
  char allIds[] = {'A', 'B', 'C', 'D'};

  for(int i=0; i<4; i++) {
    if(allIds[i] == NODE_ID) continue;
    esp_now_send(allMacs[i], (uint8_t*)&pkt, sizeof(pkt));
  }
}
