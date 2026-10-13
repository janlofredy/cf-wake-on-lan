#include <SoftwareSerial.h>

/**
 * DEEP DIAGNOSTIC SCANNER FOR WANGTONGZE ESP8266 SHIELD
 * 
 * Tests:
 * 1. Checks every common baud rate: 9600, 19200, 38400, 57600, 74880, 115200.
 * 2. Dumps raw incoming bytes so you can see if the ESP is outputting boot data or garbage.
 * 3. Interactive Serial Passthrough: You can type "AT" in the Serial Monitor and see what comes back!
 */



// SoftwareSerial on D2 (RX) and D3 (TX)
SoftwareSerial espSerial(2, 3);

long bauds[] = {115200, 9600, 74880, 57600, 38400, 19200};
const int numBauds = 6;

bool testBaud(long baud) {
  Serial.print(F("Testing baud: "));
  Serial.print(baud);
  Serial.print(F(" ... "));

  espSerial.begin(baud);
  delay(200);

  // Clear buffer
  while (espSerial.available()) espSerial.read();

  // Send AT\r\n
  espSerial.print("AT\r\n");

  long timeout = millis() + 1000;
  String reply = "";
  while (millis() < timeout) {
    while (espSerial.available()) {
      char c = (char)espSerial.read();
      reply += c;
    }
    if (reply.indexOf("OK") != -1) {
      Serial.println(F(">>> SUCCESS! (Received OK) <<<"));
      return true;
    }
  }

  if (reply.length() > 0) {
    Serial.print(F("Heard response: ["));
    Serial.print(reply);
    Serial.println(F("] (No OK found)"));
  } else {
    Serial.println(F("No response."));
  }
  return false;
}

void setup() {
  Serial.begin(9600); // PC Serial Monitor
  delay(1500);

  Serial.println(F("\n=========================================="));
  Serial.println(F("  ESP8266 DEEP HARDWARE DIAGNOSTIC SCAN   "));
  Serial.println(F("=========================================="));
  Serial.println(F("Wiring expected:"));
  Serial.println(F("  Shield TXD (or TX) -> Uno Pin D2"));
  Serial.println(F("  Shield RXD (or RX) -> Uno Pin D3"));
  Serial.println(F("  DIP Switches 1, 2, 3, 4: ALL OFF"));
  Serial.println(F("------------------------------------------"));

  long workingBaud = 0;
  for (int i = 0; i < numBauds; i++) {
    if (testBaud(bauds[i])) {
      workingBaud = bauds[i];
      break;
    }
    delay(200);
  }

  Serial.println(F("------------------------------------------"));
  if (workingBaud != 0) {
    Serial.print(F(">>> ESP8266 is OPERATIONAL at "));
    Serial.print(workingBaud);
    Serial.println(F(" baud! <<<"));
  } else {
    Serial.println(F(">>> SCAN FINISHED: No AT response on any baud rate. <<<"));
    Serial.println(F("Now listening for any raw boot messages (press shield RESET button to test)..."));
  }
  Serial.println(F("Interactive Passthrough active: Type AT in Serial Monitor and press Enter.\n"));

  if (workingBaud == 0) workingBaud = 115200;
  espSerial.begin(workingBaud);
}

void loop() {
  // Forward anything typed in Serial Monitor to ESP8266
  if (Serial.available()) {
    char c = Serial.read();
    espSerial.write(c);
  }

  // Forward anything heard from ESP8266 to Serial Monitor
  if (espSerial.available()) {
    char c = espSerial.read();
    Serial.write(c);
  }
}
