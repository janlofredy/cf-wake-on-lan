#include <Arduino.h>
#include "config.h"
#include "storage.h"
#include "time_service.h"
#include "telemetry.h"
#include "wol.h"
#include "cf_client.h"
#include "network_portal.h"
#include "web_server.h"

/**
 * ============================================================================
 * ESP8266 Standalone ServerStarter Firmware
 * ============================================================================
 * Hardware: NodeMCU 0.9 (ESP-12 Module) / ESP-12E Standalone
 * Core:     esp8266 by ESP8266 Community version 2.3.0
 *
 * In Arduino IDE:
 *   - Board: "NodeMCU 0.9 (ESP-12 Module)"
 *   - CPU Frequency: 80 MHz
 *   - Flash Size: 4M (3M SPIFFS) or 4M (1M SPIFFS)
 *   - Upload Speed: 115200 (or 921600)
 * ============================================================================
 */

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // Off initially (Active LOW)

  delay(1000);
  Serial.println(F("\n=============================================="));
  Serial.println(F("ESP8266 Standalone ServerStarter (Production)"));
  Serial.println(F("=============================================="));

  // 1. Initialize hardware telemetry
  initTelemetry();

  // 2. Load persistent configuration from EEPROM (falls back to config.h if empty)
  loadConfigFromEEPROM();

  // 3. Attempt Wi-Fi connection; if timeout or unconfigured, launch Setup AP
  bool wifiOk = connectWiFi();
  if (!wifiOk) {
    Serial.println(F("[Boot] Wi-Fi connection unavailable. Launching Setup AP..."));
    startSetupAP();
  }

  // 4. Initialize HTTP web server and name services (mDNS & NetBIOS)
  initWebServer();

  // 5. If Wi-Fi connected, start name services, NTP time service and initial sync
  if (wifiOk) {
    startNameServices();
    initTimeService(activeTzOffsetHours);

    // Initial server probe & Cloudflare sync
    bool isOnline = probeServer();
    currentServerState = isOnline ? STATE_ONLINE : STATE_OFFLINE;
    lastSentServerState = currentServerState;
    lastServerCheckTime = millis();
    updateServerStatusOnCloudflare(currentServerState);
  }
}

void loop() {
  // Service captive portal DNS if Setup AP is active
  if (isApModeActive) {
    dnsServer.processNextRequest();
  }

  // Handle local HTTP client requests and mDNS
  server.handleClient();
  MDNS.update();

  // Track Wi-Fi station connectivity transitions to re-announce name services on reconnect
  static bool wasConnected = false;
  bool isConnected = (WiFi.status() == WL_CONNECTED);
  if (isConnected && !wasConnected) {
    wasConnected = true;
    startNameServices();
  } else if (!isConnected && wasConnected) {
    wasConnected = false;
    wifiReconnectCount++;
    Serial.println(F("[WiFi] Station connection dropped."));
  }

  // Update NTP status and track minimum free heap
  updateTimeService();
  updateMinFreeHeap();

  // If running in Setup AP mode without station connection, skip polling loops
  if (!isConnected) {
    delay(10);
    return;
  }

  unsigned long currentMillis = millis();

  // 1. Periodic Local LAN Server Probe (using dynamic stored interval)
  unsigned long probeIntervalMs = (unsigned long)activeServerCheckIntervalSec * 1000UL;
  if (currentMillis - lastServerCheckTime >= probeIntervalMs || lastServerCheckTime == 0) {
    lastServerCheckTime = currentMillis;

    bool isOnline = probeServer();
    currentServerState = isOnline ? STATE_ONLINE : STATE_OFFLINE;

    // Immediate state transition sync to Cloudflare
    if (currentServerState != lastSentServerState) {
      Serial.printf("[STATE] Target transitioned to: %s\n", isOnline ? "ONLINE" : "OFFLINE");
      updateServerStatusOnCloudflare(currentServerState);
    }
  }

  // 2. Periodic Cloudflare Sync & Trigger Check (when server is offline)
  unsigned long cfSyncIntervalMs = (unsigned long)activeCfSyncIntervalSec * 1000UL;
  if (currentMillis - lastCfSyncTime >= cfSyncIntervalMs || lastCfSyncTime == 0) {
    lastCfSyncTime = currentMillis;

    if (currentServerState == STATE_OFFLINE || currentServerState == STATE_UNKNOWN) {
      checkWOLTrigger();
    }
  }

  // 3. Periodic Keepalive to Cloudflare (throttled to 15 min to conserve KV writes)
  if (currentMillis - lastCfKeepaliveTime >= CF_KEEPALIVE_INTERVAL_MS || lastCfKeepaliveTime == 0) {
    lastCfKeepaliveTime = currentMillis;
    updateServerStatusOnCloudflare(currentServerState);
  }

  delay(10);
}
