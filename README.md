# IoT ESP32-C6-Zero Smart Filament Dryer Controller

An intelligent, smart-home enabled environmental controller for 3D printing filament drying chambers. Built for the **Waveshare ESP32-C6-Zero** using **IRLZ44N Logic-Level MOSFETs**, this firmware provides non-blocking temperature regulation, smart humidity-targeted cycles, visual captive portal provisioning, and complete **Home Assistant MQTT Auto-Discovery**. It supports **Sensirion SHT30D/SHT31** sensors via I2C.

---

## 🌟 Premium Features

1. **Smart Humidity-Targeted Drying**:
   * **Active Drying (`STATE_HEATING_UP`)**: Actively runs the heater at the material's ideal drying temperature and spins the fan at 100%. This phase **continues indefinitely until your target humidity (default 15% RH) is reached**.
   * **Maintenance Phase (`STATE_MAINTAINING`)**: Throttles the heater and fan down to low circulation speeds to conserve energy while keeping the spool dry and ready.
2. **Material Preset Library**: Preconfigured drying parameters for standard filaments:
   * **PLA** (45°C, 4 hours)
   * **PETG** (55°C, 4 hours)
   * **CoPE** (55°C, 4 hours)
   * **ASA** (65°C, 6 hours)
   * **ABS** (65°C, 6 hours)
   * **TPU** (50°C, 4 hours)
   * **Manual** (Fully customizable temp/humid/timer slider overrides in Home Assistant)
