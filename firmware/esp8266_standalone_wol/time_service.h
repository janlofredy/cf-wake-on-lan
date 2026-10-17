#pragma once

#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#include "config.h"

// ==========================================
// Time & NTP Service State
// ==========================================
bool ntpSynchronized = false;
time_t bootEpoch = 0;
char bootTimeStr[20] = "Pending NTP Sync";
static unsigned long lastNtpCheckMillis = 0;

// Initialize SNTP client with active timezone offset
inline void initTimeService(int8_t tzHours) {
  Serial.printf("[NTP] Initializing SNTP with servers '%s', '%s' (Offset: UTC%+d)...\n",
                NTP_SERVER_1, NTP_SERVER_2, tzHours);
  configTime(tzHours * 3600, 0, NTP_SERVER_1, NTP_SERVER_2);
}

// Update timezone offset at runtime without restarting
inline void updateTimezone(int8_t tzHours) {
  Serial.printf("[NTP] Updating timezone offset to UTC%+d...\n", tzHours);
  configTime(tzHours * 3600, 0, NTP_SERVER_1, NTP_SERVER_2);
}

// Check if NTP has synchronized and calculate boot timestamp
inline void updateTimeService() {
  unsigned long nowMillis = millis();
  if (nowMillis - lastNtpCheckMillis < 1000) return;
  lastNtpCheckMillis = nowMillis;

  time_t now = time(nullptr);
  // Epoch > 1700000000 corresponds to post-Nov 2023
  if (now > 1700000000) {
    if (!ntpSynchronized) {
      ntpSynchronized = true;
      bootEpoch = now - (nowMillis / 1000);
      struct tm* bootTm = localtime(&bootEpoch);
      if (bootTm) {
        strftime(bootTimeStr, sizeof(bootTimeStr), "%Y-%m-%d %H:%M:%S", bootTm);
      }
      char curTimeStr[20];
      struct tm* curTm = localtime(&now);
      if (curTm) {
        strftime(curTimeStr, sizeof(curTimeStr), "%Y-%m-%d %H:%M:%S", curTm);
      } else {
        strcpy(curTimeStr, "Unknown");
      }
      Serial.printf("[NTP] Time successfully synchronized!\n  Current: %s\n  Boot Time: %s\n",
                    curTimeStr, bootTimeStr);
    }
  }
}

// Format current local time into "YYYY-MM-DD HH:MM:SS"
inline void getFormattedDateTime(char* outBuf, size_t maxLen) {
  if (!ntpSynchronized) {
    strncpy(outBuf, "Pending NTP Sync", maxLen - 1);
    outBuf[maxLen - 1] = '\0';
    return;
  }

  time_t now = time(nullptr);
  struct tm* tmInfo = localtime(&now);
  if (tmInfo) {
    strftime(outBuf, maxLen, "%Y-%m-%d %H:%M:%S", tmInfo);
  } else {
    strncpy(outBuf, "Sync Error", maxLen - 1);
    outBuf[maxLen - 1] = '\0';
  }
}

// Format system uptime into "Xd Xh Xm Xs"
inline void getFormattedUptime(char* outBuf, size_t maxLen) {
  unsigned long totalSec = millis() / 1000;
  unsigned long days = totalSec / 86400;
  unsigned long hours = (totalSec % 86400) / 3600;
  unsigned long mins = (totalSec % 3600) / 60;
  unsigned long secs = totalSec % 60;

  if (days > 0) {
    snprintf(outBuf, maxLen, "%lud %luh %lum %lus", days, hours, mins, secs);
  } else if (hours > 0) {
    snprintf(outBuf, maxLen, "%luh %lum %lus", hours, mins, secs);
  } else {
    snprintf(outBuf, maxLen, "%lum %lus", mins, secs);
  }
}

// Return whether NTP is locked
inline bool isNtpSynced() {
  return ntpSynchronized;
}
