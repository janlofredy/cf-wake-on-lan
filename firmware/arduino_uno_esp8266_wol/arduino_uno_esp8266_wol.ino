#include <SoftwareSerial.h>

/**
 * Arduino Uno R3 + WangTongze ESP8266 ESP-12E WiFi Shield
 * Connected via SoftwareSerial on Pins D2 & D3 (DuPont Jumpers)
 *
 * DIP SWITCHES:
 * Set ALL 4 switches to OFF permanently!
 *   Switch 1: OFF
 *   Switch 2: OFF
 *   Switch 3: OFF (NOR mode)
 *   Switch 4: OFF
 *
 * JUMPER CONNECTIONS:
 * - Shield "TXD" (or TX) pin -> Arduino Uno Pin D2 (Uno RX)
 * - Shield "RXD" (or RX) pin -> Arduino Uno Pin D3 (Uno TX)
 */

#if __has_include("config.h")
  #include "config.h"
#else
  #error "Missing config.h! Please copy config.h.example to config.h and configure your settings."
#endif

#define LED_PIN 13

// SoftwareSerial for ESP8266 communication on D2/D3
// Hardware Serial (Serial) remains 100% free for your USB Serial Monitor!
SoftwareSerial espSerial(2, 3); // RX = Pin 2, TX = Pin 3

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
  Serial.println(F("[WOL] Sending Wake-on-LAN packet..."));
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
      Serial.println(F("[WOL] Magic packet transmitted!"));
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
      Serial.println(F("[Cloudflare] Trigger disarmed (/ack)."));
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
      Serial.print(F("[Cloudflare] Status updated: "));
      Serial.println(statusStr);
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
        Serial.println(F("[TRIGGER] Wake-on-LAN trigger detected from Cloudflare!"));
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
  Serial.println(F("[ESP] Initializing communication on D2/D3..."));
  espSerial.begin(115200);
  delay(500);
  if (!sendCommand("AT", 1000, "OK")) {
    espSerial.begin(9600);
    delay(500);
    if (!sendCommand("AT", 1000, "OK")) {
      Serial.println(F("[ESP] AT communication failed! Check jumpers: TXD->D2, RXD->D3"));
      return;
    }
  }
  Serial.println(F("[ESP] AT communication READY!"));
}

// ==========================================
// TEST IMPLEMENTATIONS
// ==========================================
void runTestAT() {
  Serial.println(F("\n--- RUNNING TEST 1: AT Communication Check ---"));
  
  espSerial.begin(115200);
  delay(500);
  while (espSerial.available()) espSerial.read();
  
  if (sendCommand("AT", 1500, "OK")) {
    Serial.println(F(">>> SUCCESS! ESP8266 responding at 115200 baud! <<<"));
    while (true) blinkLed(1, 100);
  }

  espSerial.begin(9600);
  delay(500);
  while (espSerial.available()) espSerial.read();
  if (sendCommand("AT", 1500, "OK")) {
    Serial.println(F(">>> SUCCESS! ESP8266 responding at 9600 baud! <<<"));
    while (true) blinkLed(1, 100);
  }

  Serial.println(F(">>> FAILED! No response from ESP8266. Check wires: TXD->D2, RXD->D3 <<<"));
  digitalWrite(LED_PIN, HIGH);
}

void runTestWiFi() {
  Serial.println(F("\n--- RUNNING TEST 2: Wi-Fi Connection Check ---"));
  initESP();
  sendCommand("AT+CWMODE=1", 1000, "OK");
  Serial.print(F("[WiFi] Connecting to: "));
  Serial.println(WIFI_SSID);
  
  String join = "AT+CWJAP=\"" + String(WIFI_SSID) + "\",\"" + String(WIFI_PASSWORD) + "\"";
  if (sendCommand(join, 15000, "OK")) {
    Serial.println(F(">>> SUCCESS! Wi-Fi Connected! <<<"));
    blinkLed(5, 400);
  } else {
    Serial.println(F(">>> FAILED to connect to Wi-Fi! Check SSID/Password in config.h <<<"));
    digitalWrite(LED_PIN, HIGH);
  }
}

void runTestWOL() {
  Serial.println(F("\n--- RUNNING TEST 3: Standalone WOL Magic Packet Test ---"));
  initESP();
  sendCommand("AT+CWMODE=1", 1000, "OK");
  String join = "AT+CWJAP=\"" + String(WIFI_SSID) + "\",\"" + String(WIFI_PASSWORD) + "\"";
  sendCommand(join, 10000, "OK");

  Serial.print(F("[WOL] Broadcasting magic packets to: "));
  Serial.println(SERVER_MAC);

  for (int i = 0; i < 3; i++) {
    sendWOLPacket();
    delay(500);
  }
  Serial.println(F(">>> Check your PC! It should be turning on now! <<<"));
  blinkLed(10, 150);
}

void runTestCF() {
  Serial.println(F("\n--- RUNNING TEST 4: Cloudflare Ping Test ---"));
  initESP();
  sendCommand("AT+CWMODE=1", 1000, "OK");
  String join = "AT+CWJAP=\"" + String(WIFI_SSID) + "\",\"" + String(WIFI_PASSWORD) + "\"";
  sendCommand(join, 10000, "OK");

  Serial.print(F("[Cloudflare] Connecting to: "));
  Serial.println(CF_HOST);

  updateServerStatusOnCloudflare(STATE_ONLINE);
  Serial.println(F(">>> Check web dashboard! AOD Device should show CONNECTED! <<<"));

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
  Serial.begin(9600); // USB Serial Monitor at 9600 baud
  delay(1000);

  Serial.println(F("\n=========================================="));
  Serial.println(F("   cf-wake-on-lan Controller Starting...  "));
  Serial.println(F("=========================================="));

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
  Serial.println(F("[Ready] Always-On Device loop active."));
  blinkLed(3, 200);
}

void loop() {
#if RUN_MODE != MODE_NORMAL
  return; // Tests halt in setup
#endif

  unsigned long now = millis();

  // --- INTERVAL 1: Poll Local Server Status ---
  if (now - lastServerCheckTime >= SERVER_CHECK_INTERVAL_MS || lastServerCheckTime == 0) {
    lastServerCheckTime = now;
    bool isAlive = probeServer();
    currentServerState = isAlive ? STATE_ONLINE : STATE_OFFLINE;
    Serial.print(F("[Server LAN Probe] Status: "));
    Serial.println(isAlive ? F("ONLINE") : F("OFFLINE"));
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
