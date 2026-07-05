/**
 * @file sensor_helper.h
 * @brief Unified sensor abstraction layer supporting dual BME280/BMP280 setups, or single BME280/BMP280/DHT22 setups.
 */

#ifndef SENSOR_HELPER_H
#define SENSOR_HELPER_H

#include <Wire.h>
#include "config.h"

// Include appropriate libraries based on selected sensor mode
#if defined(SENSOR_MODE_BME280)
  #include <Adafruit_Sensor.h>
  #include <Adafruit_BME280.h>
#elif defined(SENSOR_MODE_BMP280_DHT22)
  #include <Adafruit_Sensor.h>
  #include <Adafruit_BMP280.h>
  #include <DHT.h>
#elif defined(SENSOR_MODE_DHT22_ONLY)
  #include <DHT.h>
#else
  #error "Please select a valid SENSOR_MODE in config.h!"
#endif

// Structure to hold clean environmental readings from both zones
struct SensorData {
  float temperature;       // Chamber Core Ambient Temperature in °C (for regulation)
  float humidity;          // Chamber Core Ambient Relative Humidity in %
  float heaterExhaustTemp; // Heater Exhaust Air Temperature in °C (for safety guard)
  bool isValid;            // True if core chamber readings are healthy
  bool heaterExhaustValid; // True if heater guard readings are healthy
};

// Global sensor instances and state
inline bool hasHeaterExhaustSensor = false;

#if defined(SENSOR_MODE_BME280)
  inline Adafruit_BME280 bmeChamber;
  inline Adafruit_BME280 bmeHeater;
#elif defined(SENSOR_MODE_BMP280_DHT22)
  inline Adafruit_BMP280 bmpChamber;
  inline Adafruit_BMP280 bmpHeater;
  inline DHT dht(PIN_DHT, DHT22);
#elif defined(SENSOR_MODE_DHT22_ONLY)
  inline DHT dht(PIN_DHT, DHT22);
#endif

/**
 * @brief Initializes the selected sensor configurations.
 * Sets up I2C on the correct pins and auto-detects single vs. dual sensors.
 * @return true if initialization succeeded, false otherwise.
 */
inline bool initSensors() {
  Serial.println("\n--- Initializing Sensors ---");
  bool success = true;
  hasHeaterExhaustSensor = false;

  // Initialize I2C interface on ESP32-C6 pins defined in config.h
#if defined(SENSOR_MODE_BME280) || defined(SENSOR_MODE_BMP280_DHT22)
  Serial.print("Initializing I2C (SDA Pin: ");
  Serial.print(PIN_I2C_SDA);
  Serial.print(", SCL Pin: ");
  Serial.print(PIN_I2C_SCL);
  Serial.println(")...");
  
  if (!Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL)) {
    Serial.println("[ERROR] Failed to start Wire (I2C) library!");
    success = false;
  }
#endif

  // Initialize specific sensors based on configuration mode
#if defined(SENSOR_MODE_BME280)
  Serial.println("Attempting to connect to BME280 (Chamber Ambient @ 0x77)...");
  bool chamberFound = bmeChamber.begin(0x77, &Wire);
  if (chamberFound) {
    Serial.println("[SUCCESS] Chamber BME280 found at 0x77.");
    Serial.println("Checking for secondary Heater BME280 (Heater Exhaust @ 0x76)...");
    if (bmeHeater.begin(0x76, &Wire)) {
      Serial.println("[SUCCESS] Heater BME280 found at 0x76. Dual-sensor mode enabled!");
      hasHeaterExhaustSensor = true;
    } else {
      Serial.println("[NOTE] No Heater BME280 found at 0x76. Single-sensor fallback active.");
    }
  } else {
    Serial.println("[WARNING] Could not find Chamber BME280 at 0x77! Trying fallback to 0x76...");
    chamberFound = bmeChamber.begin(0x76, &Wire);
    if (!chamberFound) {
      Serial.println("[ERROR] No BME280 sensor found at all!");
      success = false;
    } else {
      Serial.println("[SUCCESS] Single Chamber BME280 found at 0x76.");
    }
  }