3. **Provisioning Captive Portal**:
   * If unconfigured, the device boots into Access Point (AP) mode as `DRYER-XXXXXX` (using the device's unique MAC) with the password `dryer123`.
   * Accessing `http://192.168.4.1/` presents a responsive dark-mode portal to easily input WiFi, MQTT broker, and Unit Number details without changing code.
4. **Home Assistant MQTT Auto-Discovery**:
   * Automatically publishes discovery topics. The moment your device connects to your broker, it instantly registers in Home Assistant with a dashboard including:
     * Sensors: Chamber Temp, Chamber Humidity, Time Remaining, and Status.
     * Select Dropdown: Active Filament Preset.
     * Switch: Dryer Power (ON/OFF).
     * Sliders: Temperature Target, Humidity Target, and Session Timer overrides.
5. **Advanced Hardware Protections**:
   * **Heater Thermal Protection**: Shuts down the heater permanently if a MOSFET failure or thermistor detachment is detected (i.e. heater on but temp fails to rise by 2°C within 5 minutes).
   * **Ventilation Purge Cycle**: Spins the fan to 100% for 30s every 10 minutes to expel moisture pocket boundary layers, maintaining maximum drying efficiency.

---

## 🔌 Connection & Wiring Guide

The IRLZ44N pinout, looking at the labeled front face: **Gate (Pin 1) - Drain (Pin 2) - Source (Pin 3)**.

```text
                                           +--------------+
                                           |  Load Power  |
                                           | (+12V or +5V)|
                                           +-------+------+
                                                   |
                                                   +---[ Cathode (striped) ]
                                                   |                         Flyback Diode (1N4007)
                      100 Ohm                      +---[ Anode             ]
  ESP32 GPIO ➔ ➔ ➔ ➔ █▓▒░  █ ➔ ➔ ➔ ➔ +             |
  (20 or 21)       Gate Resistor     |             |
                                     +───────➔ [ Drain (D) ]
                                     |         IRLZ44N MOSFET
                                    [ ] 
                                    [ ] 10k Ohm
                                    [ ] Pull-down
                                     |
  GND ➔ ➔ ➔ ➔ ➔ ➔ ➔ ➔ ➔ ➔ ➔ ➔ ➔ ➔ ➔ ─┴───────➔ [ Source (S) ]
```

### 1. Pin Map
| Component | Pin Function | ESP32-C6 GPIO | Description |
| :--- | :--- | :--- | :--- |
| **I2C SDA** | Sensor Data | **GPIO 22** | Connect to SHT30D/SHT31 SDA |
| **I2C SCL** | Sensor Clock | **GPIO 23** | Connect to SHT30D/SHT31 SCL |
| **DHT Data** | Digital Sensor | **GPIO 14** | Connect to DHT22 Data (if used) |
| **Heater Gate** | PWM Output | **GPIO 20** | Connect to Heater MOSFET Gate (via 100Ω) |
| **Fan Gate** | PWM Output | **GPIO 21** | Connect to Fan MOSFET Gate (via 100Ω) |
| **Status LED** | Onboard LED | **GPIO 15** | Visual flashing state indicators |

### 2. Assembly Rules
* **MOSFET Source**: Connect Source (Pin 3) of both MOSFETs directly to your common DC **Ground**.
* **Flyback Protection**: A **1N4007 diode** is **mandatory** in parallel with the 5V Fan (Cathode to 5V+, Anode to MOSFET Drain) to absorb high voltage induction spikes.
* **Pull-Downs**: Place a **10kΩ resistor** from each MOSFET Gate to GND to prevent the heater/fan from randomly firing during ESP32 boot resets.

---

## 💾 Uploading and Library Setup

### 1. Required Libraries
Install the following libraries using the **Arduino Library Manager**:

* `PubSubClient` (by Nick O'Leary)
* `ArduinoJson` (by Benoit Blanchon)
* `Adafruit SHT31 Library` (by Adafruit, for SHT30D/SHT31 setups)
* `Adafruit Unified Sensor` (dependency)
* `DHT sensor library` (by Adafruit, if using DHT22)

### 2. Compilation and Flashing
1. Open the sketch in **Arduino IDE**.
2. Select **Tools ➔ Board ➔ ESP32C6 ➔ ESP32C6 Dev Module**.
3. Force ESP32-C6 into Download Mode:
   * Press and hold the **BOOT** button on the ESP32-C6-Zero.
   * Press and release the **RESET** button.
   * Release the **BOOT** button.
4. Click **Upload** in the Arduino IDE.

---

## 🧭 Initial Provisioning

1. Power on the device.
2. Search for Wi-Fi networks on your phone or laptop.
3. Connect to **`DRYER-XXXXXX`** using the password **`dryer123`**.
4. Open your browser and navigate to **`http://192.168.4.1`**.
5. Input your local Wi-Fi SSID, Password, and MQTT Broker details. Set your **Dryer Unit Number** (e.g. `1`).
6. Click **Save & Connect**. The ESP32 will reboot, connect to your smart-home network, and auto-discover in Home Assistant!

---

## 📊 MQTT Topic Schema

For Dryer Unit `#1` (dynamically updates based on configured Unit Number):

| Topic | Publish/Subscribe | Payload | Description |
| :--- | :---: | :--- | :--- |
| `garden/dryer1/state` | **Publish (retained)** | JSON string | Telemetry, active targets, states |
| `garden/dryer1/cmd/power` | **Subscribe** | `ON` or `OFF` | System power toggle |
| `garden/dryer1/cmd/filament` | **Subscribe** | Preset name string | e.g., `PLA`, `ABS`, `TPU`, `Manual` |
| `garden/dryer1/cmd/target_temp` | **Subscribe** | Float string (`20`–`75`) | Custom Temperature Override |
| `garden/dryer1/cmd/target_humidity`| **Subscribe** | Float string (`5`–`50`) | Custom Humidity Override |
| `garden/dryer1/cmd/timer` | **Subscribe** | Integer string (`0`–`1440`) | Countdown duration in minutes |

### Telemetry JSON Payload Example:
```json
{
  "temperature": 45.2,
  "humidity": 14.8,
  "is_active": true,
  "filament": "PLA",
  "target_temp": 45.0,
  "target_humidity": 15.0,
  "timer_duration": 240,
  "timer_remaining": 182,
  "heater_power": 42,
  "fan_power": 100,
  "state": "drying"
}
```

---

## 🚦 Status LED Indicators
* **Standby Mode**: Single very short blip every 5 seconds.
* **Active Drying (`STATE_HEATING_UP`)**: Slow breathing pulse (1Hz).
* **Maintaining Mode (`STATE_MAINTAINING`)**: Steady short blip every 2 seconds.
* **Critical Lockout/Fault**: Extremely rapid flashing (5Hz).
