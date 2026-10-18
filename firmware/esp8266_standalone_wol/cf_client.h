#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include "config.h"
#include "storage.h"
#include "telemetry.h"
#include "time_service.h"
#include "wol.h"

// Server Reachability State
enum ServerState { STATE_UNKNOWN, STATE_ONLINE, STATE_OFFLINE };

ServerState currentServerState  = STATE_UNKNOWN;
ServerState lastSentServerState = STATE_UNKNOWN;

unsigned long lastServerCheckTime = 0;
unsigned long lastCfSyncTime      = 0;
unsigned long lastCfKeepaliveTime = 0;

// Probe local target server availability via TCP socket
inline bool probeServer() {
  unsigned long startT = millis();
  IPAddress ip;
  bool isIp = ip.fromString(activeServerIp);

  WiFiClient client;
  client.setTimeout(2000); // 2 second timeout

  bool connected = false;
  if (isIp) {
    connected = client.connect(ip, activeServerPort);
  } else {
    connected = client.connect(activeServerIp, activeServerPort);
  }

  // Windows fallback: If checking port 22 (SSH) and it failed, try port 445 (Windows SMB)
  if (!connected && activeServerPort == 22) {
    if (isIp ? client.connect(ip, 445) : client.connect(activeServerIp, 445)) {
      connected = true;
    }
  }

  if (connected) {
    client.stop();
  }

  lastProbeLatencyMs = (uint16_t)(millis() - startT);
  totalProbes++;

  char timeBuf[20];
  if (isNtpSynced()) {
    getFormattedDateTime(timeBuf, sizeof(timeBuf));
  } else {
    char upStr[20];
    getFormattedUptime(upStr, sizeof(upStr));
    snprintf(timeBuf, sizeof(timeBuf), "Up: %s", upStr);
  }

  if (connected) {
    successProbes++;
    consecutiveSuccessProbes++;
    consecutiveFailProbes = 0;
    strncpy(lastSuccessProbeStr, timeBuf, sizeof(lastSuccessProbeStr) - 1);
    lastSuccessProbeStr[sizeof(lastSuccessProbeStr) - 1] = '\0';
    Serial.printf("[PROBE] [%s] %s:%u -> ONLINE (%ums)\n", activeServerName, activeServerIp, activeServerPort, lastProbeLatencyMs);
  } else {
    failedProbes++;
    consecutiveFailProbes++;
    consecutiveSuccessProbes = 0;
    strncpy(lastFailProbeStr, timeBuf, sizeof(lastFailProbeStr) - 1);
    lastFailProbeStr[sizeof(lastFailProbeStr) - 1] = '\0';
    Serial.printf("[PROBE] [%s] %s:%u -> OFFLINE (%ums)\n", activeServerName, activeServerIp, activeServerPort, lastProbeLatencyMs);
  }

  addRequestLog("LAN Probe", connected ? 200 : 0, lastProbeLatencyMs);
  return connected;
}

// Update server reachability status on Cloudflare Worker (/server/ping)
inline void updateServerStatusOnCloudflare(ServerState state) {
  if (strlen(activeCfHost) == 0) return;

  WiFiClient client;
  HTTPClient http;
  String url = "http://" + String(activeCfHost) + "/server/ping";

  unsigned long startT = millis();
  if (http.begin(client, url)) {
    http.setTimeout(4000);
    http.addHeader("Authorization", String("Bearer ") + activeAuthToken);
    http.addHeader("Content-Type", "application/json");

    String statusStr = (state == STATE_ONLINE) ? "online" : "offline";
    String payload   = "{\"status\":\"" + statusStr + "\""
                     + ",\"firmware_version\":\"" + String(FIRMWARE_VERSION) + "\""
                     + ",\"target_name\":\"" + String(activeServerName) + "\""
                     + ",\"target_mac\":\"" + String(activeMacStr) + "\""
                     + ",\"target_ip\":\"" + String(activeServerIp) + "\""
                     + "}";

    Serial.printf("[CF] Syncing state '%s' for '%s' (%s) to %s...\n",
                  statusStr.c_str(), activeServerName, activeMacStr, url.c_str());
    int httpCode = http.POST(payload);
    lastCfLatencyMs = (uint16_t)(millis() - startT);
    lastCfHttpCode = httpCode;
    totalCfSyncs++;

    char timeBuf[20];
    if (isNtpSynced()) {
      getFormattedDateTime(timeBuf, sizeof(timeBuf));
    } else {
      char upStr[20];
      getFormattedUptime(upStr, sizeof(upStr));
      snprintf(timeBuf, sizeof(timeBuf), "Up: %s", upStr);
    }

    if (httpCode == 200) {
      String resp = http.getString();
      int vIdx = resp.indexOf("\"worker_version\":\"");
      if (vIdx != -1) {
        int start = vIdx + 18;
        int end = resp.indexOf("\"", start);
        if (end != -1 && (end - start) < (int)sizeof(cfWorkerVersion)) {
          resp.substring(start, end).toCharArray(cfWorkerVersion, sizeof(cfWorkerVersion));
        }
      }
      successCfSyncs++;
      strncpy(lastSuccessCfSyncStr, timeBuf, sizeof(lastSuccessCfSyncStr) - 1);
      lastSuccessCfSyncStr[sizeof(lastSuccessCfSyncStr) - 1] = '\0';
      lastSentServerState = state;
      lastCfKeepaliveTime = millis();
      Serial.printf("[CF] /server/ping success (%ums, Worker: %s)\n", lastCfLatencyMs, cfWorkerVersion);
    } else {
      failedCfSyncs++;
      strncpy(lastFailCfSyncStr, timeBuf, sizeof(lastFailCfSyncStr) - 1);
      lastFailCfSyncStr[sizeof(lastFailCfSyncStr) - 1] = '\0';
      Serial.printf("[CF] HTTP POST failed: %d (%s)\n", httpCode, http.errorToString(httpCode).c_str());
    }

    addRequestLog("CF /ping", httpCode, lastCfLatencyMs);
    http.end();
  } else {
    Serial.println(F("[CF] Failed to connect HTTP client."));
  }
}

