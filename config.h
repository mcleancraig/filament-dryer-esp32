/**
 * @file config.h
 * @brief Centralized configuration parameters for the ESP32-C6-Zero Chamber Controller.
 */

#ifndef CONFIG_H
#define CONFIG_H

// =============================================================================
// 1. SENSOR CONFIGURATION MODE
// =============================================================================
// Uncomment ONLY ONE of the following modes to match your actual hardware setup:

#define SENSOR_MODE_BME280          // [RECOMMENDED] BME280 measures both Temp & Humidity via I2C
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
// 3. TARGET CONTROL PARAMETERS
// =============================================================================
// Modify these to suit your specific container and materials (e.g., filament drying)

#define TARGET_TEMP_HEAT  40.0f  // Target temperature (°C) during dehumidification phase
#define TARGET_TEMP_HOLD  25.0f  // Target temperature (°C) during maintenance phase (drops heat)
#define TARGET_HUMIDITY   20.0f  // Target relative humidity (% RH) to trigger maintenance mode

// =============================================================================
// 4. CONTROL HYSTERESIS & PWM LIMITS
// =============================================================================
#define TEMP_HYSTERESIS    0.5f  // Hysteresis range for temperature control (°C)
#define HUMID_HYSTERESIS   3.0f  // Hysteresis range for humidity control (% RH) to avoid rapid cycling

// PWM speed limits (0 to 255)
#define FAN_SPEED_ACTIVE   255   // Fan duty cycle (100%) during active drying
#define FAN_SPEED_IDLE      64   // Fan duty cycle (~25%) during maintenance to maintain circulation
#define HEATER_MAX_DUTY    255   // Max power allowed on the heater (lower if supply capacity is limited)

// =============================================================================
// 5. SYSTEM SETTINGS
// =============================================================================
#define MEASURE_INTERVAL  2000   // Non-blocking sensor update interval (milliseconds)
#define SERIAL_BAUD     115200   // Serial baud rate for USB debugging

#endif // CONFIG_H
