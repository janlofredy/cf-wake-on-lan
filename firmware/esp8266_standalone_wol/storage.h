#pragma once

#include <Arduino.h>
#include <EEPROM.h>
#include "config.h"

// ==========================================
// EEPROM Memory Layout (Total Size: 256 bytes)
// ==========================================
#define EEPROM_SIZE               256
#define EEPROM_MAGIC_BYTE         0xB3 // Incremented to migrate safely
#define EEPROM_MAGIC_ADDR         0    // 1 byte (Magic marker)
#define EEPROM_MAC_ADDR           1    // 6 bytes (MAC address binary)
#define EEPROM_IP_ADDR            8    // 16 bytes (Target IP string)
#define EEPROM_TZ_ADDR            24   // 1 byte (Signed int8 timezone offset)
#define EEPROM_PROBE_INT_ADDR     26   // 2 bytes (Server probe interval in seconds)
#define EEPROM_CF_INT_ADDR        28   // 2 bytes (CF sync interval in seconds)
#define EEPROM_SSID_ADDR          32   // 32 bytes (WiFi SSID string)
#define EEPROM_PASS_ADDR          64   // 64 bytes (WiFi Password string)
#define EEPROM_CF_HOST_ADDR       128  // 64 bytes (Cloudflare Host string)
#define EEPROM_AUTH_TOKEN_ADDR    192  // 64 bytes (Secret Auth Token string)

// Active Configuration State in Memory
byte activeMac[6] = {0};
char activeMacStr[18] = "";
char activeServerIp[16] = "";
int8_t activeTzOffsetHours = DEFAULT_TIMEZONE_OFFSET_HOURS;
uint16_t activeServerCheckIntervalSec = DEFAULT_SERVER_CHECK_INTERVAL_SEC;
uint16_t activeCfSyncIntervalSec = DEFAULT_CF_SYNC_INTERVAL_SEC;
char activeWifiSsid[33] = "";
char activeWifiPass[65] = "";
char activeCfHost[65] = "";
char activeAuthToken[65] = "";

// Convert 6-byte MAC array to formatted string "XX:XX:XX:XX:XX:XX"
inline void macBytesToString(const byte* macBytes, char* outStr) {
  sprintf(outStr, "%02x:%02x:%02x:%02x:%02x:%02x",
          macBytes[0], macBytes[1], macBytes[2],
          macBytes[3], macBytes[4], macBytes[5]);
}

