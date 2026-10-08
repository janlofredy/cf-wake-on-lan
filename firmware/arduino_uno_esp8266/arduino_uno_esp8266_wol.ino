#include <SoftwareSerial.h>

/**
 * Arduino Uno R3 + ESP8266 ESP-12E WiFi Shield
 * 
 * Hardware Setup:
 * - Arduino Uno with ESP8266 ESP-12E WiFi Shield mounted on top.
 * - Set shield DIP switches: 
 *   For Uno software serial communication: 
 *   SW 1, 2, 3, 4: Set to Arduino RX/TX pins (e.g. Uno Pins 2 & 3 or 8 & 9).
 *   Example with SoftwareSerial on Pin 2 (RX) and Pin 3 (TX):
 *     Uno Pin 2 -> Shield TX
 *     Uno Pin 3 -> Shield RX
 *
 * Flow:
 * - Local server polling loop: checks if server is reachable on LAN at SERVER_CHECK_INTERVAL_MS.
 * - Tracks 'currentServerStatus' vs 'lastSentServerStatus'.
 * - Cloudflare sync loop: runs at CF_SYNC_INTERVAL_MS.
 *     1. If status changed, updates status on Cloudflare Worker.
 *     2. When status is OFF (or during sync), checks Cloudflare for pending WOL trigger.
 *     3. Dispatches WOL UDP packet if triggered, then sends acknowledgment.
 */

// SoftwareSerial pins connected to ESP8266 shield
SoftwareSerial espSerial(2, 3); // RX, TX

// WiFi & Cloudflare Configuration
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* CF_HOST       = "YOUR_WORKER_SUBDOMAIN.workers.dev";
const char* AUTH_TOKEN    = "CHANGE_ME_SECRET_TOKEN";

// Server Configuration
const char* SERVER_MAC    = "AA:BB:CC:DD:EE:FF";
const char* SERVER_IP     = "192.168.1.100";
const int   SERVER_PORT   = 22;

// Timers (Two separate polling intervals)
const unsigned long SERVER_CHECK_INTERVAL = 5000;   // Check server every 5 seconds locally
const unsigned long CF_SYNC_INTERVAL      = 20000;  // Sync with Cloudflare every 20 seconds

unsigned long lastServerCheckTime = 0;
unsigned long lastCfSyncTime      = 0;

// State Memory
enum ServerState { STATE_UNKNOWN, STATE_ONLINE, STATE_OFFLINE };
ServerState currentServerState   = STATE_UNKNOWN;
ServerState lastSentServerState  = STATE_UNKNOWN;

bool sendCommand(String cmd, unsigned long timeout, String expectedResponse) {
  espSerial.println(cmd);
  long int time = millis();
  String response = "";
  
  while ((time + timeout) > millis()) {
    while (espSerial.available()) {
      char c = espSerial.read();
      response += c;
    }
    if (response.indexOf(expectedResponse) != -1) {
      return true;
    }
  }
  return false;
}

// Check local LAN reachability of the target server
bool probeServer() {
  String pingCmd = "AT+CIPSTART=\"TCP\",\"" + String(SERVER_IP) + "\"," + String(SERVER_PORT);
  if (sendCommand(pingCmd, 2000, "CONNECT") || sendCommand(pingCmd, 1000, "OK")) {
    sendCommand("AT+CIPCLOSE", 1000, "OK");
    return true;
  }
  sendCommand("AT+CIPCLOSE", 500, "OK");
  return false;
}

