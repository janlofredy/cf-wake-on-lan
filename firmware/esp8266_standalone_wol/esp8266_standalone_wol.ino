#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <NetBIOS.h>
#include <EEPROM.h>

/**
 * ============================================================================
 * ESP8266 ESP-12E Standalone ServerStarter Firmware
 * ============================================================================
 * Runs natively on the ESP-12E module on the WiFi Shield!
 * 
 * Hardware Settings to FLASH:
 *   - Arduino Uno RESET pin connected to GND (silences Uno MCU)
 *   - Shield DIP switches: 1: OFF, 2: OFF, 3: ON, 4: ON
 *   - Jumpers: Shield TX -> Uno D1 (or D0), Shield RX -> Uno D0 (or D1)
 *   - Press ESP-RST button before upload
 *   - In Arduino IDE: Board -> "Generic ESP8266 Module", Flash Size -> "4MB (FS:2MB OTA:~1019KB)" or similar
 * 
 * Hardware Settings to RUN (after flashing):
 *   - Shield DIP switches: 1: ON, 2: ON, 3: OFF, 4: OFF (or keep jumpers/reset)
 *   - Press ESP-RST button to start running
 * ============================================================================
 */
#if defined(__has_include)
  #if __has_include("config.h")
    #include "config.h"
  #else
    #error "Missing config.h! Please copy config.h.example to config.h and configure your settings."
  #endif
#else
  #include "config.h"
#endif

// Onboard LED on ESP-12 is usually GPIO 2 (active LOW)
#define LED_PIN 2

// EEPROM Persistent Storage Configuration
#define EEPROM_SIZE         64
#define EEPROM_MAGIC_ADDR   0
#define EEPROM_MAC_ADDR     1
#define EEPROM_MAGIC_BYTE   0xA5

byte activeMac[6] = {0};
char activeMacStr[18] = "";

WiFiUDP udp;
ESP8266WebServer server(80);

enum ServerState { STATE_UNKNOWN, STATE_ONLINE, STATE_OFFLINE };
ServerState currentServerState  = STATE_UNKNOWN;
ServerState lastSentServerState = STATE_UNKNOWN;

unsigned long lastServerCheckTime = 0;
unsigned long lastCfSyncTime      = 0;
unsigned long lastWolSentEpoch    = 0;

void blinkLed(int times, int ms) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, LOW); // Active LOW on ESP8266
    delay(ms);
    digitalWrite(LED_PIN, HIGH);
    delay(ms);
  }
}

// Convert 6-byte MAC array to formatted string "XX:XX:XX:XX:XX:XX"
void macBytesToString(const byte* macBytes, char* outStr) {
  sprintf(outStr, "%02x:%02x:%02x:%02x:%02x:%02x",
          macBytes[0], macBytes[1], macBytes[2],
          macBytes[3], macBytes[4], macBytes[5]);
}

// Parse MAC address string (e.g. "00:11:22:33:44:55") into 6 bytes without sscanf
bool parseMacAddress(const char* macStr, byte* macBytes) {
  byte byteIdx = 0;
  byte val = 0;
  bool hasNibble = false;

  for (int i = 0; macStr[i] != '\0' && byteIdx < 6; i++) {
    char c = macStr[i];
    int nibble = -1;
    if (c >= '0' && c <= '9') nibble = c - '0';
    else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;

    if (nibble != -1) {
      val = (val << 4) | (byte)nibble;
      if (hasNibble) {
        macBytes[byteIdx++] = val;
        val = 0;
        hasNibble = false;
      } else {
        hasNibble = true;
      }
    }
  }
  return (byteIdx == 6);
}

// Save MAC to persistent EEPROM storage (only writes if changed to prevent flash wear)
bool saveMacToEEPROM(const byte* newMac) {
  bool changed = false;
  byte magic = EEPROM.read(EEPROM_MAGIC_ADDR);
  if (magic != EEPROM_MAGIC_BYTE) {
    EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_BYTE);
    changed = true;
  }

  for (int i = 0; i < 6; i++) {
    if (EEPROM.read(EEPROM_MAC_ADDR + i) != newMac[i]) {
      EEPROM.write(EEPROM_MAC_ADDR + i, newMac[i]);
      changed = true;
    }
  }

  if (changed) {
    bool ok = EEPROM.commit();
    if (ok) {
      memcpy(activeMac, newMac, 6);
      macBytesToString(activeMac, activeMacStr);
      Serial.printf("[STORAGE] Target MAC saved to persistent EEPROM: %s\n", activeMacStr);
      return true;
    } else {
      Serial.println(F("[STORAGE] ERROR: EEPROM commit failed!"));
      return false;
    }
  }
  return true;
}

