#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include "config.h"
#include "storage.h"
#include "telemetry.h"
#include "time_service.h"

WiFiUDP udp;

// Visual activity blinker (Active LOW on ESP8266 GPIO2)
inline void blinkLed(int times, int ms) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, LOW); // Active LOW on ESP-12E
    delay(ms);
    digitalWrite(LED_PIN, HIGH);
    delay(ms);
  }
}

// Send Wake-on-LAN UDP magic packet (102 bytes) across multiple broadcast destinations
inline void sendWOLPacket(const byte* macAddress) {
  unsigned long startT = millis();
  char targetMacFormatted[18];
  macBytesToString(macAddress, targetMacFormatted);
  Serial.printf("[WOL] Assembling magic packet for MAC: %s\n", targetMacFormatted);

  byte magicPacket[102];
  memset(magicPacket, 0xFF, 6);
  for (int i = 0; i < 16; i++) {
    for (int m = 0; m < 6; m++) {
      magicPacket[6 + (i * 6) + m] = macAddress[m];
    }
  }

  // Bind UDP socket
  udp.begin(WOL_PORT);

  // Subnet-directed broadcast (e.g. 192.168.1.255) & Limited broadcast (255.255.255.255)
  IPAddress subnetBroadcast = ~WiFi.subnetMask() | WiFi.localIP();
  IPAddress limitedBroadcast(255, 255, 255, 255);

  // 1. Directed Subnet Broadcast on Port 9
  udp.beginPacket(subnetBroadcast, WOL_PORT);
  udp.write(magicPacket, sizeof(magicPacket));
  int res1 = udp.endPacket();

  // 2. Directed Subnet Broadcast on Port 7
  udp.beginPacket(subnetBroadcast, 7);
  udp.write(magicPacket, sizeof(magicPacket));
  int res2 = udp.endPacket();

  // 3. Limited Subnet Broadcast on Port 9
  udp.beginPacket(limitedBroadcast, WOL_PORT);
  udp.write(magicPacket, sizeof(magicPacket));
  int res3 = udp.endPacket();

  unsigned long latency = millis() - startT;
  Serial.printf("[WOL] Magic packet dispatched in %lums! Subnet %s:9 (res=%d), :7 (res=%d), 255.255.255.255:9 (res=%d)\n",
                latency, subnetBroadcast.toString().c_str(), res1, res2, res3);

  // Telemetry updates
  totalWolDispatches++;
  if (isNtpSynced()) {
    getFormattedDateTime(lastWolDispatchStr, sizeof(lastWolDispatchStr));
  } else {
    char upStr[20];
    getFormattedUptime(upStr, sizeof(upStr));
    snprintf(lastWolDispatchStr, sizeof(lastWolDispatchStr), "Up: %s", upStr);
  }
  addRequestLog("WOL Packet", 1, (uint16_t)latency);

  blinkLed(3, 80);
}
