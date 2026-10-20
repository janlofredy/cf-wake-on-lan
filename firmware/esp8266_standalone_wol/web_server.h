#pragma once

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266NetBIOS.h>
#include "config.h"
#include "storage.h"
#include "telemetry.h"
#include "time_service.h"
#include "wol.h"
#include "cf_client.h"
#include "network_portal.h"

ESP8266WebServer server(80);

// Manage Local Hostname Resolution (mDNS for Apple/Linux/Win10, NetBIOS for Windows)
inline void startNameServices() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  // 1. Ensure Station DHCP Option 12 hostname is registered with router
  WiFi.hostname(DEVICE_HOSTNAME);

  // 2. Multicast DNS (mDNS) responder (http://<DEVICE_HOSTNAME>.local)
  MDNS.close();
  if (MDNS.begin(DEVICE_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "path", "/");
    Serial.printf("[mDNS] Responder active at: http://%s.local\n", DEVICE_HOSTNAME);
  } else {
    Serial.println(F("[mDNS] Failed to start responder"));
  }

  // 3. NetBIOS (NBNS) responder (http://<DEVICE_HOSTNAME> on Windows)
  NBNS.end();
  if (NBNS.begin(DEVICE_HOSTNAME)) {
    Serial.printf("[NetBIOS] Responder active at: http://%s\n", DEVICE_HOSTNAME);
  } else {
    Serial.println(F("[NetBIOS] Failed to start responder"));
  }
}

// CSS styling for ultra-minimal, high-contrast, low-RAM plain table interface
static const char CSS_COMMON[] PROGMEM =
  "body{font-family:system-ui,-apple-system,sans-serif;background:#0d1117;color:#e6edf3;margin:0;padding:12px;font-size:13px}"
  ".wrap{max-width:540px;margin:0 auto}"
  "h2{margin:0 0 8px 0;font-size:16px;color:#58a6ff;display:flex;align-items:center;justify-content:space-between}"
  ".box{background:#161b22;border:1px solid #30363d;border-radius:6px;padding:12px;margin-bottom:12px}"
  "table{width:100%;border-collapse:collapse;margin-top:6px;font-size:12px}"
  "td,th{padding:5px 6px;border-bottom:1px solid #21262d;text-align:left}"
  "th{color:#8b949e;font-weight:600}"
  ".lbl{color:#8b949e;width:40%}"
  ".val{font-family:monospace;font-weight:600}"
  ".badge{display:inline-block;padding:2px 8px;border-radius:12px;font-weight:bold;font-size:11px}"
  ".bg-on{background:#238636;color:#fff}"
  ".bg-off{background:#da3633;color:#fff}"
  ".bg-unk{background:#d29922;color:#000}"
  "input,select{background:#0d1117;border:1px solid #30363d;color:#e6edf3;padding:6px 8px;border-radius:4px;width:100%;box-sizing:border-box;margin-top:2px;font-family:monospace;font-size:12px}"
  "button,.btn{background:#238636;color:#fff;border:none;padding:8px 12px;border-radius:4px;cursor:pointer;font-weight:bold;font-size:13px;display:inline-block;text-align:center;text-decoration:none;box-sizing:border-box}"
  "button:hover,.btn:hover{filter:brightness(1.1)}"
  ".btn-w100{width:100%}"
  ".btn-sec{background:#30363d;color:#c9d1d9}"
  ".btn-sec:hover{background:#3f4752}"
  ".btn-dan{background:#b62324;color:#fff}"
  "a{color:#58a6ff;text-decoration:none}"
  "a:hover{text-decoration:underline}"
  ".grid2{display:grid;grid-template-columns:1fr 1fr;gap:8px}"
  ".grid3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px}"
  ".mt{margin-top:8px}";

