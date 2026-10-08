#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Copy ../config.h.example to config.h and fill in your details
#if __has_include("config.h")
  #include "config.h"
#else
  #include "../config.h.example"
#endif

// Status LED (GPIO 2 is built-in LED on most ESP32 Dev Boards)
#define LED_PIN 2

WiFiUDP udp;
unsigned long lastPollTime = 0;
byte defaultMac[6] = DEFAULT_SERVER_MAC;

// Parse MAC address string (e.g. "AA:BB:CC:DD:EE:FF" or "AA-BB-CC-DD-EE-FF")
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

// Send Wake-on-LAN Magic Packet (6x 0xFF followed by 16x Target MAC)
void sendWOL(const byte* macAddress) {
  byte magicPacket[102];
  memset(magicPacket, 0xFF, 6);
  for (int i = 1; i <= 16; i++) {
    memcpy(&magicPacket[i * 6], macAddress, 6);
  }

  // Broadcast address 255.255.255.255 on WOL port (typically 9)
  IPAddress broadcastIp(255, 255, 255, 255);
  udp.beginPacket(broadcastIp, WOL_PORT);
  udp.write(magicPacket, sizeof(magicPacket));
  udp.endPacket();

  Serial.println("[WOL] Magic packet sent successfully!");
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.printf("[WiFi] Connecting to %s...\n", WIFI_SSID);
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
  } else {
    digitalWrite(LED_PIN, LOW);
    Serial.println("\n[WiFi] Failed to connect. Will retry.");
  }
}

// Acknowledge trigger completion to Cloudflare
void acknowledgeTrigger() {
  HTTPClient http;
  String url = String(CF_WORKER_URL) + "/ack";
  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + AUTH_TOKEN);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST("{}");
  if (httpCode == HTTP_CODE_OK) {
    Serial.println("[Cloudflare] Trigger acknowledged and disarmed.");
  } else {
    Serial.printf("[Cloudflare] Ack failed, code: %d\n", httpCode);
  }
  http.end();
}

// Check Cloudflare Worker for WOL trigger
void pollCloudflare() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    return;
  }

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

      if (shouldTrigger) {
        Serial.println("[Trigger] Pending Wake-on-LAN trigger detected!");
        
        byte targetMac[6];
        if (customMac && strlen(customMac) > 0 && parseMac(customMac, targetMac)) {
          Serial.printf("[WOL] Using custom MAC: %s\n", customMac);
          sendWOL(targetMac);
        } else {
          Serial.println("[WOL] Using default MAC configured in config.h");
          sendWOL(defaultMac);
        }

        // Send multiple magic packets to ensure receipt
        delay(200);
        sendWOL(defaultMac);

        // Acknowledge to prevent duplicate triggers
        acknowledgeTrigger();
      }
    } else {
      Serial.print("[JSON] Deserialization error: ");
      Serial.println(error.c_str());
    }
  } else {
    Serial.printf("[HTTP] Poll failed with HTTP code: %d\n", httpCode);
  }
  http.end();
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- ESP32 Cloudflare Wake-on-LAN Controller ---");

  connectWiFi();
}

void loop() {
  // Always on Device loop: poll periodically
  unsigned long now = millis();
  if (now - lastPollTime >= POLL_INTERVAL_MS || lastPollTime == 0) {
    lastPollTime = now;
    pollCloudflare();
  }

  // Ensure WiFi stays connected
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  delay(100);
}
