/**
 * Arduino Uno R3 + WangTongze ESP8266 ESP-12E WiFi Shield
 * NO JUMPER CABLES NEEDED! (Direct Hardware Serial via Pins D0 & D1)
 *
 * DIP SWITCH GUIDE:
 * - WHEN UPLOADING CODE VIA USB:
 *     Switch 1: OFF
 *     Switch 2: OFF
 *     Switch 3: OFF (NOR mode)
 *     Switch 4: OFF
 *
 * - WHEN RUNNING AS ALWAYS-ON DEVICE (AOD):
 *     Switch 1: ON  (P1 TTLSW: routes ESP TX to Uno D0)
 *     Switch 2: ON  (P2 TTLSW: routes ESP RX to Uno D1)
 *     Switch 3: OFF (P3 DFU/NOR: must be OFF for normal operation!)
 *     Switch 4: OFF
 */

#if __has_include("config.h")
  #include "config.h"
#else
  #error "Missing config.h! Please copy config.h.example to config.h and configure your settings."
#endif

// Built-in LED on Arduino Uno (Pin 13) for visual status
#define LED_PIN 13

// Use Uno's Hardware Serial (Pins 0 & 1)
#define espSerial Serial

// State Memory
enum ServerState { STATE_UNKNOWN, STATE_ONLINE, STATE_OFFLINE };
ServerState currentServerState   = STATE_UNKNOWN;
ServerState lastSentServerState  = STATE_UNKNOWN;

unsigned long lastServerCheckTime = 0;
unsigned long lastCfSyncTime      = 0;

void blinkLed(int times, int ms) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(ms);
    digitalWrite(LED_PIN, LOW);
    delay(ms);
  }
}

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
      // Rapid visual blink on Pin 13 indicates WOL packet sent!
      blinkLed(5, 50);
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
      // Solid pulse indicates status sync
      digitalWrite(LED_PIN, HIGH);
      delay(200);
      digitalWrite(LED_PIN, LOW);
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
        sendWOLPacket();
        delay(250);
        sendWOLPacket(); // Send twice for reliability
        acknowledgeTrigger();
        break;
      }
    }
  }

  sendCommand("AT+CIPCLOSE", 1000, "OK");
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Initialize hardware serial
  espSerial.begin(115200);
  delay(1000);

  // Test AT communication (WangTongze ESP-12E default baud is 115200)
  if (!sendCommand("AT", 1000, "OK")) {
    espSerial.begin(9600);
    delay(500);
    sendCommand("AT", 1000, "OK");
  }

  // Set Station mode
  sendCommand("AT+CWMODE=1", 1000, "OK");
  
  // Connect to 2.4 GHz Wi-Fi
  String joinCmd = "AT+CWJAP=\"" + String(WIFI_SSID) + "\",\"" + String(WIFI_PASSWORD) + "\"";
  sendCommand(joinCmd, 8000, "OK");

  // Indicate setup complete with 3 slow blinks on Uno Pin 13
  blinkLed(3, 200);
}

void loop() {
  unsigned long now = millis();

  // --- INTERVAL 1: Poll Local Server Status ---
  if (now - lastServerCheckTime >= SERVER_CHECK_INTERVAL_MS || lastServerCheckTime == 0) {
    lastServerCheckTime = now;
    bool isAlive = probeServer();
    currentServerState = isAlive ? STATE_ONLINE : STATE_OFFLINE;
  }

  // --- INTERVAL 2: Cloudflare Synchronization ---
  if (now - lastCfSyncTime >= CF_SYNC_INTERVAL_MS || lastCfSyncTime == 0) {
    lastCfSyncTime = now;

    // 1. Update Cloudflare whenever status changes, or periodically (every ~60s)
    static unsigned long lastSentTime = 0;
    bool statusChanged = (currentServerState != lastSentServerState && currentServerState != STATE_UNKNOWN);
    bool periodicRefresh = (now - lastSentTime >= 60000 || lastSentTime == 0);

    if (statusChanged || periodicRefresh) {
      updateServerStatusOnCloudflare(currentServerState);
      lastSentServerState = currentServerState;
      lastSentTime = now;
    }

    // 2. When server is OFF (or initial state), check for pending triggers
    if (currentServerState == STATE_OFFLINE || currentServerState == STATE_UNKNOWN) {
      checkWOLTrigger();
    }
  }
}
