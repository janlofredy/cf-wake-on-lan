/**
 * TEST 3: Standalone Wake-on-LAN Magic Packet Test
 * 
 * Objective: Verify that the Arduino Uno & ESP8266 can physically turn on
 *            your PC over local Wi-Fi without needing Cloudflare!
 * 
 * Instructions:
 * 1. Set DIP switches 1, 2, 3, 4 to OFF.
 * 2. Upload this sketch to Arduino Uno.
 * 3. Once uploaded, set switches:
 *      Switch 1: ON  (P1 TTLSW)
 *      Switch 2: ON  (P2 TTLSW)
 *      Switch 3: OFF (NOR mode)
 *      Switch 4: OFF
 * 4. Put your target PC in Sleep / Shutdown state.
 * 5. Press RESET on Uno.
 * 
 * Behavior:
 * - Automatically connects to Wi-Fi.
 * - Broadcasts 3 Wake-on-LAN UDP magic packets to your PC's MAC address!
 * - LED on Pin 13 pulses 10 times to indicate WOL packets dispatched.
 */

#if __has_include("config.h")
  #include "config.h"
#else
  #error "Missing config.h! Please create config.h with WIFI_SSID, WIFI_PASSWORD, and SERVER_MAC."
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

void sendMagicPacket() {
  if (sendCmd("AT+CIPSTART=\"UDP\",\"255.255.255.255\",9", 3000, "OK")) {
    byte packet[102];
    memset(packet, 0xFF, 6);
    
    int mac[6];
    sscanf(SERVER_MAC, "%x:%x:%x:%x:%x:%x", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
    for (int i = 0; i < 16; i++) {
      for (int m = 0; m < 6; m++) {
        packet[6 + (i * 6) + m] = (byte)mac[m];
      }
    }

    if (sendCmd("AT+CIPSEND=102", 2000, ">")) {
      for (int i = 0; i < 102; i++) {
        Serial.write(packet[i]);
      }
    }
    sendCmd("AT+CIPCLOSE", 1000, "OK");
  }
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

  // Send WOL packet 3 times with brief pauses
  for (int i = 0; i < 3; i++) {
    sendMagicPacket();
    delay(500);
  }

  // Rapid visual blink indicating completion
  for (int i = 0; i < 10; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(150);
    digitalWrite(LED_PIN, LOW);
    delay(150);
  }
}

void loop() {
  // Empty
}
