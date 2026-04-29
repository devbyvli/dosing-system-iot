# 💧 Automatic Liquid Dosing System — IoT Edition

> Arduino/ESP32 embedded project with real-time WiFi monitoring, MQTT control, and web dashboard.  
> Built by **Valentin Caloianu** — Embedded Systems Engineer

---

## 🎯 Project Overview

An automated liquid dosing system that monitors tank water levels using an ultrasonic sensor and controls a pump via relay. Features full IoT connectivity — live data is published to an MQTT broker every 2 seconds and a built-in web dashboard allows remote pump control from any browser.

This project was built and tested entirely in **Wokwi simulator** — no physical hardware required for demonstration.

---

## ✨ Features

- ✅ Automatic pump ON/OFF based on water level thresholds
- ✅ Real-time LCD display (level + pump status)
- ✅ WiFi connectivity via ESP32
- ✅ MQTT publish every 2s → `dosing/status` and `dosing/alert`
- ✅ Remote pump control via MQTT → `dosing/control` (ON / OFF / RESET)
- ✅ Built-in HTTP web dashboard with auto-refresh
- ✅ Safety timeout — pump auto-off after 10 seconds
- ✅ Visual indicators: green LED (pump ON), red LED (tank FULL)
- ✅ Buzzer alerts: 1x startup, 2x tank full, 3x timeout error
- ✅ State machine: IDLE → PUMPING → FULL → ERROR

---

## 🛠️ Hardware Components

| Component | Model | Pin |
|---|---|---|
| Microcontroller | ESP32 DevKit C v4 | — |
| Ultrasonic sensor | HC-SR04 | TRIG: GPIO5, ECHO: GPIO18 |
| LCD Display | 16x2 I2C | SDA: GPIO21, SCL: GPIO22 |
| Relay module | 5V relay | IN: GPIO19 |
| LED (pump ON) | Green LED | GPIO2 |
| LED (tank FULL) | Red LED | GPIO4 |
| Buzzer | Passive buzzer | GPIO15 |

---

## 📡 MQTT Topics

| Topic | Direction | Payload | Description |
|---|---|---|---|
| `dosing/status` | Publish | JSON | Distance, pump state, uptime every 2s |
| `dosing/alert` | Publish | JSON | PUMP_START, TANK_FULL, TIMEOUT alerts |
| `dosing/control` | Subscribe | String | ON / OFF / RESET commands |

**Broker:** `broker.hivemq.com:8884` (WSS)

**Example status payload:**
```json
{
  "distance": 15,
  "pump": false,
  "state": "IDLE",
  "uptime_s": 42,
  "ip": "10.0.0.2"
}
```

---

## 🌐 Web Dashboard

The ESP32 hosts a web dashboard accessible at its IP address (shown in Serial Monitor).

| Endpoint | Description |
|---|---|
| `GET /` | Live dashboard with auto-refresh every 2s |
| `GET /status` | JSON status endpoint |
| `GET /control?cmd=ON` | Turn pump ON via HTTP |
| `GET /control?cmd=OFF` | Turn pump OFF via HTTP |

---

## ⚙️ System Logic

```
Distance > 20 cm  →  Tank LOW   →  Pump ON
Distance < 5 cm   →  Tank FULL  →  Pump OFF + Red LED + 2x Buzzer
Pump ON > 10s     →  TIMEOUT    →  Pump OFF + 3x Buzzer + Error state
Remote cmd "OFF"  →  Pump OFF immediately (MQTT or HTTP)
```

---

## 📊 State Machine

```
         ┌─────────────────────────────────────┐
         ▼                                     │
       IDLE  ──(dist > 20cm)──►  PUMPING       │
         ▲                        │    │       │
         │                        │    │       │
    (dist > 20cm)         (dist < 5cm) (10s timeout)
         │                        │    │
       FULL ◄────────────────────┘    ▼
                                   ERROR
                                  (retry 5s)
```

---

## 🚀 Libraries Required

```cpp
#include <Wire.h>              // Built-in
#include <LiquidCrystal_I2C.h> // by Frank de Brabander
#include <WiFi.h>              // Built-in ESP32
#include <PubSubClient.h>      // by Nick O'Leary
#include <WebServer.h>         // Built-in ESP32
#include <ArduinoJson.h>       // by Benoit Blanchon
```

---

## 🖥️ Run in Wokwi Simulator

1. Open [wokwi.com](https://wokwi.com) and create a new **ESP32** project
2. Paste the code from `liquid_dosing_ESP32_IoT.ino`
3. Import `diagram.json` for the full circuit
4. Install libraries: `PubSubClient`, `ArduinoJson`, `LiquidCrystal I2C`
5. Press **Run ▶**
6. Open [mqttx.app](https://mqttx.app/web) → connect to `broker.hivemq.com:8884`
7. Subscribe to `dosing/status` to see live data
8. Publish `ON` / `OFF` to `dosing/control` to control the pump remotely

---

## 📸 Demo

| Wokwi Simulation | MQTT Live Data |
|---|---|
| LCD shows level + pump status | JSON payload every 2 seconds |
| Green LED = pump active | Remote control via MQTT |
| Red LED = tank full | Web dashboard at ESP32 IP |

---

## 💼 About This Project

This project demonstrates my skills in:
- **Embedded C++** — state machine, sensor reading, PWM control
- **IoT connectivity** — WiFi, MQTT pub/sub, HTTP server
- **Hardware integration** — ultrasonic sensor, relay, LCD, LEDs
- **Real-time systems** — non-blocking loop, safety timeouts

> Available for freelance embedded/IoT projects on [Upwork](https://www.upwork.com).  
> Contact: open to fixed-price and hourly contracts.

---

## 📄 License

MIT License — free to use and modify with attribution.

---

*Built with C++ · Arduino · ESP32 · MQTT · Wokwi*
