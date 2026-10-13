/**
 * TEST 1: ESP8266 Shield AT Communication & Power Check
 * 
 * Objective: Verify that the Arduino Uno can talk to the ESP8266 shield
 *            and that the shield is alive and responding to AT commands.
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
 * - FAST RAPID BLINKING: SUCCESS! AT communication is working.
 * - SOLID ON: FAILED. Shield is not responding or DIP switches 1 & 2 are not ON.
 */

#define LED_PIN 13

void blinkRapidly() {
  while (true) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
}

bool testBaud(long baud) {
  Serial.begin(baud);
  delay(500);

  // Clear serial buffer
  while (Serial.available()) Serial.read();

  // Send AT test
  Serial.println("AT");
  long timeout = millis() + 1500;
  String response = "";
  while (millis() < timeout) {
    while (Serial.available()) {
      response += (char)Serial.read();
    }
    if (response.indexOf("OK") != -1) {
      return true;
    }
  }
  return false;
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  delay(1500);

  // Try 115200 first (default for WangTongze ESP-12E)
  if (testBaud(115200)) {
    blinkRapidly(); // SUCCESS!
  }

  // Try 9600 as fallback
  if (testBaud(9600)) {
    blinkRapidly(); // SUCCESS!
  }

  // If both failed, turn LED solid ON to indicate failure
  digitalWrite(LED_PIN, HIGH);
}

void loop() {
  // Empty
}
