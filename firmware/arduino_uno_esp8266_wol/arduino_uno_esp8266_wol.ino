/**
 * Arduino Uno R3 + WangTongze ESP8266 ESP-12E WiFi Shield
 * Direct Hardware Serial via Pins D0 & D1 (No Jumper Cables Required!)
 *
 * DIP SWITCH GUIDE:
 * - WHEN UPLOADING CODE VIA USB:
 *     Switch 1: OFF
 *     Switch 2: OFF
 *     Switch 3: OFF (NOR mode)
 *     Switch 4: OFF
 *
 * - WHEN RUNNING (AOD or ANY TEST MODE):
 *     Switch 1: ON  (P1 TTLSW: routes ESP TX to Uno D0)
 *     Switch 2: ON  (P2 TTLSW: routes ESP RX to Uno D1)
 *     Switch 3: OFF (P3 DFU/NOR: MUST be OFF for normal operation!)
 *     Switch 4: OFF
 */

#if __has_include("config.h")
  #include "config.h"
#else
  #error "Missing config.h! Please copy config.h.example to config.h and configure your settings."
#endif

#define LED_PIN 13
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

// Check local LAN reachability of target server
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
      blinkLed(5, 50); // 5 fast blinks indicate WOL broadcast
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
        sendWOLPacket();
        acknowledgeTrigger();
        break;
      }
    }
  }

  sendCommand("AT+CIPCLOSE", 1000, "OK");
}

void initESP() {
  espSerial.begin(115200);
  delay(1000);
  if (!sendCommand("AT", 1000, "OK")) {
    espSerial.begin(9600);
    delay(500);
    sendCommand("AT", 1000, "OK");
  }
}

// ==========================================
// TEST IMPLEMENTATIONS
// ==========================================
void runTestAT() {
  // Try 115200 baud
  espSerial.begin(115200);
  delay(500);
  while (espSerial.available()) espSerial.read();
  
  if (sendCommand("AT", 1500, "OK")) {
    while (true) blinkLed(1, 100); // Fast rapid blink = SUCCESS
  }

  // Fallback to 9600 baud
  espSerial.begin(9600);
  delay(500);
  while (espSerial.available()) espSerial.read();
  if (sendCommand("AT", 1500, "OK")) {
    while (true) blinkLed(1, 100); // Fast rapid blink = SUCCESS
  }

  // FAILED: Solid ON
  digitalWrite(LED_PIN, HIGH);
}

void runTestWiFi() {
  initESP();
  sendCommand("AT+CWMODE=1", 1000, "OK");
  String join = "AT+CWJAP=\"" + String(WIFI_SSID) + "\",\"" + String(WIFI_PASSWORD) + "\"";
  if (sendCommand(join, 12000, "OK")) {
    blinkLed(5, 400); // 5 slow blinks = SUCCESS!
  } else {
    digitalWrite(LED_PIN, HIGH); // Solid ON = FAILED
  }
}

void runTestWOL() {
  initESP();
  sendCommand("AT+CWMODE=1", 1000, "OK");
  String join = "AT+CWJAP=\"" + String(WIFI_SSID) + "\",\"" + String(WIFI_PASSWORD) + "\"";
  sendCommand(join, 10000, "OK");

  for (int i = 0; i < 3; i++) {
    sendWOLPacket();
    delay(500);
  }
  blinkLed(10, 150); // 10 pulses = Dispatched
}

void runTestCF() {
  initESP();
  sendCommand("AT+CWMODE=1", 1000, "OK");
  String join = "AT+CWJAP=\"" + String(WIFI_SSID) + "\",\"" + String(WIFI_PASSWORD) + "\"";
  sendCommand(join, 10000, "OK");

  // Ping Cloudflare
  updateServerStatusOnCloudflare(STATE_ONLINE);

  while (true) {
    blinkLed(3, 100);
    delay(800);
  }
}

// ==========================================
// ARDUINO SETUP & LOOP
// ==========================================
void setup() {
  pinMode(LED_PIN, OUTPUT);

  // INSTANT HEARTBEAT: 2 quick blinks right on power-up
  // If you see this, the Uno CPU is 100% running!
  for (int i = 0; i < 2; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(150);
    digitalWrite(LED_PIN, LOW);
    delay(150);
  }
  delay(1000);

#if RUN_MODE == MODE_TEST_AT
  runTestAT();
  return;
#elif RUN_MODE == MODE_TEST_WIFI
  runTestWiFi();
  return;
#elif RUN_MODE == MODE_TEST_WOL
  runTestWOL();
  return;
#elif RUN_MODE == MODE_TEST_CF
  runTestCF();
  return;
#endif

  // --- NORMAL OPERATION ---
  initESP();
  sendCommand("AT+CWMODE=1", 1000, "OK");
  String joinCmd = "AT+CWJAP=\"" + String(WIFI_SSID) + "\",\"" + String(WIFI_PASSWORD) + "\"";
  sendCommand(joinCmd, 8000, "OK");
  blinkLed(3, 200); // 3 blinks = Normal Boot Ready
}

void loop() {
#if RUN_MODE != MODE_NORMAL
  return; // Tests halt here
#endif

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

    static unsigned long lastSentTime = 0;
    bool statusChanged = (currentServerState != lastSentServerState && currentServerState != STATE_UNKNOWN);
    bool periodicRefresh = (now - lastSentTime >= 60000 || lastSentTime == 0);

    if (statusChanged || periodicRefresh) {
      updateServerStatusOnCloudflare(currentServerState);
      lastSentServerState = currentServerState;
      lastSentTime = now;
    }

    if (currentServerState == STATE_OFFLINE || currentServerState == STATE_UNKNOWN) {
      checkWOLTrigger();
    }
  }
}