// Parse MAC address string (e.g. "04:42:1a:23:3b:8f") into 6 bytes without sscanf
inline bool parseMacAddress(const char* macStr, byte* macBytes) {
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

// Helper to read string from EEPROM safely
inline void readStringFromEEPROM(int startAddr, char* outBuf, int maxLen) {
  for (int i = 0; i < maxLen - 1; i++) {
    char c = (char)EEPROM.read(startAddr + i);
    if (c == 0xFF || c == '\0') {
      outBuf[i] = '\0';
      return;
    }
    outBuf[i] = c;
  }
  outBuf[maxLen - 1] = '\0';
}

// Helper to write string to EEPROM safely (only writes changed bytes to prevent flash wear)
inline bool writeStringToEEPROM(int startAddr, const char* inStr, int maxLen) {
  bool changed = false;
  int i = 0;
  for (; i < maxLen - 1 && inStr[i] != '\0'; i++) {
    if (EEPROM.read(startAddr + i) != (byte)inStr[i]) {
      EEPROM.write(startAddr + i, (byte)inStr[i]);
      changed = true;
    }
  }
  if (EEPROM.read(startAddr + i) != 0) {
    EEPROM.write(startAddr + i, 0);
    changed = true;
  }
  return changed;
}

// Save Target MAC and IP to EEPROM
inline bool saveTargetConfigToEEPROM(const byte* newMac, const char* newIp) {
  bool changed = false;
  for (int i = 0; i < 6; i++) {
    if (EEPROM.read(EEPROM_MAC_ADDR + i) != newMac[i]) {
      EEPROM.write(EEPROM_MAC_ADDR + i, newMac[i]);
      changed = true;
    }
  }
  if (writeStringToEEPROM(EEPROM_IP_ADDR, newIp, sizeof(activeServerIp))) {
    changed = true;
  }

  if (changed) {
    bool ok = EEPROM.commit();
    if (ok) {
      memcpy(activeMac, newMac, 6);
      macBytesToString(activeMac, activeMacStr);
      strncpy(activeServerIp, newIp, sizeof(activeServerIp) - 1);
      activeServerIp[sizeof(activeServerIp) - 1] = '\0';
      Serial.printf("[STORAGE] Target saved: MAC=%s, IP=%s\n", activeMacStr, activeServerIp);
      return true;
    }
    Serial.println(F("[STORAGE] ERROR: EEPROM commit failed for target config!"));
    return false;
  }
  return true;
}

// Save Timezone offset to EEPROM
inline bool saveTzToEEPROM(int8_t tz) {
  if (tz < -12 || tz > 14) tz = DEFAULT_TIMEZONE_OFFSET_HOURS;
  if ((int8_t)EEPROM.read(EEPROM_TZ_ADDR) != tz) {
    EEPROM.write(EEPROM_TZ_ADDR, (byte)tz);
    if (EEPROM.commit()) {
      activeTzOffsetHours = tz;
      Serial.printf("[STORAGE] Timezone saved: UTC%+d\n", activeTzOffsetHours);
      return true;
    }
    return false;
  }
  return true;
}

// Save Polling Rates to EEPROM
inline bool saveRatesToEEPROM(uint16_t probeSec, uint16_t cfSec) {
  if (probeSec < 1) probeSec = 1;
  if (cfSec < 5) cfSec = 5;

  bool changed = false;
  byte pLow = probeSec & 0xFF;
  byte pHigh = (probeSec >> 8) & 0xFF;
  byte cLow = cfSec & 0xFF;
  byte cHigh = (cfSec >> 8) & 0xFF;

  if (EEPROM.read(EEPROM_PROBE_INT_ADDR) != pLow)     { EEPROM.write(EEPROM_PROBE_INT_ADDR, pLow); changed = true; }
  if (EEPROM.read(EEPROM_PROBE_INT_ADDR + 1) != pHigh){ EEPROM.write(EEPROM_PROBE_INT_ADDR + 1, pHigh); changed = true; }
  if (EEPROM.read(EEPROM_CF_INT_ADDR) != cLow)        { EEPROM.write(EEPROM_CF_INT_ADDR, cLow); changed = true; }
  if (EEPROM.read(EEPROM_CF_INT_ADDR + 1) != cHigh)   { EEPROM.write(EEPROM_CF_INT_ADDR + 1, cHigh); changed = true; }

  if (changed) {
    if (EEPROM.commit()) {
      activeServerCheckIntervalSec = probeSec;
      activeCfSyncIntervalSec = cfSec;
      Serial.printf("[STORAGE] Polling rates saved: Probe=%us, CF Sync=%us\n", probeSec, cfSec);
      return true;
    }
    return false;
  }
  return true;
}

// Save Wi-Fi Credentials to EEPROM
inline bool saveWifiToEEPROM(const char* ssid, const char* pass) {
  bool changed = false;
  if (writeStringToEEPROM(EEPROM_SSID_ADDR, ssid, sizeof(activeWifiSsid))) changed = true;
  if (writeStringToEEPROM(EEPROM_PASS_ADDR, pass, sizeof(activeWifiPass))) changed = true;

  if (changed) {
    if (EEPROM.commit()) {
      strncpy(activeWifiSsid, ssid, sizeof(activeWifiSsid) - 1);
      strncpy(activeWifiPass, pass, sizeof(activeWifiPass) - 1);
      Serial.printf("[STORAGE] Wi-Fi credentials saved: SSID=%s\n", activeWifiSsid);
      return true;
    }
    return false;
  }
  return true;
}

// Save Cloudflare configuration to EEPROM
inline bool saveCfConfigToEEPROM(const char* host, const char* token) {
  bool changed = false;
  if (writeStringToEEPROM(EEPROM_CF_HOST_ADDR, host, sizeof(activeCfHost))) changed = true;
  if (writeStringToEEPROM(EEPROM_AUTH_TOKEN_ADDR, token, sizeof(activeAuthToken))) changed = true;

  if (changed) {
    if (EEPROM.commit()) {
      strncpy(activeCfHost, host, sizeof(activeCfHost) - 1);
      strncpy(activeAuthToken, token, sizeof(activeAuthToken) - 1);
      Serial.printf("[STORAGE] Cloudflare config saved: Host=%s\n", activeCfHost);
      return true;
    }
    return false;
  }
  return true;
}

// Save Full Setup Configuration (from first-time setup portal)
inline bool saveAllSetupConfig(const char* ssid, const char* pass, const char* host, const char* token,
                               const char* macStr, const char* ip, int8_t tz, uint16_t probeSec, uint16_t cfSec) {
  byte parsedMac[6];
  if (!parseMacAddress(macStr, parsedMac)) {
    Serial.println(F("[STORAGE] Failed to parse MAC address during setup save!"));
    return false;
  }

  // Ensure magic byte is written
  EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_BYTE);

  saveWifiToEEPROM(ssid, pass);
  saveCfConfigToEEPROM(host, token);
  saveTargetConfigToEEPROM(parsedMac, ip);
  saveTzToEEPROM(tz);
  saveRatesToEEPROM(probeSec, cfSec);

  return EEPROM.commit();
}

