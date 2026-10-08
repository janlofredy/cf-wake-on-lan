# CloudWOL / Cloud-Triggered Wake-on-LAN

An Always-On Device (AOD) Wake-on-LAN (WOL) remote server starter powered by **Cloudflare Workers + Cloudflare KV (100% Free Tier)** and microcontrollers:
1. **ESP32 Dev Board** (Recommended: standalone, fast TLS/HTTPS, low power)
2. **Arduino Uno + ESP8266 ESP-12E WiFi Shield** (Alternative combination)

---

## ⚡ How It Works

```mermaid
sequenceDiagram
    autonumber
    actor User as You (Phone / Terminal / Web)
    participant CF as Cloudflare Worker + KV
    participant MCU as ESP32 / Arduino AOD
    participant Target as Target Server

    User->>CF: POST /trigger (arms state in KV)
    Note over MCU,CF: Polls every 30-60s (Free tier: 100k reads/day)
    MCU->>CF: GET /status
    CF-->>MCU: { "trigger": true, "mac": "..." }
    MCU->>Target: Broadcast UDP Magic Packet (Port 9 / 255.255.255.255)
    Note over Target: Server powers on via WOL
    MCU->>CF: POST /ack (disarms trigger)
```

Both boards automatically boot and resume polling on power restore (AOD design).

---

## 📁 Repository Structure

```
├── cloudflare-worker/
│   ├── package.json
│   ├── wrangler.toml
│   └── src/
│       └── index.js
├── firmware/
│   ├── config.h.example
│   ├── esp32/
│   │   └── esp32_wol.ino
│   └── arduino_uno_esp8266/
│       └── arduino_uno_esp8266_wol.ino
├── .gitignore
└── README.md
```

---

## 🚀 Setup Guide

### 1. Cloudflare Worker Setup (Free Tier)

1. Install Wrangler CLI:
   ```bash
   npm install -g wrangler
   ```
2. Log in to Cloudflare:
   ```bash
   wrangler login
   ```
3. Create a free KV namespace:
   ```bash
   cd cloudflare-worker
   wrangler kv namespace create WOL_STORE
   ```
4. Copy the returned `id` and paste it inside [cloudflare-worker/wrangler.toml](file:///Users/janlofredy/Documents/ServerStarter/cloudflare-worker/wrangler.toml).
5. Deploy the worker:
   ```bash
   wrangler deploy
   ```
6. Set your secret authentication token:
   ```bash
   wrangler secret put API_KEY
   # Enter your preferred secret token (e.g. MySecretToken123)
   ```

---

### 2. Microcontroller Setup

#### Option A: ESP32 Dev Board (Recommended)
1. In Arduino IDE:
   - Install **ESP32 by Espressif Systems** via Boards Manager.
   - Install **ArduinoJson** library via Library Manager.
2. Duplicate `firmware/config.h.example` to `firmware/config.h` and configure:
   - Wi-Fi SSID & Password
   - Your Cloudflare Worker URL
   - Secret Auth Token
   - Target Server MAC Address
3. Connect ESP32 via Micro-USB and upload [esp32_wol.ino](file:///Users/janlofredy/Documents/ServerStarter/firmware/esp32/esp32_wol.ino).

#### Option B: Arduino Uno + ESP8266 ESP-12E Shield
1. Mount the ESP8266 ESP-12E WiFi shield onto the Arduino Uno.
2. Set the shield DIP switches to SoftwareSerial mode (e.g., Uno Pin 2 & 3).
3. Open [arduino_uno_esp8266_wol.ino](file:///Users/janlofredy/Documents/ServerStarter/firmware/arduino_uno_esp8266/arduino_uno_esp8266_wol.ino).
4. Update the Wi-Fi credentials, Worker domain, token, and MAC address.
5. Upload to Arduino Uno.

---

### 3. Triggering Wake-on-LAN from Anywhere

Send an authenticated HTTP POST request to trigger the server boot from anywhere (curl, iOS Shortcuts, Tasker, or browser):

```bash
curl -X POST "https://YOUR_WORKER_SUBDOMAIN.workers.dev/trigger" \
  -H "Authorization: Bearer YOUR_SECRET_TOKEN" \
  -H "Content-Type: application/json"
```

*(Optional)* Trigger a specific MAC address dynamically:
```bash
curl -X POST "https://YOUR_WORKER_SUBDOMAIN.workers.dev/trigger" \
  -H "Authorization: Bearer YOUR_SECRET_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"mac": "11:22:33:44:55:66"}'
```

---

## ⚙️ Free Tier Quota Details (Cloudflare KV)
- **Reads**: 100,000 / day
- **Writes**: 1,000 / day
- At a 30-second polling interval: `(86,400s / 30s) = 2,880 reads/day` (under **3%** of the free daily limit).
