#include <SoftwareSerial.h>

/**
 * Arduino Uno + ESP8266 ESP-12E WiFi Shield
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
 * NOTE: Since Wake-on-LAN magic packets are UDP broadcast packets and Cloudflare uses HTTPS,
 * the ESP8266 handles the HTTPS request (using AT commands) and UDP broadcast.
 */

// SoftwareSerial pins connected to ESP8266 shield
SoftwareSerial espSerial(2, 3); // RX, TX

// Replace with your credentials and Worker host
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* CF_HOST       = "YOUR_WORKER_SUBDOMAIN.workers.dev";
const char* AUTH_TOKEN    = "CHANGE_ME_SECRET_TOKEN";

// Server MAC to wake
const char* SERVER_MAC = "AA:BB:CC:DD:EE:FF";

unsigned long lastPoll = 0;
const unsigned long POLL_INTERVAL = 30000; // 30 seconds

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

void setup() {
  Serial.begin(9600);
  espSerial.begin(9600); // Most ESP-12E shields default to 9600 or 115200

  Serial.println(F("--- Arduino Uno + ESP-12E Shield WOL ---"));
  
  // Test communication
  if (sendCommand("AT", 2000, "OK")) {
    Serial.println(F("[ESP] AT communication ready."));
  } else {
    Serial.println(F("[ESP] Trying 115200 baud..."));
    espSerial.begin(115200);
    sendCommand("AT", 2000, "OK");
  }

  // Set station mode
  sendCommand("AT+CWMODE=1", 2000, "OK");
  
  // Connect to WiFi
  String joinCmd = "AT+CWJAP=\"" + String(WIFI_SSID) + "\",\"" + String(WIFI_PASSWORD) + "\"";
  Serial.println(F("[WiFi] Connecting..."));
  sendCommand(joinCmd, 10000, "OK");
}

void pollStatus() {
  Serial.println(F("[Cloudflare] Checking status..."));

  // Connect to Cloudflare Worker via SSL (port 443)
  String startSSL = "AT+CIPSTART=\"SSL\",\"" + String(CF_HOST) + "\",443";
  if (!sendCommand(startSSL, 5000, "OK")) {
    Serial.println(F("[SSL] Connection failed"));
    return;
  }

  // Prepare HTTP GET request
  String req = "GET /status HTTP/1.1\r\n";
  req += "Host: " + String(CF_HOST) + "\r\n";
  req += "Authorization: Bearer " + String(AUTH_TOKEN) + "\r\n";
  req += "Connection: close\r\n\r\n";

  String sendCmd = "AT+CIPSEND=" + String(req.length());
  if (sendCommand(sendCmd, 3000, ">")) {
    espSerial.print(req);
    
    // Read response
    long int timeout = millis() + 5000;
    String res = "";
    while (millis() < timeout) {
      while (espSerial.available()) {
        res += (char)espSerial.read();
      }
      if (res.indexOf("\"trigger\":true") != -1) {
        Serial.println(F("[TRIGGER] Active Wake-on-LAN trigger detected!"));
        sendWOLPacket();
        acknowledgeTrigger();
        break;
      }
    }
  }

  sendCommand("AT+CIPCLOSE", 1000, "OK");
}

void acknowledgeTrigger() {
  String startSSL = "AT+CIPSTART=\"SSL\",\"" + String(CF_HOST) + "\",443";
  if (sendCommand(startSSL, 5000, "OK")) {
    String req = "POST /ack HTTP/1.1\r\n";
    req += "Host: " + String(CF_HOST) + "\r\n";
    req += "Authorization: Bearer " + String(AUTH_TOKEN) + "\r\n";
    req += "Content-Length: 0\r\n";
    req += "Connection: close\r\n\r\n";

    String sendCmd = "AT+CIPSEND=" + String(req.length());
    if (sendCommand(sendCmd, 3000, ">")) {
      espSerial.print(req);
    }
    delay(1000);
    sendCommand("AT+CIPCLOSE", 1000, "OK");
  }
}

void sendWOLPacket() {
  Serial.println(F("[WOL] Transmitting Wake-on-LAN UDP Packet..."));
  // Open UDP connection to broadcast address 255.255.255.255 on port 9
  if (sendCommand("AT+CIPSTART=\"UDP\",\"255.255.255.255\",9", 3000, "OK")) {
    // 102 byte magic packet: 6x 0xFF + 16x MAC
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

void loop() {
  if (millis() - lastPoll > POLL_INTERVAL || lastPoll == 0) {
    lastPoll = millis();
    pollStatus();
  }
}