// Load MAC from persistent EEPROM (or fallback to SERVER_MAC in config.h)
void loadMacFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  byte magic = EEPROM.read(EEPROM_MAGIC_ADDR);

  if (magic == EEPROM_MAGIC_BYTE) {
    for (int i = 0; i < 6; i++) {
      activeMac[i] = EEPROM.read(EEPROM_MAC_ADDR + i);
    }
    macBytesToString(activeMac, activeMacStr);
    Serial.printf("[STORAGE] Loaded Target MAC from EEPROM: %s\n", activeMacStr);
  } else {
    Serial.println(F("[STORAGE] EEPROM uninitialized. Initializing with default SERVER_MAC from config.h..."));
    if (parseMacAddress(SERVER_MAC, activeMac)) {
      saveMacToEEPROM(activeMac);
    } else {
      Serial.println(F("[STORAGE] ERROR: Invalid default SERVER_MAC!"));
    }
  }
}

// Send Wake-on-LAN UDP magic packet (102 bytes) to subnet broadcast using active configured MAC
void sendWOLPacket() {
  Serial.printf("[WOL] Preparing Wake-on-LAN packet for MAC: %s\n", activeMacStr);
  
  byte magicPacket[102];
  memset(magicPacket, 0xFF, 6);

  for (int i = 0; i < 16; i++) {
    for (int m = 0; m < 6; m++) {
      magicPacket[6 + (i * 6) + m] = activeMac[m];
    }
  }

    // Ensure UDP socket is initialized and bound
    udp.begin(9);

    // Calculate subnet directed broadcast (e.g. 192.168.1.255)
    IPAddress subnetBroadcast = ~WiFi.subnetMask() | WiFi.localIP();
    IPAddress limitedBroadcast(255, 255, 255, 255);

    // 1. Send to Subnet Broadcast on Port 9
    udp.beginPacket(subnetBroadcast, 9);
    udp.write(magicPacket, sizeof(magicPacket));
    int res1 = udp.endPacket();

    // 2. Send to Subnet Broadcast on Port 7
    udp.beginPacket(subnetBroadcast, 7);
    udp.write(magicPacket, sizeof(magicPacket));
    int res2 = udp.endPacket();

    // 3. Also send to 255.255.255.255 on Port 9
    udp.beginPacket(limitedBroadcast, 9);
    udp.write(magicPacket, sizeof(magicPacket));
    int res3 = udp.endPacket();

    Serial.printf("[WOL] Magic packet sent! Subnet %s:9 (ok=%d), :7 (ok=%d), 255.255.255.255:9 (ok=%d)\n", 
                  subnetBroadcast.toString().c_str(), res1, res2, res3);
    blinkLed(3, 100);
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

#ifdef DEVICE_HOSTNAME
  WiFi.hostname(DEVICE_HOSTNAME);
#else
  WiFi.hostname("serverstarter");
#endif

  Serial.printf("[WiFi] Connecting to SSID: %s (Hostname: %s)...\n", 
                WIFI_SSID, WiFi.hostname().c_str());
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
    digitalWrite(LED_PIN, HIGH); // Off when idle
    Serial.print(F("\n[WiFi] Connected! IP Address: "));
    Serial.println(WiFi.localIP());

    // Initialize mDNS (Apple Bonjour / Linux Avahi / Windows 10+ mDNS)
    const char* hName = WiFi.hostname().c_str();
    if (MDNS.begin(hName)) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("[mDNS] Responder started: http://%s.local\n", hName);
    } else {
      Serial.println(F("[mDNS] Error setting up MDNS responder!"));
    }

    // Initialize NetBIOS (Windows legacy name resolution: http://<hostname>)
    NBNS.begin(hName);
    Serial.printf("[NetBIOS] Registered name for Windows: http://%s\n", hName);

    udp.begin(9); // Initialize UDP listener/sender
  } else {
    Serial.println(F("\n[WiFi] Connection timeout. Retrying later..."));
  }
}

// Probe local target server availability via TCP socket
bool probeServer() {
  IPAddress ip;
  bool isIp = ip.fromString(SERVER_IP);

  WiFiClient client;
  client.setTimeout(2000); // 2 second timeout

  bool connected = false;
  if (isIp) {
    connected = client.connect(ip, SERVER_PORT);
  } else {
    connected = client.connect(SERVER_IP, SERVER_PORT);
  }

  if (connected) {
    client.stop();
    Serial.printf("[PROBE] %s:%d -> ONLINE\n", SERVER_IP, SERVER_PORT);
    return true;
  }

  // Windows fallback: If checking port 22 (SSH) and it failed, try port 445 (Windows SMB)
  if (SERVER_PORT == 22) {
    if (isIp ? client.connect(ip, 445) : client.connect(SERVER_IP, 445)) {
      client.stop();
      Serial.printf("[PROBE] %s:445 (Windows SMB) -> ONLINE\n", SERVER_IP);
      return true;
    }
  }

  Serial.printf("[PROBE] %s:%d -> OFFLINE\n", SERVER_IP, SERVER_PORT);
  return false;
}

