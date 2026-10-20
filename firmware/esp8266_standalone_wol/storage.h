#pragma once

#include <Arduino.h>
#include <EEPROM.h>
#include "config.h"

// ==========================================
// EEPROM Memory Layout (Total Size: 512 bytes)
// ==========================================
#define EEPROM_SIZE                 512
#define EEPROM_MAGIC_BYTE           0xB5 // Incremented to migrate safely to PC Profiles
#define EEPROM_MAGIC_ADDR           0    // 1 byte (Magic marker)
#define EEPROM_PROFILE_COUNT_ADDR   1    // 1 byte (Number of saved PC profiles, 1..5)
#define EEPROM_ACTIVE_PROFILE_ADDR  2    // 1 byte (Index of currently active PC, 0..4)
#define EEPROM_PROBE_INT_ADDR       4    // 2 bytes (Server probe interval in seconds)
#define EEPROM_CF_INT_ADDR          6    // 2 bytes (CF sync interval in seconds)
#define EEPROM_TZ_ADDR              8    // 1 byte (Signed int8 timezone offset)

#define EEPROM_SSID_ADDR            16   // 32 bytes (WiFi SSID string)
#define EEPROM_PASS_ADDR            48   // 64 bytes (WiFi Password string)
#define EEPROM_CF_HOST_ADDR         112  // 64 bytes (Cloudflare Host string)
#define EEPROM_AUTH_TOKEN_ADDR      176  // 64 bytes (Secret Auth Token string)

#define EEPROM_PROFILES_START_ADDR  240  // Start of PC Profiles (5 * 40 bytes = 200 bytes)
#define PROFILE_RECORD_SIZE         40   // 16 name + 6 mac + 16 ip + 2 port = 40 bytes
#define MAX_PC_PROFILES             5

// ==========================================
// PC Profile Structure
// ==========================================
struct PcProfile {
  char name[16];   // e.g. "Proxmox", "Secondary PC"
  byte mac[6];     // 6 bytes binary MAC
  char ip[16];     // e.g. "192.168.1.100"
  uint16_t port;   // e.g. 22
};

// Profiles Collection in Memory
PcProfile pcProfiles[MAX_PC_PROFILES];
uint8_t pcProfileCount = 0;
uint8_t activeProfileIdx = 0;

// Active Configuration State in Memory
byte activeMac[6] = {0};
char activeMacStr[18] = "";
char activeServerIp[16] = "";
uint16_t activeServerPort = 22;
char activeServerName[16] = "";

int8_t activeTzOffsetHours = DEFAULT_TIMEZONE_OFFSET_HOURS;
uint16_t activeServerCheckIntervalSec = DEFAULT_SERVER_CHECK_INTERVAL_SEC;
uint16_t activeCfSyncIntervalSec = DEFAULT_CF_SYNC_INTERVAL_SEC;
char activeWifiSsid[33] = "";
char activeWifiPass[65] = "";
char activeCfHost[65] = "";
char activeAuthToken[65] = "";

// Convert 6-byte MAC array to formatted string "XX:XX:XX:XX:XX:XX"
void macBytesToString(const byte* macBytes, char* outStr) {
  sprintf(outStr, "%02x:%02x:%02x:%02x:%02x:%02x",
          macBytes[0], macBytes[1], macBytes[2],
          macBytes[3], macBytes[4], macBytes[5]);
}

// Parse MAC address string (e.g. "00:11:22:33:44:55" or "00-11-22-33-44-55") into 6 bytes without sscanf
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

