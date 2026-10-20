#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include "config.h"
#include "storage.h"
#include "telemetry.h"
#include "time_service.h"

DNSServer dnsServer;
bool isApModeActive = false;
const byte DNS_PORT = 53;
IPAddress AP_IP(192, 168, 4, 1);
IPAddress AP_NETMASK(255, 255, 255, 0);

// Launch Setup Access Point and Captive Portal DNS
void startSetupAP() {
  if (isApModeActive) return;

  Serial.println(F("\n[AP] Launching Setup Access Point..."));
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_NETMASK);
  WiFi.softAP(SETUP_AP_SSID);

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", AP_IP);
  isApModeActive = true;

  Serial.printf("[AP] SSID: '%s' | Gateway: http://192.168.4.1\n", SETUP_AP_SSID);
  Serial.println(F("[AP] Connect your device to configure Wi-Fi and Cloudflare credentials."));
}

// Shut down Setup Access Point and return to Station-only mode
void stopSetupAP() {
  if (!isApModeActive) return;

  Serial.println(F("[AP] Shutting down Setup Access Point..."));
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  isApModeActive = false;
  Serial.println(F("[AP] Station mode active."));
}

// Connect to configured Wi-Fi in Station mode with 25s timeout
bool connectWiFi() {
  if (strlen(activeWifiSsid) == 0) {
    Serial.println(F("[WiFi] No Wi-Fi SSID configured."));
    return false;
  }

  WiFi.mode(WIFI_STA);
#ifdef DEVICE_HOSTNAME
  WiFi.hostname(DEVICE_HOSTNAME);
#else
  WiFi.hostname("wolstarter");
#endif

  Serial.printf("[WiFi] Connecting to SSID: '%s' (Hostname: %s)...\n",
                activeWifiSsid, WiFi.hostname().c_str());

  WiFi.begin(activeWifiSsid, activeWifiPass);

  unsigned long startT = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startT < WIFI_CONNECT_TIMEOUT_MS)) {
    delay(500);
    Serial.print(".");
    digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // Toggle activity LED
  }

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_PIN, HIGH); // Off when idle (Active LOW)
    Serial.printf("\n[WiFi] Connected!\n  IP Address: %s\n  Subnet: %s\n  Gateway: %s\n  RSSI: %d dBm (%d%%)\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.subnetMask().toString().c_str(),
                  WiFi.gatewayIP().toString().c_str(),
                  WiFi.RSSI(), getWifiQuality());
    return true;
  } else {
    digitalWrite(LED_PIN, HIGH);
    wifiReconnectCount++;
    Serial.printf("\n[WiFi] Failed to connect to '%s' within %lums.\n", activeWifiSsid, WIFI_CONNECT_TIMEOUT_MS);
    return false;
  }
}

// Verify new Wi-Fi credentials in the background while keeping the Setup AP alive
bool testWifiConnection(const char* ssid, const char* pass, unsigned long timeoutMs = 15000) {
  Serial.printf("[WiFi] Background verification for SSID: '%s'...\n", ssid);
#ifdef DEVICE_HOSTNAME
  WiFi.hostname(DEVICE_HOSTNAME);
#else
  WiFi.hostname("wolstarter");
#endif
  WiFi.begin(ssid, pass);

  unsigned long startT = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startT < timeoutMs)) {
    delay(200);
    dnsServer.processNextRequest(); // Keep servicing captive portal DNS
    yield();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Background verification successful! Local IP: %s\n",
                  WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.printf("[WiFi] Background verification failed for SSID: '%s'\n", ssid);
  return false;
}