// Check Cloudflare Worker for pending WOL trigger (/status) and acknowledge (/ack)
inline void checkWOLTrigger() {
  if (strlen(activeCfHost) == 0) return;

  WiFiClient client;
  HTTPClient http;
  String url = "http://" + String(activeCfHost) + "/status";

  unsigned long startT = millis();
  if (http.begin(client, url)) {
    http.setTimeout(4000);
    http.addHeader("Authorization", String("Bearer ") + activeAuthToken);

    int httpCode = http.GET();
    lastCfLatencyMs = (uint16_t)(millis() - startT);
    lastCfHttpCode = httpCode;
    totalCfSyncs++;

    char timeBuf[20];
    if (isNtpSynced()) {
      getFormattedDateTime(timeBuf, sizeof(timeBuf));
    } else {
      char upStr[20];
      getFormattedUptime(upStr, sizeof(upStr));
      snprintf(timeBuf, sizeof(timeBuf), "Up: %s", upStr);
    }

    if (httpCode == 200) {
      successCfSyncs++;
      strncpy(lastSuccessCfSyncStr, timeBuf, sizeof(lastSuccessCfSyncStr) - 1);
      lastSuccessCfSyncStr[sizeof(lastSuccessCfSyncStr) - 1] = '\0';

      String payload = http.getString();
      int vIdx = payload.indexOf("\"worker_version\":\"");
      if (vIdx != -1) {
        int start = vIdx + 18;
        int end = payload.indexOf("\"", start);
        if (end != -1 && (end - start) < (int)sizeof(cfWorkerVersion)) {
          payload.substring(start, end).toCharArray(cfWorkerVersion, sizeof(cfWorkerVersion));
        }
      }
      if (payload.indexOf("\"trigger\":true") != -1) {
        Serial.println(F("[TRIGGER] Wake-on-LAN trigger detected from Cloudflare!"));
        sendWOLPacket(activeMac);
        delay(250);
        sendWOLPacket(activeMac);

        // Acknowledge trigger with Cloudflare
        WiFiClient ackClient;
        HTTPClient ackHttp;
        String ackUrl = "http://" + String(activeCfHost) + "/ack";
        unsigned long ackStartT = millis();
        if (ackHttp.begin(ackClient, ackUrl)) {
          ackHttp.setTimeout(3000);
          ackHttp.addHeader("Authorization", String("Bearer ") + activeAuthToken);
          ackHttp.addHeader("Content-Type", "application/json");
          int ackRes = ackHttp.POST("{\"result\":\"SENT\"}");
          uint16_t ackLatency = (uint16_t)(millis() - ackStartT);
          ackHttp.end();
          Serial.printf("[TRIGGER] Cloudflare trigger disarmed (code=%d, %ums)\n", ackRes, ackLatency);
          addRequestLog("CF /ack", ackRes, ackLatency);
        }
      }
    } else {
      failedCfSyncs++;
      strncpy(lastFailCfSyncStr, timeBuf, sizeof(lastFailCfSyncStr) - 1);
      lastFailCfSyncStr[sizeof(lastFailCfSyncStr) - 1] = '\0';
      Serial.printf("[TRIGGER] HTTP GET /status failed: %d (%s)\n", httpCode, http.errorToString(httpCode).c_str());
    }

    addRequestLog("CF /status", httpCode, lastCfLatencyMs);
    http.end();
  }
}