// Update server status on Cloudflare Worker (via plain HTTP for Core 2.3.0 stability)
void updateServerStatusOnCloudflare(ServerState state) {
  HTTPClient http;
  String url = "http://" + String(CF_HOST) + "/server/ping";

  if (http.begin(url)) {
    http.addHeader("Authorization", String("Bearer ") + AUTH_TOKEN);
    http.addHeader("Content-Type", "application/json");

    String statusStr = (state == STATE_ONLINE) ? "online" : "offline";
    String payload   = "{\"status\":\"" + statusStr + "\"}";

    Serial.printf("[CF] Syncing state '%s' to %s...\n", statusStr.c_str(), url.c_str());
    int httpCode = http.POST(payload);

    if (httpCode > 0) {
      Serial.printf("[CF] Response: %d\n", httpCode);
      lastSentServerState = state;
    } else {
      Serial.printf("[CF] HTTP POST failed, code: %d (%s)\n", 
                    httpCode, http.errorToString(httpCode).c_str());
    }
    http.end();
  } else {
    Serial.println(F("[CF] Failed to connect HTTP client."));
  }
}

// Check Cloudflare for pending WOL trigger (when server is offline)
void checkWOLTrigger() {
  HTTPClient http;
  String url = "http://" + String(CF_HOST) + "/status";

  if (http.begin(url)) {
    http.addHeader("Authorization", String("Bearer ") + AUTH_TOKEN);

    int httpCode = http.GET();
    if (httpCode == 200) {
      String payload = http.getString();
      if (payload.indexOf("\"trigger\":true") != -1) {
        Serial.println(F("[TRIGGER] Wake-on-LAN trigger detected from Cloudflare!"));
        sendWOLPacket();
        delay(300);
        sendWOLPacket();

        // Acknowledge trigger
        HTTPClient ackHttp;
        String ackUrl = "http://" + String(CF_HOST) + "/ack";
        if (ackHttp.begin(ackUrl)) {
          ackHttp.addHeader("Authorization", String("Bearer ") + AUTH_TOKEN);
          ackHttp.addHeader("Content-Type", "application/json");
          ackHttp.POST("{\"result\":\"SENT\"}");
          ackHttp.end();
          Serial.println(F("[TRIGGER] Acknowledged with Cloudflare."));
        }
      }
    } else if (httpCode < 0) {
      Serial.printf("[TRIGGER] HTTP GET failed, code: %d (%s)\n", 
                    httpCode, http.errorToString(httpCode).c_str());
    }
    http.end();
  }
}

// Local ESP8266 Web Server handlers (Ultra-lightweight plain HTML for low storage & RAM)
void handleRoot() {
  String html = F("<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>ESP8266 WOL</title>"
                  "<style>body{font-family:sans-serif;margin:20px;background:#111;color:#eee}"
                  "h2{margin-bottom:10px}.box{background:#222;padding:15px;border-radius:6px;max-width:320px}"
                  "p{margin:6px 0}input{width:100%;box-sizing:border-box;padding:8px;margin-top:4px;border:1px solid #444;background:#333;color:#eee;border-radius:4px}"
                  "button{background:#0284c7;color:#fff;border:none;padding:10px 18px;font-size:15px;border-radius:4px;cursor:pointer;width:100%;margin-top:10px}"
                  "a{color:#38bdf8}</style></head><body>"
                  "<div class='box'><h2>ESP8266 WOL Device</h2>"
                  "<p><b>Server:</b> ");
  html += (currentServerState == STATE_ONLINE) ? F("<span style='color:#4ade80'>ONLINE</span>") : (currentServerState == STATE_OFFLINE ? F("<span style='color:#f87171'>OFFLINE</span>") : F("UNKNOWN"));
  html += F("</p><p><b>Target IP:</b> ");
  html += SERVER_IP;
  html += F("</p><p><b>Target MAC:</b> ");
  html += activeMacStr;
  html += F("</p><p><b>Free Heap:</b> ");
  html += String(ESP.getFreeHeap());
  html += F(" bytes</p><p style='font-size:12px;color:#94a3b8'><b>Access URLs:</b><br>"
            "&bull; Mac / Linux / iOS: <a href='http://");
  html += WiFi.hostname();
  html += F(".local'>http://");
  html += WiFi.hostname();
  html += F(".local</a><br>&bull; Windows: <a href='http://");
  html += WiFi.hostname();
  html += F("'>http://");
  html += WiFi.hostname();
  html += F("</a></p>");
  if (lastWolSentEpoch > 0 && (millis() - lastWolSentEpoch < 30000)) {
    html += F("<p style='background:#0369a1;padding:8px;border-radius:4px;color:#fff;font-size:13px;margin:8px 0'>⚡ WOL Packet Sent!</p>");
  }
  html += F("<form method='POST' action='/trigger'><button type='submit'>Wake Server (Send WOL)</button></form>"
            "<hr style='border:0;border-top:1px solid #333;margin:15px 0'>"
            "<form method='POST' action='/update-mac'>"
            "<p style='font-size:13px;font-weight:bold;margin-bottom:2px'>Configure Target MAC:</p>"
            "<input type='text' name='mac' value='");
  html += activeMacStr;
  html += F("' maxlength='17' required pattern='^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$'>"
            "<button type='submit' style='background:#475569'>Save to Persistent Storage</button>"
            "</form>"
            "<p style='margin-top:12px;font-size:12px'><a href='/'>Refresh</a></p>"
            "</div></body></html>");
  server.send(200, "text/html", html);
}

