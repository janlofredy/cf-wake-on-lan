/**
 * TEST 4: Cloudflare Worker SSL & Ping Test
 * 
 * Objective: Verify that the ESP8266 shield can make an HTTPS SSL connection
 *            to your Cloudflare Worker and send a test /server/ping!
 * 
 * Instructions:
 * 1. Set DIP switches 1, 2, 3, 4 to OFF.
 * 2. Upload this sketch to Arduino Uno.
 * 3. Once uploaded, set switches:
 *      Switch 1: ON  (P1 TTLSW)
 *      Switch 2: ON  (P2 TTLSW)
 *      Switch 3: OFF (NOR mode)
 *      Switch 4: OFF
 * 4. Press RESET on Uno.
 * 
 * Visual Indicator (Pin 13 on Uno):
 * - FAST TRIPLE BLINK: SSL request to Cloudflare SUCCEEDED!
 *   (Check your Web Dashboard: AOD Device will show "CONNECTED"!)
 * - SOLID ON: Failed to connect via HTTPS / SSL to Cloudflare.
 */

#if __has_include("config.h")
  #include "config.h"
#else
  #error "Missing config.h! Please create config.h with WIFI_SSID, WIFI_PASSWORD, CF_HOST, and AUTH_TOKEN."
#endif

#define LED_PIN 13

bool sendCmd(String cmd, long timeout, String expected) {
  Serial.println(cmd);
  long t = millis() + timeout;
  String res = "";
  while (millis() < t) {
    while (Serial.available()) res += (char)Serial.read();
    if (res.indexOf(expected) != -1) return true;
  }
  return false;
}

bool pingCloudflare() {
  // Connect to Worker via SSL (Port 443)
  String startSSL = "AT+CIPSTART=\"SSL\",\"" + String(CF_HOST) + "\",443";
  if (!sendCmd(startSSL, 6000, "OK")) {
    return false;
  }

  String body = "{\"status\":\"ONLINE\"}";
  String req = "POST /server/ping HTTP/1.1\r\n";
  req += "Host: " + String(CF_HOST) + "\r\n";
  req += "Authorization: Bearer " + String(AUTH_TOKEN) + "\r\n";
  req += "Content-Type: application/json\r\n";
  req += "Content-Length: " + String(body.length()) + "\r\n";
  req += "Connection: close\r\n\r\n";
  req += body;

  String sendCmdStr = "AT+CIPSEND=" + String(req.length());
  bool success = false;
  if (sendCmd(sendCmdStr, 3000, ">")) {
    Serial.print(req);
    
    // Check for HTTP 200 response
    long timeout = millis() + 5000;
    String res = "";
    while (millis() < timeout) {
      while (Serial.available()) res += (char)Serial.read();
      if (res.indexOf("200 OK") != -1) {
        success = true;
        break;
      }
    }
  }

  sendCmd("AT+CIPCLOSE", 1000, "OK");
  return success;
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  delay(1500);

  Serial.begin(115200);
  if (!sendCmd("AT", 1000, "OK")) {
    Serial.begin(9600);
    sendCmd("AT", 1000, "OK");
  }

  sendCmd("AT+CWMODE=1", 1000, "OK");
  String join = "AT+CWJAP=\"" + String(WIFI_SSID) + "\",\"" + String(WIFI_PASSWORD) + "\"";
  sendCmd(join, 10000, "OK");

  delay(1000);

  if (pingCloudflare()) {
    // Triple pulse loop indicating Cloudflare reached!
    while (true) {
      for (int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(100);
        digitalWrite(LED_PIN, LOW);
        delay(100);
      }
      delay(800);
    }
  } else {
    // Solid on = SSL / DNS error
    digitalWrite(LED_PIN, HIGH);
  }
}

void loop() {
  // Empty
}