// Helper to read string from EEPROM safely
void readStringFromEEPROM(int startAddr, char* outBuf, int maxLen) {
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
bool writeStringToEEPROM(int startAddr, const char* inStr, int maxLen) {
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

// Sync active target shortcuts with currently active PC profile
void syncActiveProfilePointers() {
  if (pcProfileCount == 0) return;
  if (activeProfileIdx >= pcProfileCount) activeProfileIdx = 0;

  PcProfile* active = &pcProfiles[activeProfileIdx];
  memcpy(activeMac, active->mac, 6);
  macBytesToString(activeMac, activeMacStr);
  strncpy(activeServerIp, active->ip, sizeof(activeServerIp) - 1);
  activeServerIp[sizeof(activeServerIp) - 1] = '\0';
  activeServerPort = active->port;
  strncpy(activeServerName, active->name, sizeof(activeServerName) - 1);
  activeServerName[sizeof(activeServerName) - 1] = '\0';
}

// Read PC Profile from EEPROM
void readProfileFromEEPROM(uint8_t idx, PcProfile& profile) {
  int base = EEPROM_PROFILES_START_ADDR + (idx * PROFILE_RECORD_SIZE);
  readStringFromEEPROM(base, profile.name, sizeof(profile.name));
  for (int i = 0; i < 6; i++) {
    profile.mac[i] = EEPROM.read(base + 16 + i);
  }
  readStringFromEEPROM(base + 22, profile.ip, sizeof(profile.ip));
  byte pLow = EEPROM.read(base + 38);
  byte pHigh = EEPROM.read(base + 39);
  profile.port = (pHigh << 8) | pLow;
  if (profile.port == 0 || profile.port == 0xFFFF) profile.port = 22;
}

// Write PC Profile to EEPROM
bool writeProfileToEEPROM(uint8_t idx, const PcProfile& profile) {
  bool changed = false;
  int base = EEPROM_PROFILES_START_ADDR + (idx * PROFILE_RECORD_SIZE);

  if (writeStringToEEPROM(base, profile.name, sizeof(profile.name))) changed = true;
  for (int i = 0; i < 6; i++) {
    if (EEPROM.read(base + 16 + i) != profile.mac[i]) {
      EEPROM.write(base + 16 + i, profile.mac[i]);
      changed = true;
    }
  }
  if (writeStringToEEPROM(base + 22, profile.ip, sizeof(profile.ip))) changed = true;
  byte pLow = profile.port & 0xFF;
  byte pHigh = (profile.port >> 8) & 0xFF;
  if (EEPROM.read(base + 38) != pLow)  { EEPROM.write(base + 38, pLow); changed = true; }
  if (EEPROM.read(base + 39) != pHigh) { EEPROM.write(base + 39, pHigh); changed = true; }

  return changed;
}

// Switch the Active PC Profile
bool setActiveProfile(uint8_t idx) {
  if (idx >= pcProfileCount) return false;
  activeProfileIdx = idx;
  EEPROM.write(EEPROM_ACTIVE_PROFILE_ADDR, activeProfileIdx);
  bool ok = EEPROM.commit();
  syncActiveProfilePointers();
  Serial.printf("[PROFILES] Switched active PC to [%u]: '%s' (%s, %s:%u)\n",
                activeProfileIdx, activeServerName, activeMacStr, activeServerIp, activeServerPort);
  return ok;
}

// Add a new PC Profile (up to 5 profiles max)
bool addProfile(const char* name, const byte* mac, const char* ip, uint16_t port) {
  if (pcProfileCount >= MAX_PC_PROFILES) {
    Serial.println(F("[PROFILES] Cannot add: Maximum of 5 PC profiles reached!"));
    return false;
  }

  PcProfile newProf;
  strncpy(newProf.name, name, sizeof(newProf.name) - 1);
  newProf.name[sizeof(newProf.name) - 1] = '\0';
  memcpy(newProf.mac, mac, 6);
  strncpy(newProf.ip, ip, sizeof(newProf.ip) - 1);
  newProf.ip[sizeof(newProf.ip) - 1] = '\0';
  newProf.port = (port > 0) ? port : 22;

  writeProfileToEEPROM(pcProfileCount, newProf);
  pcProfiles[pcProfileCount] = newProf;
  pcProfileCount++;

  EEPROM.write(EEPROM_PROFILE_COUNT_ADDR, pcProfileCount);
  EEPROM.commit();

  Serial.printf("[PROFILES] Added new profile [%u]: '%s' (%s:%u)\n",
                pcProfileCount - 1, newProf.name, newProf.ip, newProf.port);
  return true;
}

// Delete a PC Profile (cannot delete if only 1 profile remaining)
bool deleteProfile(uint8_t idx) {
  if (pcProfileCount <= 1 || idx >= pcProfileCount) {
    Serial.println(F("[PROFILES] Cannot delete profile (minimum 1 profile required)."));
    return false;
  }

  // Shift subsequent profiles
  for (uint8_t i = idx; i < pcProfileCount - 1; i++) {
    pcProfiles[i] = pcProfiles[i + 1];
    writeProfileToEEPROM(i, pcProfiles[i]);
  }

  pcProfileCount--;
  EEPROM.write(EEPROM_PROFILE_COUNT_ADDR, pcProfileCount);

  // Adjust active index
  if (activeProfileIdx == idx) {
    activeProfileIdx = 0;
  } else if (activeProfileIdx > idx) {
    activeProfileIdx--;
  }
  EEPROM.write(EEPROM_ACTIVE_PROFILE_ADDR, activeProfileIdx);
  EEPROM.commit();

  syncActiveProfilePointers();
  Serial.printf("[PROFILES] Deleted profile at index %u. Profiles remaining: %u, Active: [%u] '%s'\n",
                idx, pcProfileCount, activeProfileIdx, activeServerName);
  return true;
}

// Update Target MAC/IP of the Active PC Profile
bool saveTargetConfigToEEPROM(const byte* mac, const char* ip) {
  if (pcProfileCount == 0) return false;
  PcProfile* active = &pcProfiles[activeProfileIdx];
  memcpy(active->mac, mac, 6);
  strncpy(active->ip, ip, sizeof(active->ip) - 1);
  active->ip[sizeof(active->ip) - 1] = '\0';
  writeProfileToEEPROM(activeProfileIdx, *active);
  if (EEPROM.commit()) {
    syncActiveProfilePointers();
    Serial.printf("[STORAGE] Active PC [%u] '%s' updated: MAC=%s, IP=%s\n",
                  activeProfileIdx, activeServerName, activeMacStr, activeServerIp);
    return true;
  }
  return false;
}

// Save Timezone offset to EEPROM
bool saveTzToEEPROM(int8_t tz) {
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
bool saveRatesToEEPROM(uint16_t probeSec, uint16_t cfSec) {
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
bool saveWifiToEEPROM(const char* ssid, const char* pass) {
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
bool saveCfConfigToEEPROM(const char* host, const char* token) {
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
bool saveAllSetupConfig(const char* ssid, const char* pass, const char* host, const char* token,
                               const char* macStr, const char* ip, int8_t tz, uint16_t probeSec, uint16_t cfSec,
                               const char* pcName = "Default PC", uint16_t port = 22) {
  byte parsedMac[6];
  if (!parseMacAddress(macStr, parsedMac)) {
    Serial.println(F("[STORAGE] Failed to parse MAC address during setup save!"));
    return false;
  }

  // Ensure magic byte is written
  EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_BYTE);

  saveWifiToEEPROM(ssid, pass);
  saveCfConfigToEEPROM(host, token);
  saveTzToEEPROM(tz);
  saveRatesToEEPROM(probeSec, cfSec);

  // Initialize Profile 0 with the configured target
  PcProfile p0;
  strncpy(p0.name, pcName, sizeof(p0.name) - 1);
  p0.name[sizeof(p0.name) - 1] = '\0';
  memcpy(p0.mac, parsedMac, 6);
  strncpy(p0.ip, ip, sizeof(p0.ip) - 1);
  p0.ip[sizeof(p0.ip) - 1] = '\0';
  p0.port = (port > 0) ? port : 22;

  writeProfileToEEPROM(0, p0);
  pcProfiles[0] = p0;
  pcProfileCount = 1;
  activeProfileIdx = 0;

  EEPROM.write(EEPROM_PROFILE_COUNT_ADDR, pcProfileCount);
  EEPROM.write(EEPROM_ACTIVE_PROFILE_ADDR, activeProfileIdx);

  bool ok = EEPROM.commit();
  syncActiveProfilePointers();
  return ok;
}

// Reset/Clear EEPROM configuration to force setup portal on next boot
void clearEEPROMConfig() {
  EEPROM.write(EEPROM_MAGIC_ADDR, 0x00);
  EEPROM.commit();
  Serial.println(F("[STORAGE] EEPROM magic byte cleared. Device will enter Setup AP mode on restart."));
}

// Load entire configuration from EEPROM (or populate with fallback defaults from config.h)
void loadConfigFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  byte magic = EEPROM.read(EEPROM_MAGIC_ADDR);

  if (magic == EEPROM_MAGIC_BYTE) {
    Serial.println(F("[STORAGE] Valid EEPROM magic marker found. Loading saved configuration..."));

    // 1. Timezone
    activeTzOffsetHours = (int8_t)EEPROM.read(EEPROM_TZ_ADDR);
    if (activeTzOffsetHours < -12 || activeTzOffsetHours > 14) {
      activeTzOffsetHours = DEFAULT_TIMEZONE_OFFSET_HOURS;
    }

    // 2. Polling Rates
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

    // 3. WiFi SSID & Password
    readStringFromEEPROM(EEPROM_SSID_ADDR, activeWifiSsid, sizeof(activeWifiSsid));
    readStringFromEEPROM(EEPROM_PASS_ADDR, activeWifiPass, sizeof(activeWifiPass));
    if (strlen(activeWifiSsid) == 0) {
      strncpy(activeWifiSsid, WIFI_SSID, sizeof(activeWifiSsid) - 1);
      strncpy(activeWifiPass, WIFI_PASSWORD, sizeof(activeWifiPass) - 1);
    }

    // 4. Cloudflare Host & Auth Token
    readStringFromEEPROM(EEPROM_CF_HOST_ADDR, activeCfHost, sizeof(activeCfHost));
    readStringFromEEPROM(EEPROM_AUTH_TOKEN_ADDR, activeAuthToken, sizeof(activeAuthToken));
    if (strlen(activeCfHost) == 0) {
      strncpy(activeCfHost, CF_HOST, sizeof(activeCfHost) - 1);
      strncpy(activeAuthToken, AUTH_TOKEN, sizeof(activeAuthToken) - 1);
    }

    // 5. PC Profiles
    pcProfileCount = EEPROM.read(EEPROM_PROFILE_COUNT_ADDR);
    if (pcProfileCount < 1 || pcProfileCount > MAX_PC_PROFILES) {
      pcProfileCount = 1;
    }

    activeProfileIdx = EEPROM.read(EEPROM_ACTIVE_PROFILE_ADDR);
    if (activeProfileIdx >= pcProfileCount) {
      activeProfileIdx = 0;
    }

    for (uint8_t i = 0; i < pcProfileCount; i++) {
      readProfileFromEEPROM(i, pcProfiles[i]);
    }

    syncActiveProfilePointers();

  } else {
    Serial.println(F("[STORAGE] EEPROM uninitialized or upgraded. Initializing with default PC Profiles..."));

    EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_BYTE);

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

    // Default Pre-loaded Profiles:
    // Profile 0: Proxmox Server (from config.h or 192.168.1.100)
    PcProfile p0;
    strncpy(p0.name, "Proxmox", sizeof(p0.name) - 1);
    p0.name[sizeof(p0.name) - 1] = '\0';
    parseMacAddress(SERVER_MAC, p0.mac);
    strncpy(p0.ip, SERVER_IP, sizeof(p0.ip) - 1);
    p0.ip[sizeof(p0.ip) - 1] = '\0';
    p0.port = SERVER_PORT;
    writeProfileToEEPROM(0, p0);
    pcProfiles[0] = p0;

    // Profile 1: Secondary PC (192.168.1.100, 00:11:22:33:44:55)
    PcProfile p1;
    strncpy(p1.name, "Secondary PC", sizeof(p1.name) - 1);
    p1.name[sizeof(p1.name) - 1] = '\0';
    parseMacAddress("00:11:22:33:44:55", p1.mac);
    strncpy(p1.ip, "192.168.1.100", sizeof(p1.ip) - 1);
    p1.ip[sizeof(p1.ip) - 1] = '\0';
    p1.port = 22;
    writeProfileToEEPROM(1, p1);
    pcProfiles[1] = p1;

    pcProfileCount = 2;
    activeProfileIdx = 0;

    EEPROM.write(EEPROM_PROFILE_COUNT_ADDR, pcProfileCount);
    EEPROM.write(EEPROM_ACTIVE_PROFILE_ADDR, activeProfileIdx);

    EEPROM.commit();
    syncActiveProfilePointers();
  }

  Serial.printf("[STORAGE] Active PC: [%u] '%s' (MAC: %s, IP: %s:%u) | Profiles: %u/%u\n",
                activeProfileIdx, activeServerName, activeMacStr, activeServerIp, activeServerPort,
                pcProfileCount, MAX_PC_PROFILES);
}
