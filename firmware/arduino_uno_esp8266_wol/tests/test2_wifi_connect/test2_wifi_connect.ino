/**
 * TEST 2: Wi-Fi Connection Check
 * 
 * Objective: Verify that the ESP8266 shield successfully associates with
 *            your 2.4 GHz home Wi-Fi network and obtains a local IP.
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
 * LED Indicators (Pin 13 Built-in LED on Uno):
 * - 5 SLOW BLINKS: Wi-Fi CONNECTED SUCCESSFULLY!
 * - CONTINUOUS SOLID ON: Wi-Fi connection FAILED (Check SSID/Password in config.h).
 */

#if __has_include("../../config.h")
  #include "../../config.h"
#elif __has_include("../config.h")
  #include "../config.h"
#elif __has_include("config.h")
  #include "config.h"
#else
  #error "Missing config.h! Please create config.h with WIFI_SSID and WIFI_PASSWORD."
#endif

#define LED_PIN 13

void blinkTimes(int n) {
  for (int i = 0; i < n; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(400);
    digitalWrite(LED_PIN, LOW);
    delay(400);
  }
}

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

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  delay(1500);

  Serial.begin(115200);
  if (!sendCmd("AT", 1000, "OK")) {
    Serial.begin(9600);
    sendCmd("AT", 1000, "OK");
  }

  // Set Station mode
  sendCmd("AT+CWMODE=1", 1000, "OK");

  // Attempt Wi-Fi join
  String join = "AT+CWJAP=\"" + String(WIFI_SSID) + "\",\"" + String(WIFI_PASSWORD) + "\"";
  if (sendCmd(join, 12000, "OK")) {
    // SUCCESS: Blink 5 times
    blinkTimes(5);
  } else {
    // FAILED: Keep LED solid ON
    digitalWrite(LED_PIN, HIGH);
  }
}

void loop() {
  // Empty
}