// Reset/Clear EEPROM configuration to force setup portal on next boot
inline void clearEEPROMConfig() {
  EEPROM.write(EEPROM_MAGIC_ADDR, 0x00);
  EEPROM.commit();
  Serial.println(F("[STORAGE] EEPROM magic byte cleared. Device will enter Setup AP mode on restart."));
}

// Load entire configuration from EEPROM (or populate with fallback defaults from config.h)
inline void loadConfigFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  byte magic = EEPROM.read(EEPROM_MAGIC_ADDR);

  if (magic == EEPROM_MAGIC_BYTE) {
    Serial.println(F("[STORAGE] Valid EEPROM magic marker found. Loading saved configuration..."));

    // 1. Target MAC
    for (int i = 0; i < 6; i++) {
      activeMac[i] = EEPROM.read(EEPROM_MAC_ADDR + i);
    }
    macBytesToString(activeMac, activeMacStr);

    // 2. Target IP
    readStringFromEEPROM(EEPROM_IP_ADDR, activeServerIp, sizeof(activeServerIp));
    if (strlen(activeServerIp) < 7) {
      strncpy(activeServerIp, SERVER_IP, sizeof(activeServerIp) - 1);
    }

    // 3. Timezone
    activeTzOffsetHours = (int8_t)EEPROM.read(EEPROM_TZ_ADDR);
    if (activeTzOffsetHours < -12 || activeTzOffsetHours > 14) {
      activeTzOffsetHours = DEFAULT_TIMEZONE_OFFSET_HOURS;
    }

    // 4. Polling Rates
    uint16_t pLow = EEPROM.read(EEPROM_PROBE_INT_ADDR);
    uint16_t pHigh = EEPROM.read(EEPROM_PROBE_INT_ADDR + 1);
    activeServerCheckIntervalSec = (pHigh << 8) | pLow;
    if (activeServerCheckIntervalSec < 1 || activeServerCheckIntervalSec > 3600) {
      activeServerCheckIntervalSec = DEFAULT_SERVER_CHECK_INTERVAL_SEC;
    }

    uint16_t cLow = EEPROM.read(EEPROM_CF_INT_ADDR);
    uint16_t cHigh = EEPROM.read(EEPROM_CF_INT_ADDR + 1);
    activeCfSyncIntervalSec = (cHigh << 8) | cLow;
    if (activeCfSyncIntervalSec < 5 || activeCfSyncIntervalSec > 86400) {
      activeCfSyncIntervalSec = DEFAULT_CF_SYNC_INTERVAL_SEC;
    }

    // 5. WiFi SSID & Password
    readStringFromEEPROM(EEPROM_SSID_ADDR, activeWifiSsid, sizeof(activeWifiSsid));
    readStringFromEEPROM(EEPROM_PASS_ADDR, activeWifiPass, sizeof(activeWifiPass));
    if (strlen(activeWifiSsid) == 0) {
      strncpy(activeWifiSsid, WIFI_SSID, sizeof(activeWifiSsid) - 1);
      strncpy(activeWifiPass, WIFI_PASSWORD, sizeof(activeWifiPass) - 1);
    }

    // 6. Cloudflare Host & Auth Token
    readStringFromEEPROM(EEPROM_CF_HOST_ADDR, activeCfHost, sizeof(activeCfHost));
    readStringFromEEPROM(EEPROM_AUTH_TOKEN_ADDR, activeAuthToken, sizeof(activeAuthToken));
    if (strlen(activeCfHost) == 0) {
      strncpy(activeCfHost, CF_HOST, sizeof(activeCfHost) - 1);
      strncpy(activeAuthToken, AUTH_TOKEN, sizeof(activeAuthToken) - 1);
    }

  } else {
    Serial.println(F("[STORAGE] EEPROM uninitialized or new layout detected. Populating with config.h fallbacks..."));

    // Write magic byte
    EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_BYTE);

    // Populate fallbacks
    if (parseMacAddress(SERVER_MAC, activeMac)) {
      for (int i = 0; i < 6; i++) EEPROM.write(EEPROM_MAC_ADDR + i, activeMac[i]);
      macBytesToString(activeMac, activeMacStr);
    }
    strncpy(activeServerIp, SERVER_IP, sizeof(activeServerIp) - 1);
    writeStringToEEPROM(EEPROM_IP_ADDR, activeServerIp, sizeof(activeServerIp));

    activeTzOffsetHours = DEFAULT_TIMEZONE_OFFSET_HOURS;
    EEPROM.write(EEPROM_TZ_ADDR, (byte)activeTzOffsetHours);

    activeServerCheckIntervalSec = DEFAULT_SERVER_CHECK_INTERVAL_SEC;
    EEPROM.write(EEPROM_PROBE_INT_ADDR, activeServerCheckIntervalSec & 0xFF);
    EEPROM.write(EEPROM_PROBE_INT_ADDR + 1, (activeServerCheckIntervalSec >> 8) & 0xFF);

    activeCfSyncIntervalSec = DEFAULT_CF_SYNC_INTERVAL_SEC;
    EEPROM.write(EEPROM_CF_INT_ADDR, activeCfSyncIntervalSec & 0xFF);
    EEPROM.write(EEPROM_CF_INT_ADDR + 1, (activeCfSyncIntervalSec >> 8) & 0xFF);

    strncpy(activeWifiSsid, WIFI_SSID, sizeof(activeWifiSsid) - 1);
    strncpy(activeWifiPass, WIFI_PASSWORD, sizeof(activeWifiPass) - 1);
    writeStringToEEPROM(EEPROM_SSID_ADDR, activeWifiSsid, sizeof(activeWifiSsid));
    writeStringToEEPROM(EEPROM_PASS_ADDR, activeWifiPass, sizeof(activeWifiPass));

    strncpy(activeCfHost, CF_HOST, sizeof(activeCfHost) - 1);
    strncpy(activeAuthToken, AUTH_TOKEN, sizeof(activeAuthToken) - 1);
    writeStringToEEPROM(EEPROM_CF_HOST_ADDR, activeCfHost, sizeof(activeCfHost));
    writeStringToEEPROM(EEPROM_AUTH_TOKEN_ADDR, activeAuthToken, sizeof(activeAuthToken));

    EEPROM.commit();
  }

  Serial.printf("[STORAGE] Active Config: SSID='%s', Target MAC=%s, Target IP=%s, TZ=UTC%+d, Probe=%us, CF Sync=%us\n",
                activeWifiSsid, activeMacStr, activeServerIp, activeTzOffsetHours,
                activeServerCheckIntervalSec, activeCfSyncIntervalSec);
}
