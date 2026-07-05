/**
 * @file config.h
 * @brief Centralized configuration parameters for the ESP32-C6-Zero Chamber Controller.
 * Extends the basic config to include NVS configuration parameters and default fallback values.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =============================================================================
// 1. SENSOR CONFIGURATION MODE
// =============================================================================
// Uncomment ONLY ONE of the following modes to match your actual hardware setup:

// #define SENSOR_MODE_BME280          // [RECOMMENDED] BME280 measures both Temp & Humidity via I2C
#define SENSOR_MODE_SHT30           // SHT30/SHT31/SHT35 measures both Temp & Humidity via I2C
// #define SENSOR_MODE_BMP280_DHT22    // BMP280 (I2C) for Temp + DHT22 (Digital) for Humidity
// #define SENSOR_MODE_DHT22_ONLY      // DHT22 measures both Temp & Humidity (Digital)

// =============================================================================
// 2. PIN ASSIGNMENTS
// =============================================================================
// Safe ESP32-C6-Zero pins that do not interfere with bootstrapping or USB.

#define PIN_I2C_SDA       22  // Standard I2C Data pin
#define PIN_I2C_SCL       23  // Standard I2C Clock pin
#define PIN_DHT           14  // Single-wire digital data pin for DHT22 (if used)

#define PIN_HEATER_GATE   20  // PWM Output: Gates the MOSFET for the 12V Heating Pad
#define PIN_FAN_GATE      21  // PWM Output: Gates the MOSFET for the 5V Fan

#define PIN_STATUS_LED    15  // Onboard status indicator LED (usually blue/green)

// =============================================================================
// 3. CAPTIVE PORTAL PROVISIONING SETTINGS
// =============================================================================
#define AP_PASSWORD       "dryer123" // Password to connect to local DRYER-XXXXXX access point
#define AP_TIMEOUT_MIN    5          // Captive portal times out and sleeps after 5 minutes if unconfigured
#define NVS_NAMESPACE     "dryer-cfg" // Preferences namespace for NVS

// =============================================================================
// 4. CONTROL HYSTERESIS & PWM LIMITS
// =============================================================================
#define TEMP_HYSTERESIS    0.5f  // Hysteresis range for temperature control (°C)
#define HUMID_HYSTERESIS   3.0f  // Hysteresis range for humidity control (% RH) to avoid rapid cycling
#define MAX_HEATER_EXHAUST_TEMP 70.0f // Max allowed heater exhaust temperature (°C) before safety override limits power
#define TARGET_TEMP_HOLD   25.0f // Default fallback holding temperature (°C) during maintenance phase


// PWM speed limits (0 to 255)
#define FAN_SPEED_ACTIVE   255   // Fan duty cycle (100%) during active drying
#define FAN_SPEED_IDLE      64   // Fan duty cycle (~25%) during maintenance to maintain circulation
#define HEATER_MAX_DUTY    255   // Max power allowed on the heater (lower if supply capacity is limited)

// =============================================================================
// 5. SYSTEM SETTINGS
// =============================================================================
#define MEASURE_INTERVAL  2000   // Non-blocking sensor update interval (milliseconds)
#define TELEMETRY_INTERVAL 5000  // Non-blocking MQTT publish interval (milliseconds)
#define SERIAL_BAUD     115200   // Serial baud rate for USB debugging

// Device configuration struct stored in NVS Preferences
struct DeviceConfig {
  int     unitNumber;
  char    wifiSSID[64];
  char    wifiPassword[64];
  char    mqttBroker[64];
  int     mqttPort;
  char    mqttUser[32];
  char    mqttPassword[64];
};

#endif // CONFIG_H
