#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>

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

WiFiUDP udp;

enum ServerState { STATE_UNKNOWN, STATE_ONLINE, STATE_OFFLINE };
ServerState currentServerState  = STATE_UNKNOWN;
ServerState lastSentServerState = STATE_UNKNOWN;

unsigned long lastServerCheckTime = 0;
unsigned long lastCfSyncTime      = 0;

void blinkLed(int times, int ms) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, LOW); // Active LOW on ESP8266
    delay(ms);
    digitalWrite(LED_PIN, HIGH);
    delay(ms);
  }
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

// Send Wake-on-LAN UDP magic packet (102 bytes) to 255.255.255.255:9
void sendWOLPacket() {
  Serial.println(F("[WOL] Sending Wake-on-LAN magic packet..."));
  
  byte magicPacket[102];
  memset(magicPacket, 0xFF, 6);

  byte mac[6] = {0};
  if (parseMacAddress(SERVER_MAC, mac)) {
    for (int i = 0; i < 16; i++) {
      for (int m = 0; m < 6; m++) {
        magicPacket[6 + (i * 6) + m] = mac[m];
      }
    }

    IPAddress broadcastIp(255, 255, 255, 255);
    udp.beginPacket(broadcastIp, 9);
    udp.write(magicPacket, sizeof(magicPacket));
    udp.endPacket();

    Serial.println(F("[WOL] Magic packet transmitted successfully!"));
    blinkLed(3, 100);
  } else {
    Serial.println(F("[WOL] ERROR: Failed to parse SERVER_MAC string!"));
  }
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.printf("[WiFi] Connecting to SSID: %s\n", WIFI_SSID);
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
  } else {
    Serial.println(F("\n[WiFi] Connection timeout. Retrying later..."));
  }
}

// Probe local target server availability via TCP socket (port 22 / SSH)
bool probeServer() {
  WiFiClient client;
  client.setTimeout(1000);
  if (client.connect(SERVER_IP, SERVER_PORT)) {
    client.stop();
    return true;
  }
  return false;
}

// Update server status on Cloudflare Worker
void updateServerStatusOnCloudflare(ServerState state) {
  WiFiClientSecure client;
  client.setInsecure(); // Skip certificate thumbprint validation for simple worker pings

  HTTPClient http;
  String url = "https://" + String(CF_HOST) + "/server/ping";

  if (http.begin(client, url)) {
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
      Serial.printf("[CF] HTTP POST failed: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  }
}

// Check Cloudflare for pending WOL trigger (when server is offline)
void checkWOLTrigger() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = "https://" + String(CF_HOST) + "/status";

  if (http.begin(client, url)) {
    http.addHeader("Authorization", String("Bearer ") + AUTH_TOKEN);

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      if (payload.indexOf("\"trigger\":true") != -1) {
        Serial.println(F("[TRIGGER] Wake-on-LAN trigger detected from Cloudflare!"));
        sendWOLPacket();
        delay(300);
        sendWOLPacket();

        // Acknowledge trigger
        HTTPClient ackHttp;
        String ackUrl = "https://" + String(CF_HOST) + "/server/acknowledge";
        if (ackHttp.begin(client, ackUrl)) {
          ackHttp.addHeader("Authorization", String("Bearer ") + AUTH_TOKEN);
          ackHttp.POST("");
          ackHttp.end();
          Serial.println(F("[TRIGGER] Acknowledged with Cloudflare."));
        }
      }
    }
    http.end();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // LED off initially

  delay(1000);
  Serial.println(F("\n========================================"));
  Serial.println(F("ESP8266 Standalone ServerStarter v1.0"));
  Serial.println(F("========================================"));

  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    delay(2000);
    return;
  }

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

  delay(100);
}
