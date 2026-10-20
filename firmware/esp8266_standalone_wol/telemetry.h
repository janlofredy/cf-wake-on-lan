#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "time_service.h"

// ==========================================
// Rolling 10-Request Activity Log
// ==========================================
struct RequestLogEntry {
  char timeStr[20];   // "YYYY-MM-DD HH:MM:SS" or uptime if NTP pending
  char action[16];    // e.g. "CF /ping", "CF /status", "LAN Probe", "WOL Send"
  int16_t status;     // HTTP status code (200, 401, -1) or result (1/0)
  uint16_t latencyMs; // Round-trip latency in milliseconds
};

#define MAX_REQUEST_LOG 10
RequestLogEntry requestHistory[MAX_REQUEST_LOG];
uint8_t historyHead = 0;
uint8_t historyCount = 0;

// Add an entry to the in-memory circular log (Cloudflare requests only)
void addRequestLog(const char* action, int16_t status, uint16_t latencyMs) {
  if (strncmp(action, "CF ", 3) != 0) return;

  RequestLogEntry* entry = &requestHistory[historyHead];

  if (isNtpSynced()) {
    getFormattedDateTime(entry->timeStr, sizeof(entry->timeStr));
  } else {
    char upStr[20];
    getFormattedUptime(upStr, sizeof(upStr));
    snprintf(entry->timeStr, sizeof(entry->timeStr), "Up: %s", upStr);
  }

  strncpy(entry->action, action, sizeof(entry->action) - 1);
  entry->action[sizeof(entry->action) - 1] = '\0';
  entry->status = status;
  entry->latencyMs = latencyMs;

  historyHead = (historyHead + 1) % MAX_REQUEST_LOG;
  if (historyCount < MAX_REQUEST_LOG) {
    historyCount++;
  }
}

// ==========================================
// Telemetry Data State
// ==========================================
char resetReason[32] = "Power On";
uint32_t minFreeHeap = 0;
uint32_t wifiReconnectCount = 0;

char lastSuccessProbeStr[20] = "Never";
char lastFailProbeStr[20] = "Never";
uint32_t totalProbes = 0;
uint32_t successProbes = 0;
uint32_t failedProbes = 0;
uint32_t consecutiveSuccessProbes = 0;
uint32_t consecutiveFailProbes = 0;
uint16_t lastProbeLatencyMs = 0;

char lastSuccessCfSyncStr[20] = "Never";
char lastFailCfSyncStr[20] = "Never";
uint32_t totalCfSyncs = 0;
uint32_t successCfSyncs = 0;
uint32_t failedCfSyncs = 0;
int lastCfHttpCode = 0;
uint16_t lastCfLatencyMs = 0;

uint32_t totalWolDispatches = 0;
char lastWolDispatchStr[20] = "Never";

char cfWorkerVersion[16] = "Pending Sync";

// Initialize system telemetry at boot
void initTelemetry() {
  String reason = ESP.getResetReason();
  strncpy(resetReason, reason.c_str(), sizeof(resetReason) - 1);
  resetReason[sizeof(resetReason) - 1] = '\0';
  minFreeHeap = ESP.getFreeHeap();
}

// Monitor lowest free heap recorded to track memory leaks
void updateMinFreeHeap() {
  uint32_t currentFree = ESP.getFreeHeap();
  if (currentFree < minFreeHeap || minFreeHeap == 0) {
    minFreeHeap = currentFree;
  }
}

// Convert RSSI dBm to estimated signal quality percentage (0-100%)
int getWifiQuality() {
  int32_t rssi = WiFi.RSSI();
  if (rssi <= -100) return 0;
  if (rssi >= -50)  return 100;
  return 2 * (rssi + 100);
}

// Compute Server Probe success percentage
float getProbeSuccessRate() {
  if (totalProbes == 0) return 0.0f;
  return ((float)successProbes / (float)totalProbes) * 100.0f;
}

// Compute Cloudflare Sync success percentage
float getCfSyncSuccessRate() {
  if (totalCfSyncs == 0) return 0.0f;
  return ((float)successCfSyncs / (float)totalCfSyncs) * 100.0f;
}