// Send Wake-on-LAN UDP magic packet (102 bytes)
void sendWOLPacket() {
  Serial.println(F("[WOL] Transmitting Wake-on-LAN UDP Packet..."));
  if (sendCommand("AT+CIPSTART=\"UDP\",\"255.255.255.255\",9", 3000, "OK")) {
    byte packet[102];
    memset(packet, 0xFF, 6);
    
    int mac[6];
    sscanf(SERVER_MAC, "%x:%x:%x:%x:%x:%x", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
    for (int i = 0; i < 16; i++) {
      for (int m = 0; m < 6; m++) {
        packet[6 + (i * 6) + m] = (byte)mac[m];
      }
    }

    String sendCmd = "AT+CIPSEND=102";
    if (sendCommand(sendCmd, 2000, ">")) {
      for (int i = 0; i < 102; i++) {
        espSerial.write(packet[i]);
      }
      Serial.println(F("[WOL] Magic packet broadcast complete!"));
    }
    sendCommand("AT+CIPCLOSE", 1000, "OK");
  }
}

// Acknowledge trigger completion to Cloudflare
void acknowledgeTrigger() {
  String startSSL = "AT+CIPSTART=\"SSL\",\"" + String(CF_HOST) + "\",443";
  if (sendCommand(startSSL, 5000, "OK")) {
    String req = "POST /ack HTTP/1.1\r\n";
    req += "Host: " + String(CF_HOST) + "\r\n";
    req += "Authorization: Bearer " + String(AUTH_TOKEN) + "\r\n";
    req += "Content-Type: application/json\r\n";
    req += "Content-Length: 17\r\n";
    req += "Connection: close\r\n\r\n";
    req += "{\"result\":\"SENT\"}";

    String sendCmd = "AT+CIPSEND=" + String(req.length());
    if (sendCommand(sendCmd, 3000, ">")) {
      espSerial.print(req);
    }
    delay(500);
    sendCommand("AT+CIPCLOSE", 1000, "OK");
  }
}

// Update Server status on Cloudflare
void updateServerStatusOnCloudflare(ServerState state) {
  String startSSL = "AT+CIPSTART=\"SSL\",\"" + String(CF_HOST) + "\",443";
  if (sendCommand(startSSL, 5000, "OK")) {
    String statusStr = (state == STATE_ONLINE) ? "ONLINE" : "OFFLINE";
    String body = "{\"status\":\"" + statusStr + "\"}";

    String req = "POST /server/ping HTTP/1.1\r\n";
    req += "Host: " + String(CF_HOST) + "\r\n";
    req += "Authorization: Bearer " + String(AUTH_TOKEN) + "\r\n";
    req += "Content-Type: application/json\r\n";
    req += "Content-Length: " + String(body.length()) + "\r\n";
    req += "Connection: close\r\n\r\n";
    req += body;

    String sendCmd = "AT+CIPSEND=" + String(req.length());
    if (sendCommand(sendCmd, 3000, ">")) {
      espSerial.print(req);
      Serial.println(F("[Cloudflare] Status updated to: ") + statusStr);
    }
    delay(500);
    sendCommand("AT+CIPCLOSE", 1000, "OK");
  }
}

// Check Cloudflare for pending WOL trigger (when server is OFF)
void checkWOLTrigger() {
  String startSSL = "AT+CIPSTART=\"SSL\",\"" + String(CF_HOST) + "\",443";
  if (!sendCommand(startSSL, 5000, "OK")) {
    return;
  }

  String req = "GET /status HTTP/1.1\r\n";
  req += "Host: " + String(CF_HOST) + "\r\n";
  req += "Authorization: Bearer " + String(AUTH_TOKEN) + "\r\n";
  req += "Connection: close\r\n\r\n";

  String sendCmd = "AT+CIPSEND=" + String(req.length());
  if (sendCommand(sendCmd, 3000, ">")) {
    espSerial.print(req);
    
    long int timeout = millis() + 5000;
    String res = "";
    while (millis() < timeout) {
      while (espSerial.available()) {
        res += (char)espSerial.read();
      }
      if (res.indexOf("\"trigger\":true") != -1) {
        Serial.println(F("[TRIGGER] Pending Wake-on-LAN trigger detected!"));
        sendWOLPacket();
        delay(200);
        sendWOLPacket(); // Send twice for reliability
        acknowledgeTrigger();
        break;
      }
    }
  }

  sendCommand("AT+CIPCLOSE", 1000, "OK");
}

void setup() {
  Serial.begin(9600);
  espSerial.begin(9600);

  Serial.println(F("\n--- Arduino Uno + ESP-12E Shield Dual-Interval WOL ---"));
  
  if (!sendCommand("AT", 2000, "OK")) {
    espSerial.begin(115200);
    sendCommand("AT", 2000, "OK");
  }

  sendCommand("AT+CWMODE=1", 2000, "OK");
  
  String joinCmd = "AT+CWJAP=\"" + String(WIFI_SSID) + "\",\"" + String(WIFI_PASSWORD) + "\"";
  Serial.println(F("[WiFi] Connecting..."));
  sendCommand(joinCmd, 10000, "OK");
}

void loop() {
  unsigned long now = millis();

  // --- INTERVAL 1: Poll Local Server Status ---
  if (now - lastServerCheckTime >= SERVER_CHECK_INTERVAL || lastServerCheckTime == 0) {
    lastServerCheckTime = now;
    bool isAlive = probeServer();
    currentServerState = isAlive ? STATE_ONLINE : STATE_OFFLINE;
    Serial.print(F("[Server Probe] Status: "));
    Serial.println(isAlive ? F("ONLINE") : F("OFFLINE"));
  }

  // --- INTERVAL 2: Cloudflare Synchronization ---
  if (now - lastCfSyncTime >= CF_SYNC_INTERVAL || lastCfSyncTime == 0) {
    lastCfSyncTime = now;

    // 1. Update Cloudflare whenever status changes, or periodically (every ~60s) to keep AOD health alive
    static unsigned long lastSentTime = 0;
    bool statusChanged = (currentServerState != lastSentServerState && currentServerState != STATE_UNKNOWN);
    bool periodicRefresh = (now - lastSentTime >= 60000 || lastSentTime == 0);

    if (statusChanged || periodicRefresh) {
      Serial.println(F("[AOD Health/Status] Pinging Cloudflare with server status..."));
      updateServerStatusOnCloudflare(currentServerState);
      lastSentServerState = currentServerState;
      lastSentTime = now;
    }

    // 2. At the same time, when status is OFF (or on initial boot), check for triggers
    if (currentServerState == STATE_OFFLINE || currentServerState == STATE_UNKNOWN) {
      Serial.println(F("[Server is OFF] Checking Cloudflare for WOL triggers..."));
      checkWOLTrigger();
    }
  }
}
