#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <ESPmDNS.h>

#if defined(__has_include)
  #if __has_include("config.h")
    #include "config.h"
  #else
    #error "Missing config.h! Please copy config.h.example to config.h and configure your settings."
  #endif
#else
  #include "config.h"
#endif

#define LED_PIN 2

WiFiUDP udp;
Preferences prefs;
byte activeMac[6] = DEFAULT_SERVER_MAC;
char activeMacStr[18] = "";

// State Memory
enum ServerState { STATE_UNKNOWN, STATE_ONLINE, STATE_OFFLINE };
ServerState currentServerState  = STATE_UNKNOWN;
ServerState lastSentServerState = STATE_UNKNOWN;

// Timers (Two separate polling intervals)
unsigned long lastServerCheckTime = 0;
unsigned long lastCfSyncTime      = 0;

// Parse MAC address
bool parseMac(const char* macStr, byte* macBytes) {
  int values[6];
  if (6 == sscanf(macStr, "%x:%x:%x:%x:%x:%x",
                  &values[0], &values[1], &values[2],
                  &values[3], &values[4], &values[5]) ||
      6 == sscanf(macStr, "%x-%x-%x-%x-%x-%x",
                  &values[0], &values[1], &values[2],
                  &values[3], &values[4], &values[5])) {
    for (int i = 0; i < 6; ++i) {
      macBytes[i] = (byte)values[i];
    }
    return true;
  }
  return false;
}

// Send Wake-on-LAN Magic Packet
void sendWOL(const byte* macAddress) {
  byte magicPacket[102];
  memset(magicPacket, 0xFF, 6);
  for (int i = 1; i <= 16; i++) {
    memcpy(&magicPacket[i * 6], macAddress, 6);
  }

  IPAddress broadcastIp(255, 255, 255, 255);
  udp.beginPacket(broadcastIp, WOL_PORT);
  udp.write(magicPacket, sizeof(magicPacket));
  udp.endPacket();

  Serial.println("[WOL] Magic packet sent successfully!");
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

#ifdef DEVICE_HOSTNAME
  const char* hName = DEVICE_HOSTNAME;
#else
  const char* hName = "serverstarter";
#endif
  WiFi.setHostname(hName);

  Serial.printf("[WiFi] Connecting to %s (Hostname: %s)...\n", WIFI_SSID, hName);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_PIN, HIGH);
    Serial.println("\n[WiFi] Connected! IP: " + WiFi.localIP().toString());

    // Initialize mDNS (http://<hostname>.local)
    if (MDNS.begin(hName)) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("[mDNS] Device hostname active: http://%s.local\n", hName);
    }
  } else {
    digitalWrite(LED_PIN, LOW);
    Serial.println("\n[WiFi] Failed to connect.");
  }
}

// Probe local server reachability using TCP socket
bool probeServer() {
  WiFiClient client;
  client.setTimeout(1); // 1-second timeout
  if (client.connect(SERVER_IP, SERVER_PORT)) {
    client.stop();
    return true;
  }
  return false;
}

// Update server status on Cloudflare Worker
void updateServerStatusOnCloudflare(ServerState state) {
  HTTPClient http;
  String url = String(CF_WORKER_URL) + "/server/ping";
  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + AUTH_TOKEN);
  http.addHeader("Content-Type", "application/json");

  String body = (state == STATE_ONLINE) ? "{\"status\":\"ONLINE\"}" : "{\"status\":\"OFFLINE\"}";
  int httpCode = http.POST(body);
  if (httpCode == HTTP_CODE_OK) {
    Serial.printf("[Cloudflare] Server status updated to: %s\n", (state == STATE_ONLINE) ? "ONLINE" : "OFFLINE");
  }
  http.end();
}

// Acknowledge trigger completion to Cloudflare
void acknowledgeTrigger() {
  HTTPClient http;
  String url = String(CF_WORKER_URL) + "/ack";
  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + AUTH_TOKEN);
  http.addHeader("Content-Type", "application/json");

  http.POST("{\"result\":\"SENT\"}");
  http.end();
}

