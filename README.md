# ESP32-C6-Zero Chamber Temp & Humidity Controller

This firmware controls a **12V Heating Pad** and a **5V Fan** using a **Waveshare ESP32-C6-Zero** and **IRLZ44N Logic-Level N-Channel MOSFETs** to dry and maintain a target humidity level inside a sealed chamber.

---

## 🛠️ Hardware Requirements & Recommended Setup

1. **Microcontroller**: [Waveshare ESP32-C6-Zero](https://www.waveshare.com/esp32-c6-zero.htm)
2. **Loads**:
   * 12V DC Heating Pad (Resistive)
   * 5V DC Fan (Inductive)
3. **Power Supply**:
   * 12V DC power supply (capable of supplying the heater's current, e.g., 2A–3A).
   * 12V-to-5V step-down buck converter (to power the 5V fan and the ESP32).
4. **MOSFET Switches** (Qty: 2):
   * [IRLZ44N N-channel Logic-Level MOSFETs](https://www.infineon.com/dgdl/Infineon-IRLZ44N-DataSheet-v01_01-EN.pdf?fileId=5546d462533600a4015356ec236b281c). *Must be the IRL series (logic-level), not IRF series, as IRF gates require 10V to fully open, whereas IRL opens fully at 3V–5V.*
5. **Passive Components**:
   * **100Ω Resistors** (Qty: 2): Gate current limiters (protects ESP32 GPIOs).
   * **10kΩ Resistors** (Qty: 2): Gate pull-downs (keeps gates closed during boot/reset).
   * **1N4007 Diodes** (Qty: 2): Flyback protection (crucial for the fan, highly recommended for the heater).
6. **Sensor (Select One)**:
   * **BME280** [Highly Recommended]: Measures both temp and humidity via I2C.
   * **BMP280 + DHT22**: Use BMP280 for temp (I2C) and DHT22 for humidity (Digital pin).
   * **DHT22 only**: Measures both temp and humidity via a single digital pin.

---

## 🔌 Connection & Wiring Guide

### 1. Sensor Connection

* **I2C Mode (BME280 or BMP280)**:
  * **VCC** (Sensor) ➔ **3V3** (ESP32)
  * **GND** (Sensor) ➔ **GND** (ESP32)
  * **SDA** (Sensor) ➔ **GPIO 22** (ESP32)
  * **SCL** (Sensor) ➔ **GPIO 23** (ESP32)
  * *Note: Ensure your breakout board has built-in pull-up resistors on SDA/SCL. If not, add 4.7kΩ resistors from SDA and SCL to 3V3.*

* **DHT22 (if used)**:
  * **VCC** (DHT22) ➔ **3V3** (ESP32)
  * **GND** (DHT22) ➔ **GND** (ESP32)
  * **DATA** (DHT22) ➔ **GPIO 14** (ESP32)
  * *Note: Place a 10kΩ pull-up resistor from the DATA line to VCC (3V3) if your module doesn't have one.*

### 2. MOSFET Gate Drive Circuit (For BOTH Heater and Fan)
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

* **Heater MOSFET**:
  * **Gate (G)** ➔ **GPIO 20** (via 100Ω gate resistor).
  * **Drain (D)** ➔ **Negative (-) terminal** of the 12V Heating Pad.
  * **Source (S)** ➔ **GND**.
  * **Flyback Diode (1N4007)**: Cathode (stripe) to 12V+, Anode to MOSFET Drain.
  * **Heating Pad Positive (+)** ➔ **12V DC Positive (+)**.

* **Fan MOSFET**:
  * **Gate (G)** ➔ **GPIO 21** (via 100Ω gate resistor).
  * **Drain (D)** ➔ **Negative (-) terminal** of the 5V Fan.
  * **Source (S)** ➔ **GND**.
  * **Flyback Diode (1N4007)**: Cathode (stripe) to 5V+, Anode to MOSFET Drain.
  * **Fan Positive (+)** ➔ **5V DC Positive (+)** (from the 5V buck converter).

---

## 💾 Uploading and Library Setup

### 1. Required Libraries
Install the following libraries using the **Arduino Library Manager** (Sketch ➔ Include Library ➔ Manage Libraries):

* **For BME280/BMP280**:
  * `Adafruit BME280 Library` (by Adafruit)
  * `Adafruit BMP280 Library` (by Adafruit)
  * `Adafruit Unified Sensor` (dependency, automatically prompted)
* **For DHT22**:
  * `DHT sensor library` (by Adafruit)

### 2. Configuration
Open `config.h` and edit your settings:
* Uncomment your sensor choice under Section 1 (e.g., `#define SENSOR_MODE_BME280`).
* Set your target temperature and humidity thresholds (Sections 3 and 4).

### 3. Flash Instructions
1. Add the ESP32 board URL to Arduino IDE (File ➔ Preferences ➔ Additional Boards Manager URLs):
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
2. Open **Boards Manager**, search for `esp32` by Espressif, and install version **3.x.x** (or latest).
3. Select your board: **Tools ➔ Board ➔ ESP32C6 ➔ ESP32C6 Dev Module**.
4. Set upload speed: **115200** or **921600** (depends on adapter).
5. **Enter Download Mode on ESP32-C6-Zero**:
   * Hold down the **BOOT** button.
   * Press and release the **RESET** button.
   * Release the **BOOT** button.
6. Click **Upload** in the Arduino IDE.

---

## 📈 System Operation & Telemetry

Once booted, open the **Serial Monitor** at **115200 baud** to see real-time logs:

```text
==============================================
  ESP32-C6 Chamber Temp & Humidity Controller
==============================================

--- Initializing Sensors ---
Initializing I2C (SDA Pin: 22, SCL Pin: 23)...
Attempting to connect to BME280 sensor...
[SUCCESS] BME280 found and ready.
[SYSTEM] Hardware successfully initialized.

Telemetry -> Temp: 24.3 °C | Humid: 64.2 % RH | State: ACTIVE HEATING / DEHUMIDIFYING
Control   -> Heater PWM: 255 (100%) | Fan PWM: 255 (100%)

Telemetry -> Temp: 38.2 °C | Humid: 42.1 % RH | State: ACTIVE HEATING / DEHUMIDIFYING
Control   -> Heater PWM: 153 (60%) | Fan PWM: 255 (100%)

Telemetry -> Temp: 40.1 °C | Humid: 19.8 % RH | State: MAINTENANCE / HOLDING
Control   -> Heater PWM: 0 (0%) | Fan PWM: 64 (25%)
```

### 🚦 Visual LED Status Indicators
* **Rapid Flash (5Hz)**: Critical startup sensor initialization failure.
* **Moderate Flash (2.5Hz)**: Runtime sensor read failure (wires loose, sensor disconnected).
* **Slow Pulse (1Hz)**: Active Dehumidifying / Heating Phase (`STATE_HEATING_UP`).
* **Short periodic blink (every 3s)**: Maintenance Mode (`STATE_MAINTAINING`), system is healthy.
