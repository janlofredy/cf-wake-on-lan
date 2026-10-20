# ESP8266 ESP-12E WiFi Shield: Comprehensive Hardware & Setup Guide

> **Archived Offline Reference**: This document serves as a complete, standalone offline guide for understanding, configuring, troubleshooting, and flashing the **ESP8266 ESP-12E UART Wireless WiFi Shield**. It permanently archives the critical hardware analysis, board clone differences, and flashing procedures originally documented in external tutorials (such as Tiago Souza's *Instructables* guide and Korean community setup walkthroughs) in case those external URLs become broken or unreachable.

---

## 📌 Table of Contents
1. [Overview & Shield Architecture](#-overview--shield-architecture)
2. [Direct Programming: ESP8266 Standalone vs. Arduino MCU](#-direct-programming-esp8266-standalone-vs-arduino-mcu)
3. [Pinout & Hardware Layout](#-pinout--hardware-layout)
4. [The Classic "Pin 0 and Pin 1 Bending Trick"](#-the-classic-pin-0-and-pin-1-bending-trick)
   - [Why the Bending Trick Was Invented](#why-the-bending-trick-was-invented)
   - [How the Trick Works (Step-by-Step)](#how-the-trick-works-step-by-step)
   - [Modern Non-Destructive Alternative (No Bending Needed)](#modern-non-destructive-alternative-no-bending-needed)
5. [Board Identification ("Shield" vs "Shiald" vs "Moer")](#-board-identification-shield-vs-shiald-vs-moer)
   - [The Infamous "Moer" Clone Hardware Defect (J3Y vs J1Y)](#the-infamous-moer-clone-hardware-defect-j3y-vs-j1y)
   - [Why Standalone Flashing Completely Bypasses the Flaw](#why-standalone-flashing-completely-bypasses-the-flaw)
6. [DIP Switch Truth Table & Operating Modes](#-dip-switch-truth-table--operating-modes)
7. [Flashing Firmware Directly to ESP8266 (Step-by-Step)](#-flashing-firmware-directly-to-esp8266-step-by-step)
   - [Method 1: Using Arduino Uno as USB-UART Bridge (Recommended)](#method-1-using-arduino-uno-as-usb-uart-bridge-recommended)
   - [Method 2: Using an External USB-to-TTL Adapter](#method-2-using-an-external-usb-to-ttl-adapter)
8. [Arduino IDE Configuration](#-arduino-ide-configuration)
9. [Troubleshooting & Common Pitfalls](#-troubleshooting--common-pitfalls)
10. [Original Sources & References](#-original-sources--references)

---

## 🔍 Overview & Shield Architecture

The **ESP8266 ESP-12E UART Wireless WiFi Shield** is an Arduino Uno-compatible expansion board designed to bring 802.11b/g/n Wi-Fi capabilities to microcontrollers. Designed originally by WangTongze (*elecshop.ml* / *Doit*), it is widely cloned and sold across online electronics stores.

While originally marketed as a slave modem that communicates with the Arduino over AT commands, the onboard **ESP-12E** module is actually a standalone **32-bit Tensilica L106 microcontroller running at 80/160 MHz** with **4MB (32 Mbit) flash memory**. Running code directly on the ESP8266 turns the shield into a self-contained, high-performance IoT controller, completely bypassing AT command parsing and `SoftwareSerial` bottlenecks.

### Key Specifications:
- **MCU**: Tensilica Xtensa 32-bit LX106 @ 80 MHz / 160 MHz
- **Memory**: 4 MB (32 Mbit) SPI Flash, 64 KB instruction RAM, 96 KB data RAM
- **Wi-Fi**: 802.11 b/g/n (2.4 GHz), WPA/WPA2, Station (STA) + Access Point (AP) modes
- **Form Factor**: Standard Arduino Uno R3 footprint (compatible with Uno, Mega 2560, Leonardo)
- **Power Supply**: 5V from Arduino header $\rightarrow$ onboard **AMS1117-3.3V** LDO regulator powers the ESP8266 (draws up to ~250mA peak during RF transmission)
- **Logic Level Shifting**: Built-in 5V $\leftrightarrow$ 3.3V bidirectional level translation between Arduino digital pins and ESP8266 GPIOs
- **Status LEDs**:
  - `PWR`: Power indicator (Red)
  - `DFU`: Download Firmware Upgrade mode (Lights up when GPIO0 is pulled LOW via DIP 4)
  - `AP`: Access Point active indicator
  - `STA`: Station mode connected indicator
- **Buttons**:
  - `ESP-RST`: Direct hardware reset for the ESP8266 module
  - `RESET`: Pass-through reset for the underlying Arduino Uno board

---

## 💻 Direct Programming: ESP8266 Standalone vs. Arduino MCU

A major source of confusion with this shield stems from **where the program is actually running**. There are two completely different architectural paradigms:

### Paradigm Comparison:

```
[Legacy Arduino AT Architecture]
+-------------------------+      Serial / AT Commands      +----------------------+
|  Arduino Uno MCU        | =============================> |  ESP8266 ESP-12E     |
|  (ATmega328P 8-bit)     |   (Flaky 9600 baud text,       |  (Dumb Slave Modem   |
|  *Your Sketch Runs Here*|    SoftwareSerial bottleneck)  |   with AT firmware)  |
+-------------------------+                                +----------------------+
         ▲
         | (Bottleneck: Limited 2KB RAM, text parsing errors, fragile HTTP/TLS)

[Modern Standalone ESP8266 Architecture (Used in this Repo)]
+-------------------------+                                +----------------------+
|  Arduino Uno Board      |                                |  ESP8266 ESP-12E     |
|  (MCU Silenced:         |                                |  (Tensilica 32-bit   |
|   RESET jumpered to GND)|                                |   80/160MHz, 4MB)    |
|                         |                                |                      |
|  *Only used as physical | ===== DuPont Jumpers ========> |  *Full Standalone    |
|   bracket & USB-Serial  |      (During Flashing Only)    |   Firmware Runs Here*|
|   bridge for flashing*  |                                |  - Native Wi-Fi/UDP  |
+-------------------------+                                |  - Web UI Dashboard  |
                                                           |  - NTP Time Clock    |
                                                           |  - Cloudflare Client |
                                                           +----------------------+
```

| Feature | Legacy AT Approach (Code on Arduino) | Direct ESP Programming (Code on ESP8266) |
| :--- | :--- | :--- |
| **Target Device in Arduino IDE** | `Arduino Uno` (ATmega328P) | `Generic ESP8266 Module` (ESP-12E) |
| **Processor & Clock** | 8-bit AVR @ 16 MHz | 32-bit Tensilica Xtensa @ 80 / 160 MHz |
| **SRAM & Storage** | 2 KB SRAM / 32 KB Flash | 80 KB RAM / **4 MB (4096 KB)** Flash |
| **Communication Stack** | Manual `AT+CWJAP`, `AT+CIPSTART` text commands | Native hardware C++ Wi-Fi & UDP stack (`ESP8266WiFi`, `WiFiUdp`) |
| **Reliability** | Prone to baud-rate mismatches, buffer truncation | 100% rock-solid, zero buffer overruns |
| **Arduino Uno Role** | Main orchestrator | Passive USB bridge (or completely removed after flash) |

### How Direct ESP Programming Works in this Project:
1. You load the `.ino` sketch ([`firmware/esp8266_standalone_wol/esp8266_standalone_wol.ino`](firmware/esp8266_standalone_wol/esp8266_standalone_wol.ino)) onto the **ESP-12E chip itself**, not the Arduino Uno.
2. The Arduino Uno's ATmega328P processor is put to sleep by jumpering **`RESET` to `GND`**.
3. The Uno's onboard USB-to-UART bridge (ATmega16U2/CH340) feeds the Arduino IDE compiler output directly into the ESP-12E bootloader.
4. Once flashed, the ESP8266 independently handles Wi-Fi connection, DNS captive portal, Web UI configuration server, NTP time synchronization, LAN server pinging, and UDP Wake-on-LAN dispatching!

---

## 🗺️ Pinout & Hardware Layout

```
         +---------------------------------------------------+
         | [ESP-RST]                                [DFU LED]|
         |                                                   |
         |  +---------+   +------+  +---------------------+  |
         |  |         |   | DIP  |  |  ESP-12E MODULE     |  |
         |  | AMS1117 |   |SWITCH|  |  (32-bit ESP8266)   |  |
         |  |  3.3V   |   |1 2 3 4| |                     |  |
         |  +---------+   +------+  +---------------------+  |
         |                                                   |
         |  [Maintenance Pins: 5V / 3V3 / GND]               |
         |  [ADC: 0-1V and 0-3.3V pins]                      |
         |                                                   |
         |  Arduino Uno Header Rails (D0-D13 / A0-A5 / Power)|
         +---------------------------------------------------+
```

### Functional Regions:
1. **DIP Switch (4-position)**: Switches operating modes between Arduino serial passthrough, standalone running, and ESP flash download.
2. **Maintenance Header (`Maintenance Only`)**: 3-pin test header connected to the AMS1117 regulator. *Do not use as a primary power source.*
3. **ADC Header**: Two analog inputs:
   - **`0-1V`**: Direct connection to ESP8266 internal ADC (pin TOUT / ADC0).
   - **`0-3.3V`**: Route through an onboard resistor divider so standard 3.3V sensors can be sampled safely.
4. **Serial Jumpers**: Near the digital pins, routing ESP8266 `TXD0` and `RXD0` either to Arduino Uno pins `D0`/`D1` or to female breakout headers for DuPont jumpers.

---

## 🪛 The Classic "Pin 0 and Pin 1 Bending Trick"

Anyone who read early online tutorials for this shield (notably Tiago Souza's widely circulated *Instructables* guide) likely encountered the **"Pin 0 and 1 Bending Trick"**.

### Why the Bending Trick Was Invented
The shield was originally laid out so that when stacked onto an Arduino Uno:
- ESP8266 TX is routed toward Arduino **`D0` (RX)**.
- ESP8266 RX is routed toward Arduino **`D1` (TX)**.

This routing assumes the **ATmega328P MCU** on the Uno is talking to the ESP8266. However, on the Arduino Uno, pins `D0` and `D1` are also physically wired to the **onboard USB-to-UART bridge** (the ATmega16U2 or CH340 chip connected to your computer):
1. **Serial Bus Contention**: Whenever you plug in the USB cable to upload a sketch or open the Arduino Serial Monitor, the Uno's USB chip and the ESP8266 both fight for control over `D0` and `D1`. Sketch uploads to the Uno fail immediately.
2. **Serial RX/TX Inversion**: When using the Uno as a USB-to-Serial adapter from a PC to talk directly to the ESP8266, the PC's TX is Arduino's `D0` (USB chip TX), which requires connecting to ESP8266 RX. The straight pin headers stacked into the board formed a crossover conflict.

To solve this without cutting PCB traces, early makers devised the physical bending trick:

```
    SHIELD BOTTOM PINS (Underneath D0 & D1):
            
      [D2] [D3] [D4] ...  (Straight down into Uno headers)
       |    |    |
       |    |    |
      /    /              <-- Pins D0 & D1 BENT OUTWARDS (at ~90 degrees)
     [D0] [D1]                so they DO NOT insert into Uno sockets!
      |    |
      |    +--------------------+
      +--------------------+    | (Cross-jumper with DuPont wires)
                           |    |
      [D0 Socket]    [D1 Socket] (On Arduino Uno)
```

### How the Trick Works (Step-by-Step):
1. Locate the male pin headers on the bottom of the shield corresponding to **Digital Pin 0 (RX)** and **Digital Pin 1 (TX)**.
2. Using needle-nose pliers, gently bend those two pins outwards at a ~90-degree angle so that when you stack the shield onto the Arduino Uno, all pins insert into their sockets **except D0 and D1** (which hang outside).
3. Connect two DuPont female-to-male jumper wires:
   - From bent Shield **`D0`** $\rightarrow$ insert into Arduino Uno **`D1`** socket.
   - From bent Shield **`D1`** $\rightarrow$ insert into Arduino Uno **`D0`** socket.
4. This crossed the lines so the PC's USB-UART adapter spoke directly to the ESP8266 without conflicting with the Uno's native routing.

---

### Modern Non-Destructive Alternative (No Bending Needed!)

**In this project, you DO NOT need to bend or damage your shield's pins!** 

Modern standalone flashing accomplishes the exact same goal cleanly using software and DIP switches:
1. **DIP Switch Isolation**: Setting **DIP 1 to `OFF`** and **DIP 2 to `OFF`** internally disconnects the ESP8266's serial lines from the Uno header pins `D0` and `D1`.
2. **Silencing the Uno MCU**: By placing a jumper wire between Arduino Uno **`RESET`** and **`GND`**, the ATmega328P is completely disabled and tri-stated.
3. **Dedicated Auxiliary Breakout Headers**: You simply plug DuPont jumpers from the shield's female breakout header pins (`TX`/`RX`) directly into Uno `D0`/`D1` (or to an external FTDI adapter).

> [!TIP]
> **When to still use the Bending Trick**: If you happen to have a rare clone variant that lacks the auxiliary female TX/RX breakout header, or if the internal DIP switches are damaged/shorted, the "Pin 0 and 1 Bending Trick" remains a verified emergency fallback method to isolate and reroute the serial lines!

---

## 🏷️ Board Identification ("Shield" vs "Shiald" vs "Moer")

Because multiple clone factories produced this board, you will find three main silkscreen variants. Look closely at the white text printed on the PCB near the DIP switches and ESP-12 module:

```
Variant A: "Shield" + "More info & tech"    --> Standard Functional Board
Variant B: "Shiald" + "more info & tech"    --> Functional Clone (Typo only)
Variant C: "Shield" + "Moer info & tech"    --> DEFECTIVE HARDWARE CLONE!
```

### Detailed Breakdown:

| Silkscreen Marking | Spelling of "Shield" | Spelling of "More" | Transistor Type | Status / Compatibility |
| :--- | :--- | :--- | :--- | :--- |
| **Standard** | `Shield` | `More info & tech` | **J1Y** (BSS138 N-MOSFET) | ✅ Fully functional |
| **Typo Variant** | `Shiald` | `more info & tech` | **J1Y** (BSS138 N-MOSFET) | ✅ Fully functional (cosmetic typo only) |
| **Defective Clone** | `Shield` | **`Moer info & tech`** (or `Moer info $ tech`) | **J3Y** (S8050 NPN BJT) | ⚠️ **Defective Level Shifter** (Fails with Uno MCU) |

---

### The Infamous "Moer" Clone Hardware Defect (J3Y vs J1Y)

On standard boards, the bidirectional 5V $\leftrightarrow$ 3.3V level conversion between Arduino pins `D0`/`D1` and ESP8266 pins `RX`/`TX` is handled by two small SOT-23 surface-mount **N-Channel MOSFETs** labeled **`J1Y`** (BSS138).

In the budget **"Moer"** clone, the manufacturer mistakenly substituted **`J3Y`** transistors (S8050 general-purpose NPN bipolar junction transistors):
- **Why It Fails**: S8050 NPN transistors are intended for audio amplifiers, not bidirectional logic level shifting.
- **The Symptom**: When the shield is stacked on an Arduino Uno and DIP switches 1 & 2 are turned `ON`, the voltage on the Arduino's `D0` (RX) and `D1` (TX) lines is held at a constant 0V or stuck at ~2.8V.
- **Result**: You cannot upload any sketch to the Arduino Uno (`avrdude: stk500_recv(): programmer is not responding`), and AT serial commands completely fail.

#### Physical Hardware Fix (If you must use AT commands with Uno MCU):
Desolder the two `J3Y` SMD transistors near the DIP switch and solder in genuine `J1Y` (BSS138) MOSFETs using fine tweezers and a soldering iron.

---

### Why Standalone Flashing Completely Bypasses the Flaw!

In this project, **we do not use the Arduino Uno MCU or factory AT commands at all**:
1. By connecting Arduino Uno **`RESET` to `GND`**, the ATmega328P is completely held in tri-state (silenced).
2. The Arduino Uno's onboard USB-to-UART converter (ATmega16U2 or CH340) connects directly to the ESP8266 via DuPont jumpers.
3. The firmware runs **100% natively on the ESP-12E 32-bit CPU**.
4. The defective level shifters between the Uno MCU and ESP8266 are entirely out of the picture!

---

## 🎛️ DIP Switch Truth Table & Operating Modes

The 4-way DIP switch controls routing and boot mode:

| Switch | Name | Function when `ON` | Function when `OFF` |
| :---: | :---: | :--- | :--- |
| **1 (P1)** | ESP TX $\rightarrow$ Arduino | Connects ESP8266 TX to Arduino pin D0/D1 | Disconnects ESP8266 TX from Arduino |
| **2 (P2)** | ESP RX $\leftarrow$ Arduino | Connects ESP8266 RX to Arduino pin D1/D0 | Disconnects ESP8266 RX from Arduino |
| **3 (P3)** | Flash Boot Enable | Enables flash download mode circuit | Normal boot mode |
| **4 (P4)** | GPIO0 Pull-Down | Pulls ESP8266 **GPIO0 LOW** (Lights **`DFU` LED**) | GPIO0 floats HIGH (Normal run mode) |

---

### Configuration Matrix:

#### 1. 📥 Direct Flashing Mode (Upload code to ESP8266):
```
DIP 1: OFF
DIP 2: OFF
DIP 3: ON
DIP 4: ON   <-- (DFU LED lights up red)
```
*Note*: Always tap the **`ESP-RST`** button after setting these switches to enter the bootloader.

#### 2. ⚡ Standalone Production Run Mode (Always-On WOL Starter):
```
DIP 1: ON  (or leave DuPont jumpers wired to Uno USB)
DIP 2: ON  (or leave DuPont jumpers wired to Uno USB)
DIP 3: OFF
DIP 4: OFF  <-- (DFU LED turns off)
```
*Note*: Tap the **`ESP-RST`** button after toggling. The ESP8266 will boot into the standalone firmware and start its web dashboard.

#### 3. 🛑 Arduino AT Command Passthrough Mode (Legacy):
```
DIP 1: OFF
DIP 2: OFF
DIP 3: OFF
DIP 4: OFF
```
*(With SoftwareSerial jumpers connected to Uno pins D2 and D3).*

---

## ⚡ Flashing Firmware Directly to ESP8266 (Step-by-Step)

### Method 1: Using Arduino Uno as USB-UART Bridge (Recommended)

You do not need a separate FTDI programmer; your Arduino Uno already has a high-quality USB-to-Serial converter onboard.

```
+--------------------+                       +---------------------------+
|    Arduino Uno     |                       | ESP8266 ESP-12E Shield    |
|                    |                       |                           |
|  [RESET] ----+     |                       |                           |
|              |     |                       |                           |
|  [ GND ] ----+     |                       |                           |
|                    |                       |                           |
|  [  D1 (TX)  ] <=========================> | [ TX / TXD Pin ]          |
|  [  D0 (RX)  ] <=========================> | [ RX / RXD Pin ]          |
|  [  5V / GND ] (Stacked into header rails) | [ 5V / GND     ]          |
+--------------------+                       +---------------------------+
```

1. **Silence the Arduino MCU**:
   - Insert a jumper wire between the Arduino Uno **`RESET`** pin and **`GND`**.
   - *Explanation*: This keeps the ATmega328P halted so its TX/RX pins go high-impedance, letting the USB chip talk straight to the ESP8266.
2. **Mount the Shield**:
   - Seat the ESP-12E WiFi Shield onto the Arduino Uno headers (this provides 5V and GND power).
3. **Set DIP Switches to Flash Mode**:
   - Set **DIP 1**: `OFF`
   - Set **DIP 2**: `OFF`
   - Set **DIP 3**: `ON`
   - Set **DIP 4**: `ON` (Confirm the red **`DFU` LED** lights up).
4. **Connect Serial Jumpers**:
   - Connect a DuPont female-to-male wire from Shield **`TX` / `TXD`** $\rightarrow$ Uno **`D1` (TX)**.
   - Connect a DuPont female-to-male wire from Shield **`RX` / `RXD`** $\rightarrow$ Uno **`D0` (RX)**.
   - *(Note: Since the Uno MCU is bypassed, connect TX to TX and RX to RX so the PC's USB-UART bridge routes straight to the ESP).*
   - *If your specific clone variant swaps silkscreen labels and the upload fails, swap the two jumpers: Shield TX $\rightarrow$ Uno D0 and Shield RX $\rightarrow$ Uno D1.*
5. **Connect USB & Reset**:
   - Plug the Arduino Uno USB cable into your computer.
   - Press the **`ESP-RST`** button on the shield once.
6. **Upload in Arduino IDE**:
   - Follow the [Arduino IDE Configuration](#-arduino-ide-configuration) section below and click **Upload**.
7. **Switch to Run Mode**:
   - Set **DIP 3**: `OFF`
   - Set **DIP 4**: `OFF`
   - Set **DIP 1**: `ON` and **DIP 2**: `ON` (or leave the DuPont jumpers in place).
   - Press the **`ESP-RST`** button. The ESP8266 will boot into production mode!

---

### Method 2: Using an External USB-to-TTL Adapter

If you have a dedicated 3.3V USB-UART adapter (e.g., CP2102, FT232RL, or CH340):

1. **Wiring**:
   - Adapter **`3V3`** $\rightarrow$ Shield **`3.3V`** (or Adapter `5V` $\rightarrow$ Shield `5V`)
   - Adapter **`GND`** $\rightarrow$ Shield **`GND`**
   - Adapter **`TX`** $\rightarrow$ Shield **`RX` / `RXD`**
   - Adapter **`RX`** $\rightarrow$ Shield **`TX` / `TXD`**
2. **DIP Switches**:
   - Set **DIP 1: OFF, 2: OFF, 3: ON, 4: ON**.
3. **Press `ESP-RST`**, upload firmware, then switch DIP 3 & 4 `OFF` to run.

---

## 💻 Arduino IDE Configuration

1. **Add ESP8266 Board Manager Repository**:
   - Go to **Arduino IDE $\rightarrow$ Settings / Preferences**.
   - In **Additional Boards Manager URLs**, paste:
     ```
     http://arduino.esp8266.com/stable/package_esp8266com_index.json
     ```
2. **Install Board Core**:
   - Open **Tools $\rightarrow$ Board $\rightarrow$ Boards Manager...**
   - Search for **`esp8266`** and install **`esp8266 by ESP8266 Community`** (v2.7.4, v3.0.2, or v3.1.2).
3. **Select Board Settings**:
   - **Board**: `Tools > Board > ESP8266 Boards > Generic ESP8266 Module`
   - **Flash Size**: `4MB (FS:2MB OTA:~1019KB)` *(or `512KB` if using a very early ESP-12 model)*
   - **CPU Frequency**: `80 MHz` (or `160 MHz`)
   - **Crystal Frequency**: `26 MHz`
   - **Flash Mode**: `DOUT` (compatible with all flash chip brands)
   - **Upload Speed**: `115200`
   - **Port**: Select the COM/tty port corresponding to your Arduino or USB converter.
4. **Compile & Upload**:
   - Open [`firmware/esp8266_standalone_wol/esp8266_standalone_wol.ino`](firmware/esp8266_standalone_wol/esp8266_standalone_wol.ino).
   - Press **`ESP-RST`** on the shield.
   - Click **Upload** ($\rightarrow$).

---

## 🛠️ Troubleshooting & Common Pitfalls

### 1. `error: espcomm_sync failed` or `Timed out waiting for packet header`
- **Cause**: The ESP8266 did not enter download mode, or the serial lines are crossed.
- **Checklist**:
  - Is the Arduino Uno **`RESET`** jumpered securely to **`GND`**? (If not, the ATmega328P interferes).
  - Are **DIP 3 and 4 ON**? Is the red **`DFU` LED** illuminated?
  - Did you tap the **`ESP-RST`** button right before clicking upload?
  - **Swap TX and RX wires**: Many clone boards invert the `TX`/`RX` labels on their silkscreen. Swap Uno D0 and D1 jumpers and try again.

### 2. Random Reset / Wi-Fi Dropouts during Transmission (Brownout)
- **Cause**: ESP8266 Wi-Fi transmissions require up to ~250-300mA current spikes.
- **Remedy**: Low-quality USB cables or unpowered USB hubs can drop below 4.5V under load. Connect your Arduino Uno to an external 9V-12V 1A DC power supply or plug directly into a powered PC USB port.

### 3. Setup AP (`ServerStarter-Setup`) Not Showing Up
- **Explanation**: On boot, the firmware first attempts to connect to your configured Wi-Fi credentials for ~25 seconds.
- If it cannot connect (or if newly flashed without credentials), it automatically falls back to AP mode. Wait 25–30 seconds after powering on, and `ServerStarter-Setup` will broadcast at `http://192.168.4.1`.

### 4. Legacy AT Firmware Restoration (If Ever Needed)
If you ever want to restore the factory Ai-Thinker AT command set:
- Tool: **Espressif Flash Download Tools** (v2.4 or v3.x)
- Binary: `Ai-Thinker_ESP8266_DOUT_32Mbit_v1.5.4.1-a AT Firmware.bin`
- Flash Address: `0x00000`
- SPI Mode: `DOUT`, Speed: `40MHz`, Size: `32Mbit`
- Baud: `115200`

---

## 📚 Original Sources & References

Permanent attribution and citations for the underlying research and tutorials:

1. **Instructables Complete Tutorial**:
   - *Title*: "ESP8266 ESP-12E UART Wireless WIFI Shield TTL Converter Uncomplicated"
   - *Author*: Tiago Souza (Campina Grande, Brazil)
   - *Original URL*: `https://www.instructables.com/ESP8266-ESP-12E-UART-Wireless-WIFI-Shield-TTL-Conv/`
   - *Key Contribution*: In-depth analysis of the "Moer" clone transistor flaw (J3Y vs J1Y) and DIP switch matrix.
2. **Video Walkthrough (Part 1 - Hardware Setup & Bridge Mode)**:
   - *Title*: "Arduino Uno 에 ESP8266 WIFI Shield 를 장착해서 사용해보기"
   - *Original Video ID*: `Y1GWoiuE-Ls` (`https://www.youtube.com/watch?v=Y1GWoiuE-Ls`)
3. **Video Walkthrough (Part 2 - Standalone Web Server & Flashing)**:
   - *Title*: "Arduino Uno 에 ESP8266 WIFI Shield 를 장착해서 Wepserver HelloServer 예제 테스트 해 보기"
   - *Original Video ID*: `jCL4VuFRKvQ` (`https://www.youtube.com/watch?v=jCL4VuFRKvQ`)
4. **Original Hardware Designer**: WangTongze (`elecshop.ml` / *Doit* open-source hardware initiative).