// Convert 6-byte MAC array to formatted string "XX:XX:XX:XX:XX:XX"
void macBytesToString(const byte* macBytes, char* outStr) {
  sprintf(outStr, "%02x:%02x:%02x:%02x:%02x:%02x",
          macBytes[0], macBytes[1], macBytes[2],
          macBytes[3], macBytes[4], macBytes[5]);
}

// Save MAC to persistent flash storage via Preferences (NVS)
bool saveTargetMac(const byte* newMac) {
  if (memcmp(activeMac, newMac, 6) != 0) {
    memcpy(activeMac, newMac, 6);
    macBytesToString(activeMac, activeMacStr);
    prefs.begin("wol_config", false);
    prefs.putBytes("mac", activeMac, 6);
    prefs.end();
    Serial.printf("[STORAGE] Target MAC saved to Preferences: %s\n", activeMacStr);
    return true;
  }
  return false;
}

// Load MAC from persistent storage (or fallback to DEFAULT_SERVER_MAC)
void loadTargetMac() {
  prefs.begin("wol_config", true);
  size_t len = prefs.getBytes("mac", activeMac, 6);
  prefs.end();

  if (len == 6) {
    macBytesToString(activeMac, activeMacStr);
    Serial.printf("[STORAGE] Loaded Target MAC from Preferences: %s\n", activeMacStr);
  } else {
    macBytesToString(activeMac, activeMacStr);
    Serial.printf("[STORAGE] Using default Target MAC: %s\n", activeMacStr);
    // Persist default to NVS
    prefs.begin("wol_config", false);
    prefs.putBytes("mac", activeMac, 6);
    prefs.end();
  }
}

// Check Cloudflare Worker for WOL trigger
void checkWOLTrigger() {
  HTTPClient http;
  String url = String(CF_WORKER_URL) + "/status";
  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + AUTH_TOKEN);

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      bool shouldTrigger = doc["trigger"];
      const char* customMac = doc["mac"];

      // Sync MAC from Cloudflare KV if provided and valid
      if (customMac && strlen(customMac) > 0) {
        byte remoteMac[6];
        if (parseMac(customMac, remoteMac)) {
          saveTargetMac(remoteMac);
        }
      }

      if (shouldTrigger) {
        Serial.printf("[Trigger] Active Wake-on-LAN trigger detected for MAC: %s\n", activeMacStr);
        sendWOL(activeMac);
        delay(200);
        sendWOL(activeMac); // Send twice for reliability
        acknowledgeTrigger();
      }
    }
  }
  http.end();
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- ESP32 Dual-Interval Wake-on-LAN Controller ---");

  // Load persistent MAC address from flash storage
  loadTargetMac();

  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    return;
  }

  unsigned long now = millis();

  // --- INTERVAL 1: Poll Local Server Status ---
  if (now - lastServerCheckTime >= SERVER_CHECK_INTERVAL_MS || lastServerCheckTime == 0) {
    lastServerCheckTime = now;
    bool isAlive = probeServer();
    currentServerState = isAlive ? STATE_ONLINE : STATE_OFFLINE;
    Serial.printf("[Server Probe] %s\n", isAlive ? "ONLINE" : "OFFLINE");
  }

  // --- INTERVAL 2: Cloudflare Synchronization ---
  if (now - lastCfSyncTime >= CF_SYNC_INTERVAL_MS || lastCfSyncTime == 0) {
    lastCfSyncTime = now;

    // 1. If status changed or periodic refresh (~60s), ping Cloudflare to keep AOD health alive
    static unsigned long lastSentTime = 0;
    bool statusChanged = (currentServerState != lastSentServerState && currentServerState != STATE_UNKNOWN);
    bool periodicRefresh = (now - lastSentTime >= 60000 || lastSentTime == 0);

    if (statusChanged || periodicRefresh) {
      Serial.println("[AOD Health/Status] Pinging Cloudflare with server status...");
      updateServerStatusOnCloudflare(currentServerState);
      lastSentServerState = currentServerState;
      lastSentTime = now;
    }

    // 2. When server is OFF (or initial state), check if there is a trigger
    if (currentServerState == STATE_OFFLINE || currentServerState == STATE_UNKNOWN) {
      Serial.println("[Server is OFF] Checking Cloudflare for pending triggers...");
      checkWOLTrigger();
    }
  }

  delay(50);
}