#elif defined(SENSOR_MODE_BMP280_DHT22)
  Serial.println("Attempting to connect to BMP280 (Chamber Ambient @ 0x77)...");
  bool chamberFound = bmpChamber.begin(0x77);
  if (chamberFound) {
    Serial.println("[SUCCESS] Chamber BMP280 found at 0x77.");
    Serial.println("Checking for secondary Heater BMP280 (Heater Exhaust @ 0x76)...");
    if (bmpHeater.begin(0x76)) {
      Serial.println("[SUCCESS] Heater BMP280 found at 0x76. Dual-sensor mode enabled!");
      hasHeaterExhaustSensor = true;
    } else {
      Serial.println("[NOTE] No Heater BMP280 found at 0x76. Single-sensor fallback active.");
    }
  } else {
    Serial.println("[WARNING] Could not find Chamber BMP280 at 0x77! Trying fallback to 0x76...");
    chamberFound = bmpChamber.begin(0x76);
    if (!chamberFound) {
      Serial.println("[ERROR] No BMP280 sensor found at all!");
      success = false;
    } else {
      Serial.println("[SUCCESS] Single Chamber BMP280 found at 0x76.");
    }
  }

  Serial.print("Initializing DHT22 sensor on Pin ");
  Serial.println(PIN_DHT);
  dht.begin();
  Serial.println("[SUCCESS] DHT22 initialized.");

#elif defined(SENSOR_MODE_DHT22_ONLY)
  Serial.print("Initializing DHT22 sensor on Pin ");
  Serial.println(PIN_DHT);
  dht.begin();
  Serial.println("[SUCCESS] DHT22 initialized.");
#endif

  return success;
}

/**
 * @brief Reads current temperatures and humidity from the active sensors.
 * Performs validation checks to catch sensor disconnected/failure states.
 * @return SensorData structure containing the readings.
 */
inline SensorData readSensors() {
  SensorData data;
  data.temperature = 0.0f;
  data.humidity = 0.0f;
  data.heaterExhaustTemp = 0.0f;
  data.isValid = false;
  data.heaterExhaustValid = false;

#if defined(SENSOR_MODE_BME280)
  float tChamber = bmeChamber.readTemperature();
  float hChamber = bmeChamber.readHumidity();

  if (isnan(tChamber) || isnan(hChamber) || tChamber < -40.0f || tChamber > 85.0f) {
    Serial.println("[WARNING] Chamber BME280 read failed or returned out-of-bounds values!");
  } else {
    data.temperature = tChamber;
    data.humidity = hChamber;
    data.isValid = true;
  }

  if (hasHeaterExhaustSensor) {
    float tHeater = bmeHeater.readTemperature();
    if (isnan(tHeater) || tHeater < -40.0f || tHeater > 85.0f) {
      Serial.println("[WARNING] Heater BME280 read failed or returned out-of-bounds!");
    } else {
      data.heaterExhaustTemp = tHeater;
      data.heaterExhaustValid = true;
    }
  } else {
    // Single sensor fallback: duplicate Chamber Ambient measurements
    data.heaterExhaustTemp = data.temperature;
    data.heaterExhaustValid = data.isValid;
  }

#elif defined(SENSOR_MODE_BMP280_DHT22)
  float tChamber = bmpChamber.readTemperature();
  float hChamber = dht.readHumidity();

  if (isnan(tChamber) || isnan(hChamber) || tChamber < -40.0f || tChamber > 85.0f || hChamber < 0.0f || hChamber > 100.0f) {
    Serial.println("[WARNING] BMP280/DHT22 read failure!");
  } else {
    data.temperature = tChamber;
    data.humidity = hChamber;
    data.isValid = true;
  }

  if (hasHeaterExhaustSensor) {
    float tHeater = bmpHeater.readTemperature();
    if (isnan(tHeater) || tHeater < -40.0f || tHeater > 85.0f) {
      Serial.println("[WARNING] Heater BMP280 read failed or returned out-of-bounds!");
    } else {
      data.heaterExhaustTemp = tHeater;
      data.heaterExhaustValid = true;
    }
  } else {
    // Single sensor fallback: duplicate Chamber Ambient measurements
    data.heaterExhaustTemp = data.temperature;
    data.heaterExhaustValid = data.isValid;
  }

#elif defined(SENSOR_MODE_DHT22_ONLY)
  float tChamber = dht.readTemperature();
  float hChamber = dht.readHumidity();

  if (isnan(tChamber) || isnan(hChamber) || tChamber < -40.0f || tChamber > 80.0f || hChamber < 0.0f || hChamber > 100.0f) {
    Serial.println("[WARNING] DHT22 read failed! Check connections.");
  } else {
    data.temperature = tChamber;
    data.humidity = hChamber;
    data.isValid = true;
    
    // Single sensor fallback: duplicate Chamber Ambient measurements
    data.heaterExhaustTemp = tChamber;
    data.heaterExhaustValid = true;
  }
#endif

  return data;
}

#endif // SENSOR_HELPER_H