void handleManualTrigger() {
  Serial.println(F("[HTTP] Manual WOL trigger requested via ESP web server."));
  sendWOLPacket();
  delay(200);
  sendWOLPacket();
  lastWolSentEpoch = millis();

  // Redirect back to root
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleUpdateMac() {
  if (server.hasArg("mac")) {
    String newMacStr = server.arg("mac");
    newMacStr.trim();
    byte newMac[6];
    if (parseMacAddress(newMacStr.c_str(), newMac)) {
      saveMacToEEPROM(newMac);
      Serial.printf("[HTTP] Target MAC updated via web UI: %s\n", activeMacStr);
    } else {
      Serial.println(F("[HTTP] Invalid MAC received via web UI!"));
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleStatusJson() {
  String json = "{\"server\":\"";
  json += (currentServerState == STATE_ONLINE) ? "ONLINE" : (currentServerState == STATE_OFFLINE ? "OFFLINE" : "UNKNOWN");
  json += "\",\"ip\":\"" + String(SERVER_IP) + "\",\"mac\":\"" + String(activeMacStr) + "\",\"free_heap\":" + String(ESP.getFreeHeap()) + "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // LED off initially

  delay(1000);
  Serial.println(F("\n========================================"));
  Serial.println(F("ESP8266 Standalone ServerStarter v1.0"));
  Serial.println(F("========================================"));

  // Initialize and load persistent Target MAC from EEPROM
  loadMacFromEEPROM();

  connectWiFi();

  // Start local plain HTML web server on port 80
  server.on("/", HTTP_GET, handleRoot);
  server.on("/trigger", HTTP_POST, handleManualTrigger);
  server.on("/update-mac", HTTP_POST, handleUpdateMac);
  server.on("/status", HTTP_GET, handleStatusJson);
  server.begin();
  Serial.print(F("[Web] Local HTTP server active at: http://"));
  Serial.println(WiFi.localIP());

  // Probe target server immediately upon boot so state is not UNKNOWN
  bool isOnline = probeServer();
  currentServerState = isOnline ? STATE_ONLINE : STATE_OFFLINE;
  lastSentServerState = currentServerState;
  lastServerCheckTime = millis();
  updateServerStatusOnCloudflare(currentServerState);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    delay(2000);
    return;
  }

  // Handle incoming HTTP client requests on ESP
  server.handleClient();
  MDNS.update();

  unsigned long currentMillis = millis();

  // 1. Probe local LAN server every SERVER_CHECK_INTERVAL_MS
  if (currentMillis - lastServerCheckTime >= SERVER_CHECK_INTERVAL_MS) {
    lastServerCheckTime = currentMillis;

    bool isOnline = probeServer();
    currentServerState = isOnline ? STATE_ONLINE : STATE_OFFLINE;

    // Immediate state change sync
    if (currentServerState != lastSentServerState) {
      Serial.printf("[STATE] Target transitioned to: %s\n", isOnline ? "ONLINE" : "OFFLINE");
      updateServerStatusOnCloudflare(currentServerState);
    }
  }

  // 2. Periodic sync & WOL polling
  if (currentMillis - lastCfSyncTime >= CF_SYNC_INTERVAL_MS) {
    lastCfSyncTime = currentMillis;

    // Periodic heartbeat sync
    updateServerStatusOnCloudflare(currentServerState);

    // If server is currently offline, check if user requested a wake-up
    if (currentServerState == STATE_OFFLINE) {
      checkWOLTrigger();
    }
  }

  delay(10);
}