// Handler: Setup Portal Page (for first-time setup or fallback AP)
inline void handleSetupPage() {
  String html;
  html.reserve(3500);
  html += F("<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>ServerStarter Setup</title><style>");
  html += (__FlashStringHelper*)CSS_COMMON;
  html += F("</style></head><body><div class='wrap'><div class='box'><h2><span>⚡ ServerStarter Setup</span></h2>"
            "<p style='color:#8b949e;margin:0 0 10px 0'>Configure device Wi-Fi, Cloudflare credentials, target server, and polling intervals.</p>"
            "<form method='POST' action='/save-setup'>"
            "<label><b>Wi-Fi Network (SSID):</b></label>");

  // Wi-Fi network scanner
  int n = WiFi.scanNetworks();
  if (n > 0) {
    html += F("<select name='ssid' required><option value=''>-- Select Detected Wi-Fi Network --</option>");
    for (int i = 0; i < n; ++i) {
      String ssidName = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      String enc = (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? "" : " 🔒";
      html += "<option value='" + ssidName + "'";
      if (ssidName == activeWifiSsid) html += " selected";
      html += ">" + ssidName + " (" + String(rssi) + " dBm)" + enc + "</option>";
    }
    html += F("</select><p style='font-size:11px;color:#8b949e;margin:2px 0 6px'>Or type manual SSID:</p>"
              "<input type='text' name='manual_ssid' placeholder='Manual SSID if hidden'>");
  } else {
    html += F("<input type='text' name='ssid' value='");
    html += activeWifiSsid;
    html += F("' placeholder='Your Wi-Fi SSID' required maxlength='32'>");
  }

  html += F("<div class='mt'><label><b>Wi-Fi Password:</b></label>"
            "<input type='password' name='pass' value='");
  html += activeWifiPass;
  html += F("' placeholder='Wi-Fi Password' maxlength='64'></div>"
            "<hr style='border:0;border-top:1px solid #30363d;margin:12px 0'>"
            "<div><label><b>Cloudflare Worker Host (Host ONLY, no http://):</b></label>"
            "<input type='text' name='cf_host' value='");
  html += activeCfHost;
  html += F("' placeholder='cf-wake-on-lan.subdomain.workers.dev' required maxlength='64'></div>"
            "<div class='mt'><label><b>Secret Auth Token:</b></label>"
            "<input type='password' name='auth_token' value='");
  html += activeAuthToken;
  html += F("' placeholder='Secret AOD device token' required maxlength='64'></div>"
            "<hr style='border:0;border-top:1px solid #30363d;margin:12px 0'>"
            "<div class='grid2'>"
            "<div><label><b>Target MAC Address:</b></label>"
            "<input type='text' name='mac' value='");
  html += activeMacStr;
  html += F("' placeholder='00:11:22:33:44:55' pattern='^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$' required maxlength='17'></div>"
            "<div><label><b>Target LAN IP:</b></label>"
            "<input type='text' name='ip' value='");
  html += activeServerIp;
  html += F("' placeholder='192.168.1.100' pattern='^([0-9]{1,3}\\.){3}[0-9]{1,3}$' required maxlength='15'></div>"
            "</div>"
            "<hr style='border:0;border-top:1px solid #30363d;margin:12px 0'>"
            "<div class='grid3'>"
            "<div><label><b>Timezone (UTC):</b></label>"
            "<input type='number' name='tz' min='-12' max='14' value='");
  html += String(activeTzOffsetHours);
  html += F("'></div>"
            "<div><label><b>Probe Rate (sec):</b></label>"
            "<input type='number' name='probe_sec' min='1' max='3600' value='");
  html += String(activeServerCheckIntervalSec);
  html += F("'></div>"
            "<div><label><b>CF Sync Rate (sec):</b></label>"
            "<input type='number' name='cf_sec' min='5' max='86400' value='");
  html += String(activeCfSyncIntervalSec);
  html += F("'></div>"
            "</div>"
            "<div class='mt' style='margin-top:14px'><button type='submit' class='btn btn-w100'>Verify & Save Configuration</button></div>"
            "</form></div>"
            "<div style='text-align:center;font-size:10px;color:#6e7681;font-family:monospace;margin-top:10px'>"
            "ServerStarter Firmware: <b style='color:#8b949e'>" FIRMWARE_VERSION "</b>"
            "</div></div></body></html>");

  server.send(200, "text/html", html);
}

// Handler: Process setup save and verify connection in background
inline void handleSaveSetup() {
  String reqSsid = server.arg("ssid");
  String manualSsid = server.arg("manual_ssid");
  if (manualSsid.length() > 0) reqSsid = manualSsid;
  reqSsid.trim();

  String reqPass = server.arg("pass");
  reqPass.trim();

  String reqHost = server.arg("cf_host");
  reqHost.trim();
  reqHost.replace("http://", "");
  reqHost.replace("https://", "");
  if (reqHost.endsWith("/")) reqHost.remove(reqHost.length() - 1);

  String reqToken = server.arg("auth_token");
  reqToken.trim();

  String reqMac = server.arg("mac");
  reqMac.trim();

  String reqIp = server.arg("ip");
  reqIp.trim();

  int reqTz = server.arg("tz").toInt();
  int reqProbe = server.arg("probe_sec").toInt();
  int reqCf = server.arg("cf_sec").toInt();
  if (reqProbe < 1) reqProbe = 5;
  if (reqCf < 5) reqCf = 30;

  // Background verification of Wi-Fi connection
  Serial.printf("[SETUP] Testing connection to SSID: %s...\n", reqSsid.c_str());
  bool connected = testWifiConnection(reqSsid.c_str(), reqPass.c_str(), 15000);

  if (!connected) {
    String errHtml;
    errHtml.reserve(1000);
    errHtml += F("<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
                 "<title>Connection Failed</title><style>");
    errHtml += (__FlashStringHelper*)CSS_COMMON;
    errHtml += F("</style></head><body><div class='wrap'><div class='box'><h2 style='color:#f85149'>❌ Connection Failed</h2>"
                 "<p>Could not connect to Wi-Fi network <b>");
    errHtml += reqSsid;
    errHtml += F("</b>. The Setup Access Point is still active.</p>"
                 "<p style='color:#8b949e'>Please verify your password and signal strength, then try again.</p>"
                 "<p class='mt'><a href='/setup' class='btn btn-sec btn-w100'>Back to Setup Form</a></p>"
                 "</div></div></body></html>");
    server.send(200, "text/html", errHtml);
    return;
  }

  // Connection succeeded! Persist to EEPROM
  saveAllSetupConfig(reqSsid.c_str(), reqPass.c_str(), reqHost.c_str(), reqToken.c_str(),
                     reqMac.c_str(), reqIp.c_str(), (int8_t)reqTz, (uint16_t)reqProbe, (uint16_t)reqCf);

  // Send success response
  String succHtml;
  succHtml.reserve(1500);
  succHtml += F("<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Setup Complete</title><style>");
  succHtml += (__FlashStringHelper*)CSS_COMMON;
  succHtml += F("</style></head><body><div class='wrap'><div class='box'><h2 style='color:#3fb950'>✅ Connected & Saved!</h2>"
                "<p>Successfully connected to <b>");
  succHtml += reqSsid;
  succHtml += F("</b>!</p>"
                "<table>"
                "<tr><td class='lbl'>Device IP:</td><td class='val'>");
  succHtml += WiFi.localIP().toString();
  succHtml += F("</td></tr>"
                "<tr><td class='lbl'>Apple / Linux URL:</td><td class='val'><a href='http://" DEVICE_HOSTNAME ".local'>http://" DEVICE_HOSTNAME ".local</a></td></tr>"
                "<tr><td class='lbl'>Windows URL:</td><td class='val'><a href='http://" DEVICE_HOSTNAME "'>http://" DEVICE_HOSTNAME "</a></td></tr>"
                "<tr><td class='lbl'>Target MAC:</td><td class='val'>");
  succHtml += activeMacStr;
  succHtml += F("</td></tr>"
                "<tr><td class='lbl'>Target IP:</td><td class='val'>");
  succHtml += activeServerIp;
  succHtml += F("</td></tr>"
                "</table>"
                "<p class='mt' style='color:#8b949e'>The Setup AP is now shutting down. You can access the dashboard on your local network:</p>"
                "<p class='mt'><a href='http://" DEVICE_HOSTNAME ".local/' class='btn btn-w100'>Open Local Dashboard (http://" DEVICE_HOSTNAME ".local)</a></p>"
                "<p class='mt'><a href='http://");
  succHtml += WiFi.localIP().toString();
  succHtml += F("/' class='btn btn-sec btn-w100'>Open via Direct IP (http://");
  succHtml += WiFi.localIP().toString();
  succHtml += F(")</a></p>"
                "</div></div></body></html>");
  server.send(200, "text/html", succHtml);

  // Allow HTTP response to flush before stopping AP
  delay(500);
  stopSetupAP();

  // Initialize Name Services (mDNS & NetBIOS) on newly connected Wi-Fi
  startNameServices();

  // Initialize NTP with saved timezone
  initTimeService(activeTzOffsetHours);

  // Probe target server and sync with Cloudflare immediately
  bool isOnline = probeServer();
  currentServerState = isOnline ? STATE_ONLINE : STATE_OFFLINE;
  lastSentServerState = currentServerState;
  updateServerStatusOnCloudflare(currentServerState);
}

// Handler: Root Dashboard (ultra-minimal plain tables)
inline void handleRoot() {
  // If AP mode is active and user accesses root, redirect to setup
  if (isApModeActive) {
    handleSetupPage();
    return;
  }

  char curTimeBuf[20];
  getFormattedDateTime(curTimeBuf, sizeof(curTimeBuf));

  char upBuf[20];
  getFormattedUptime(upBuf, sizeof(upBuf));

  String html;
  html.reserve(8500);
  html += F("<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>ServerStarter Dashboard</title><style>");
  html += (__FlashStringHelper*)CSS_COMMON;
  html += F("</style></head><body><div class='wrap'>"
            "<div class='box'><h2><span>⚡ ServerStarter</span>");

  if (currentServerState == STATE_ONLINE) {
    html += F("<span class='badge bg-on'>SERVER ONLINE</span>");
  } else if (currentServerState == STATE_OFFLINE) {
    html += F("<span class='badge bg-off'>SERVER OFFLINE</span>");
  } else {
    html += F("<span class='badge bg-unk'>UNKNOWN</span>");
  }

  html += F("</h2>");
  html += "<div style='color:#8b949e;margin:2px 0 8px 0'>Active Target: <b style='color:#58a6ff'>" + String(activeServerName) + "</b> &nbsp;(<span class='val'>" + String(activeMacStr) + "</span> &bull; <span class='val'>" + String(activeServerIp) + ":" + String(activeServerPort) + "</span>)</div>";
  html += "<div class='grid2 mt'>"
          "<form method='POST' action='/trigger'><button type='submit' class='btn btn-w100'>🚀 Wake " + String(activeServerName) + " (WOL)</button></form>"
          "<form method='POST' action='/reset-setup'><button type='submit' class='btn btn-sec btn-w100' onclick=\"return confirm('Enter Setup AP mode?')\">⚙️ Setup AP Mode</button></form>"
          "</div></div>";

  // PC Profiles Management
  html += "<div class='box'><h2><span>🖥️ PC Profiles (" + String(pcProfileCount) + "/" + String(MAX_PC_PROFILES) + ")</span></h2>"
          "<table><thead><tr><th>Name</th><th>MAC Address</th><th>IP:Port</th><th>Action</th></tr></thead><tbody>";
  for (uint8_t i = 0; i < pcProfileCount; i++) {
    char pMac[18];
    macBytesToString(pcProfiles[i].mac, pMac);
    html += "<tr><td class='val'>" + String(pcProfiles[i].name) + "</td>";
    html += "<td class='val'>" + String(pMac) + "</td>";
    html += "<td class='val'>" + String(pcProfiles[i].ip) + ":" + String(pcProfiles[i].port) + "</td>";
    html += "<td>";
    if (i == activeProfileIdx) {
      html += F("<span class='badge bg-on'>ACTIVE</span>");
    } else {
      html += "<form method='POST' action='/select-profile' style='display:inline'><input type='hidden' name='idx' value='" + String(i) + "'>"
              "<button type='submit' class='btn' style='padding:2px 8px;font-size:11px'>Activate</button></form>";
    }
    if (pcProfileCount > 1) {
      html += "<form method='POST' action='/delete-profile' style='display:inline;margin-left:4px'><input type='hidden' name='idx' value='" + String(i) + "'>"
              "<button type='submit' class='btn btn-dan' style='padding:2px 8px;font-size:11px' onclick=\"return confirm('Delete this PC profile?')\">✕</button></form>";
    }
    html += "</td></tr>";
  }
  html += F("</tbody></table>");

  if (pcProfileCount < MAX_PC_PROFILES) {
    html += F("<form method='POST' action='/add-profile' style='margin-top:10px;border-top:1px solid #21262d;padding-top:8px'>"
              "<div style='font-size:11px;font-weight:600;color:#8b949e;margin-bottom:4px'>+ Add PC Profile:</div>"
              "<div class='grid3'>"
              "<input type='text' name='name' placeholder='Name' required maxlength='15'>"
              "<input type='text' name='mac' placeholder='MAC (AA:BB:CC:...)' pattern='^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$' required maxlength='17'>"
              "<input type='text' name='ip' placeholder='LAN IP (192.168.1.X)' pattern='^([0-9]{1,3}\\.){3}[0-9]{1,3}$' required maxlength='15'>"
              "</div>"
              "<div class='mt'><button type='submit' class='btn btn-sec btn-w100'>+ Add PC Profile</button></div>"
              "</form>");
  }
  html += F("</div>");

  // System Telemetry
  html += F("<div class='box'><h2><span>📊 Device Telemetry</span></h2><table>"
            "<tr><td class='lbl'>Current Time:</td><td class='val'>");
  html += curTimeBuf;
  html += F("</td></tr><tr><td class='lbl'>Device Boot Time:</td><td class='val'>");
  html += bootTimeStr;
  html += F("</td></tr><tr><td class='lbl'>Reset Reason:</td><td class='val'>");
  html += resetReason;
  html += F("</td></tr><tr><td class='lbl'>Uptime:</td><td class='val'>");
  html += upBuf;
  html += F("</td></tr><tr><td class='lbl'>Free Heap / Min:</td><td class='val'>");
  html += String(ESP.getFreeHeap());
  html += F(" / ");
  html += String(minFreeHeap);
  html += F(" bytes</td></tr><tr><td class='lbl'>Wi-Fi Signal:</td><td class='val'>");
  html += String(WiFi.RSSI());
  html += F(" dBm (");
  html += String(getWifiQuality());
  html += F("%)</td></tr><tr><td class='lbl'>Wi-Fi Reconnects:</td><td class='val'>");
  html += String(wifiReconnectCount);
  html += F("</td></tr><tr><td class='lbl'>Device IP:</td><td class='val'>");
  html += WiFi.localIP().toString();
  html += F("</td></tr><tr><td class='lbl'>Local Hostname:</td><td class='val'>" DEVICE_HOSTNAME ".local</td></tr>"
            "<tr><td class='lbl'>LAN Access URLs:</td><td class='val' style='font-size:11px'>"
            "&bull; Apple / Linux: <a href='http://" DEVICE_HOSTNAME ".local'>http://" DEVICE_HOSTNAME ".local</a><br>"
            "&bull; Windows: <a href='http://" DEVICE_HOSTNAME "'>http://" DEVICE_HOSTNAME "</a> or <a href='http://" DEVICE_HOSTNAME ".local'>.local</a><br>"
            "&bull; Router DNS: <a href='http://" DEVICE_HOSTNAME ".lan'>http://" DEVICE_HOSTNAME ".lan</a>"
            "</td></tr></table></div>");

  // LAN Server Probe Telemetry
  html += F("<div class='box'><h2><span>🎯 LAN Server Probe</span></h2><table>"
            "<tr><td class='lbl'>Target Name:</td><td class='val'>");
  html += activeServerName;
  html += F("</td></tr><tr><td class='lbl'>Target Address:</td><td class='val'>");
  html += activeServerIp;
  html += F(" (Port ");
  html += String(activeServerPort);
  html += F(")</td></tr><tr><td class='lbl'>Target MAC:</td><td class='val'>");
  html += activeMacStr;
  html += F("</td></tr><tr><td class='lbl'>Last Probe Latency:</td><td class='val'>");
  html += String(lastProbeLatencyMs);
  html += F(" ms</td></tr><tr><td class='lbl'>Probe Success Rate:</td><td class='val'>");
  html += String(getProbeSuccessRate(), 1);
  html += F("% (");
  html += String(successProbes);
  html += F("/");
  html += String(totalProbes);
  html += F(")</td></tr><tr><td class='lbl'>Last Successful Ping:</td><td class='val'>");
  html += lastSuccessProbeStr;
  html += F("</td></tr><tr><td class='lbl'>Last Failed Ping:</td><td class='val'>");
  html += lastFailProbeStr;
  html += F("</td></tr><tr><td class='lbl'>Consecutive Fails:</td><td class='val'>");
  html += String(consecutiveFailProbes);
  html += F("</td></tr></table></div>");

  // Cloudflare Integration Details
  html += F("<div class='box'><h2><span>☁️ Cloudflare Integration</span></h2><table>"
            "<tr><td class='lbl'>Worker Host:</td><td class='val'>");
  html += activeCfHost;
  html += F("</td></tr><tr><td class='lbl'>Last Sync Result:</td><td class='val'>");
  html += String(lastCfHttpCode);
  html += F(" (");
  html += String(lastCfLatencyMs);
  html += F(" ms)</td></tr><tr><td class='lbl'>Sync Success Rate:</td><td class='val'>");
  html += String(getCfSyncSuccessRate(), 1);
  html += F("% (");
  html += String(successCfSyncs);
  html += F("/");
  html += String(totalCfSyncs);
  html += F(")</td></tr><tr><td class='lbl'>Last Successful Sync:</td><td class='val'>");
  html += lastSuccessCfSyncStr;
  html += F("</td></tr><tr><td class='lbl'>Last Failed Sync:</td><td class='val'>");
  html += lastFailCfSyncStr;
  html += F("</td></tr><tr><td class='lbl'>WOL Dispatches:</td><td class='val'>");
  html += String(totalWolDispatches);
  html += F(" (Last: ");
  html += lastWolDispatchStr;
  html += F(")</td></tr></table></div>");

  // Rolling 10-Request Activity Log
  html += F("<div class='box'><h2><span>📜 Recent Requests (Last 10)</span></h2>"
            "<table><thead><tr><th>Time</th><th>Action</th><th>Status</th><th>Latency</th></tr></thead><tbody>");
  if (historyCount == 0) {
    html += F("<tr><td colspan='4' style='color:#8b949e;text-align:center'>No activity recorded yet</td></tr>");
  } else {
    for (int i = 0; i < historyCount; i++) {
      int idx = (historyHead - 1 - i + MAX_REQUEST_LOG) % MAX_REQUEST_LOG;
      RequestLogEntry* e = &requestHistory[idx];
      html += "<tr><td class='val'>" + String(e->timeStr) + "</td>";
      html += "<td class='val'>" + String(e->action) + "</td>";
      html += "<td class='val'>";
      if (e->status == 200 || e->status == 1) {
        html += "<span style='color:#3fb950'>" + String(e->status) + "</span>";
      } else if (e->status == 0) {
        html += "<span style='color:#f85149'>OFFLINE</span>";
      } else {
        html += "<span style='color:#f85149'>" + String(e->status) + "</span>";
      }
      html += "</td><td class='val'>" + String(e->latencyMs) + "ms</td></tr>";
    }
  }
  html += F("</tbody></table></div>");

  // Configuration Form
  html += F("<div class='box'><h2><span>⚙️ Quick Configuration</span></h2>"
            "<form method='POST' action='/update-config'>"
            "<div class='grid2'>"
            "<div><label>Target MAC:</label><input type='text' name='mac' value='");
  html += activeMacStr;
  html += F("' maxlength='17' required pattern='^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$'></div>"
            "<div><label>Target IP:</label><input type='text' name='ip' value='");
  html += activeServerIp;
  html += F("' maxlength='15' required pattern='^([0-9]{1,3}\\.){3}[0-9]{1,3}$'></div>"
            "</div>"
            "<div class='grid3 mt'>"
            "<div><label>Timezone (UTC):</label><input type='number' name='tz' min='-12' max='14' value='");
  html += String(activeTzOffsetHours);
  html += F("'></div>"
            "<div><label>Probe (sec):</label><input type='number' name='probe_sec' min='1' max='3600' value='");
  html += String(activeServerCheckIntervalSec);
  html += F("'></div>"
            "<div><label>CF Sync (sec):</label><input type='number' name='cf_sec' min='5' max='86400' value='");
  html += String(activeCfSyncIntervalSec);
  html += F("'></div>"
            "</div>"
            "<div class='mt'><button type='submit' class='btn btn-sec btn-w100'>Save Configuration Changes</button></div>"
            "</form></div>"
            "<div style='text-align:center;margin:12px 0 4px;font-size:11px;color:#8b949e'>"
            "<a href='/'>Refresh</a> • <a href='/status' target='_blank'>JSON API</a> • <a href='/setup'>Full Setup Portal</a>"
            "</div>"
            "<div style='text-align:center;font-size:10px;color:#6e7681;font-family:monospace;margin-bottom:8px'>"
            "Firmware: <b style='color:#8b949e'>" FIRMWARE_VERSION "</b> &nbsp;|&nbsp; Cloudflare Worker: <b style='color:#8b949e'>");
  html += cfWorkerVersion;
  html += F("</b></div></div></body></html>");

  server.send(200, "text/html", html);
}

// Handler: Manual WOL trigger from dashboard
inline void handleManualTrigger() {
  Serial.println(F("[HTTP] Manual WOL trigger requested via web UI."));
  sendWOLPacket(activeMac);
  delay(200);
  sendWOLPacket(activeMac);

  server.sendHeader("Location", "/");
  server.send(303);
}

// Handler: Switch Active PC Profile
inline void handleSelectProfile() {
  if (server.hasArg("idx")) {
    uint8_t idx = server.arg("idx").toInt();
    if (setActiveProfile(idx)) {
      bool isOnline = probeServer();
      currentServerState = isOnline ? STATE_ONLINE : STATE_OFFLINE;
      lastSentServerState = currentServerState;
      updateServerStatusOnCloudflare(currentServerState);
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

// Handler: Add a New PC Profile
inline void handleAddProfile() {
  if (server.hasArg("name") && server.hasArg("mac") && server.hasArg("ip")) {
    String name = server.arg("name");
    name.trim();
    String macStr = server.arg("mac");
    macStr.trim();
    String ipStr = server.arg("ip");
    ipStr.trim();
    uint16_t port = 22;
    if (server.hasArg("port")) {
      int p = server.arg("port").toInt();
      if (p > 0 && p <= 65535) port = (uint16_t)p;
    }

    byte parsedMac[6];
    if (name.length() > 0 && parseMacAddress(macStr.c_str(), parsedMac)) {
      addProfile(name.c_str(), parsedMac, ipStr.c_str(), port);
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

// Handler: Delete a PC Profile
inline void handleDeleteProfile() {
  if (server.hasArg("idx")) {
    uint8_t idx = server.arg("idx").toInt();
    if (deleteProfile(idx)) {
      bool isOnline = probeServer();
      currentServerState = isOnline ? STATE_ONLINE : STATE_OFFLINE;
      lastSentServerState = currentServerState;
      updateServerStatusOnCloudflare(currentServerState);
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

// Handler: Update configuration from root dashboard form
inline void handleUpdateConfig() {
  bool updated = false;

  if (server.hasArg("mac")) {
    String mStr = server.arg("mac");
    mStr.trim();
    byte parsedMac[6];
    if (parseMacAddress(mStr.c_str(), parsedMac)) {
      saveTargetConfigToEEPROM(parsedMac, activeServerIp);
      updated = true;
    }
  }

  if (server.hasArg("ip")) {
    String ipStr = server.arg("ip");
    ipStr.trim();
    IPAddress testIp;
    if (testIp.fromString(ipStr)) {
      saveTargetConfigToEEPROM(activeMac, ipStr.c_str());
      updated = true;
    }
  }

  if (server.hasArg("tz")) {
    int tzVal = server.arg("tz").toInt();
    if (tzVal != activeTzOffsetHours) {
      saveTzToEEPROM((int8_t)tzVal);
      updateTimezone((int8_t)tzVal);
      updated = true;
    }
  }

  if (server.hasArg("probe_sec") && server.hasArg("cf_sec")) {
    int pSec = server.arg("probe_sec").toInt();
    int cSec = server.arg("cf_sec").toInt();
    if (pSec != activeServerCheckIntervalSec || cSec != activeCfSyncIntervalSec) {
      saveRatesToEEPROM((uint16_t)pSec, (uint16_t)cSec);
      updated = true;
    }
  }

  if (updated) {
    // Re-probe target immediately so state reflects updated target IP
    bool isOnline = probeServer();
    currentServerState = isOnline ? STATE_ONLINE : STATE_OFFLINE;
    lastSentServerState = currentServerState;
    updateServerStatusOnCloudflare(currentServerState);
  }

  server.sendHeader("Location", "/");
  server.send(303);
}

// Handler: Switch device into Setup AP mode on-demand
inline void handleResetSetup() {
  Serial.println(F("[HTTP] Reset to Setup AP mode requested."));
  startSetupAP();
  server.sendHeader("Location", "/setup");
  server.send(303);
}

// Handler: Comprehensive JSON status endpoint (/status)
inline void handleStatusJson() {
  char curTimeBuf[20];
  getFormattedDateTime(curTimeBuf, sizeof(curTimeBuf));

  char upBuf[20];
  getFormattedUptime(upBuf, sizeof(upBuf));

  String json;
  json.reserve(2500);
  json += "{";
  json += "\"datetime\":\"" + String(curTimeBuf) + "\",";
  json += "\"ntp_synced\":" + String(isNtpSynced() ? "true" : "false") + ",";
  json += "\"boot_time\":\"" + String(bootTimeStr) + "\",";
  json += "\"uptime\":\"" + String(upBuf) + "\",";
  json += "\"uptime_seconds\":" + String(millis() / 1000) + ",";
  json += "\"reset_reason\":\"" + String(resetReason) + "\",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"min_free_heap\":" + String(minFreeHeap) + ",";

  // WiFi
  json += "\"wifi\":{";
  json += "\"ssid\":\"" + String(activeWifiSsid) + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"hostname\":\"" DEVICE_HOSTNAME "\",";
  json += "\"mdns\":\"" DEVICE_HOSTNAME ".local\",";
  json += "\"gateway\":\"" + WiFi.gatewayIP().toString() + "\",";
  json += "\"subnet\":\"" + WiFi.subnetMask().toString() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"quality\":" + String(getWifiQuality()) + ",";
  json += "\"reconnects\":" + String(wifiReconnectCount);
  json += "},";

  // Target Server
  json += "\"server\":{";
  json += "\"name\":\"" + String(activeServerName) + "\",";
  json += "\"status\":\"" + String(currentServerState == STATE_ONLINE ? "ONLINE" : (currentServerState == STATE_OFFLINE ? "OFFLINE" : "UNKNOWN")) + "\",";
  json += "\"ip\":\"" + String(activeServerIp) + "\",";
  json += "\"mac\":\"" + String(activeMacStr) + "\",";
  json += "\"port\":" + String(activeServerPort) + ",";
  json += "\"last_probe_ms\":" + String(lastProbeLatencyMs) + ",";
  json += "\"success_rate\":" + String(getProbeSuccessRate(), 2) + ",";
  json += "\"last_success\":\"" + String(lastSuccessProbeStr) + "\",";
  json += "\"last_fail\":\"" + String(lastFailProbeStr) + "\",";
  json += "\"total_probes\":" + String(totalProbes) + ",";
  json += "\"consecutive_fails\":" + String(consecutiveFailProbes);
  json += "},";

  // PC Profiles
  json += "\"profiles\":{";
  json += "\"count\":" + String(pcProfileCount) + ",";
  json += "\"active_index\":" + String(activeProfileIdx) + ",";
  json += "\"active_name\":\"" + String(activeServerName) + "\",";
  json += "\"list\":[";
  for (uint8_t i = 0; i < pcProfileCount; i++) {
    char pMac[18];
    macBytesToString(pcProfiles[i].mac, pMac);
    if (i > 0) json += ",";
    json += "{\"index\":" + String(i) + ",";
    json += "\"name\":\"" + String(pcProfiles[i].name) + "\",";
    json += "\"mac\":\"" + String(pMac) + "\",";
    json += "\"ip\":\"" + String(pcProfiles[i].ip) + "\",";
    json += "\"port\":" + String(pcProfiles[i].port) + ",";
    json += "\"is_active\":" + String(i == activeProfileIdx ? "true" : "false") + "}";
  }
  json += "]},";

  // Cloudflare
  json += "\"cloudflare\":{";
  json += "\"host\":\"" + String(activeCfHost) + "\",";
  json += "\"last_http_code\":" + String(lastCfHttpCode) + ",";
  json += "\"last_latency_ms\":" + String(lastCfLatencyMs) + ",";
  json += "\"success_rate\":" + String(getCfSyncSuccessRate(), 2) + ",";
  json += "\"last_success\":\"" + String(lastSuccessCfSyncStr) + "\",";
  json += "\"last_fail\":\"" + String(lastFailCfSyncStr) + "\",";
  json += "\"total_syncs\":" + String(totalCfSyncs) + ",";
  json += "\"success_syncs\":" + String(successCfSyncs) + ",";
  json += "\"failed_syncs\":" + String(failedCfSyncs) + ",";
  json += "\"total_wol\":" + String(totalWolDispatches) + ",";
  json += "\"last_wol\":\"" + String(lastWolDispatchStr) + "\"";
  json += "},";

  // Intervals
  json += "\"intervals\":{";
  json += "\"server_probe_sec\":" + String(activeServerCheckIntervalSec) + ",";
  json += "\"cf_sync_sec\":" + String(activeCfSyncIntervalSec) + ",";
  json += "\"timezone_utc\":" + String(activeTzOffsetHours);
  json += "},";

  // History Log
  json += "\"history\":[";
  for (int i = 0; i < historyCount; i++) {
    int idx = (historyHead - 1 - i + MAX_REQUEST_LOG) % MAX_REQUEST_LOG;
    RequestLogEntry* e = &requestHistory[idx];
    if (i > 0) json += ",";
    json += "{\"time\":\"" + String(e->timeStr) + "\",";
    json += "\"action\":\"" + String(e->action) + "\",";
    json += "\"status\":" + String(e->status) + ",";
    json += "\"latency_ms\":" + String(e->latencyMs) + "}";
  }
  json += "],";
  json += "\"firmware_version\":\"" + String(FIRMWARE_VERSION) + "\",";
  json += "\"worker_version\":\"" + String(cfWorkerVersion) + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

// Handler: Captive portal probes from iOS, Android, and Windows
inline void handleCaptivePortalRedirect() {
  server.sendHeader("Location", "http://192.168.4.1/setup", true);
  server.send(302, "text/plain", "");
}

// Initialize all web server endpoints and mDNS responder
inline void initWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/setup", HTTP_GET, handleSetupPage);
  server.on("/save-setup", HTTP_POST, handleSaveSetup);
  server.on("/trigger", HTTP_POST, handleManualTrigger);
  server.on("/select-profile", HTTP_POST, handleSelectProfile);
  server.on("/add-profile", HTTP_POST, handleAddProfile);
  server.on("/delete-profile", HTTP_POST, handleDeleteProfile);
  server.on("/update-config", HTTP_POST, handleUpdateConfig);
  server.on("/update-mac", HTTP_POST, handleUpdateConfig); // Backwards compatibility
  server.on("/reset-setup", HTTP_POST, handleResetSetup);
  server.on("/status", HTTP_GET, handleStatusJson);

  // Captive Portal probe redirects
  server.on("/generate_204", handleCaptivePortalRedirect);
  server.on("/hotspot-detect.html", handleCaptivePortalRedirect);
  server.on("/connecttest.txt", handleCaptivePortalRedirect);
  server.on("/ncsi.txt", handleCaptivePortalRedirect);

  // Catch-all for captive portal domains
  server.onNotFound([]() {
    if (isApModeActive) {
      handleCaptivePortalRedirect();
    } else {
      server.send(404, "text/plain", "Not Found");
    }
  });

  server.begin();
  Serial.println(F("[Web] HTTP server listening on port 80"));

  // Start mDNS and NetBIOS responders if Wi-Fi station is connected
  if (WiFi.status() == WL_CONNECTED) {
    startNameServices();
  }
}
