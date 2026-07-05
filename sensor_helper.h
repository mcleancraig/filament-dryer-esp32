/**
 * @file sensor_helper.h
 * @brief Unified sensor abstraction layer supporting BME280, BMP280+DHT22, and DHT22-only configurations.
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

// Structure to hold clean environmental readings
struct SensorData {
  float temperature; // Temperature in °C
  float humidity;    // Relative Humidity in %
  bool isValid;      // True if readings are healthy and valid
};

// Global sensor instances
#if defined(SENSOR_MODE_BME280)
  Adafruit_BME280 bme; // I2C BME280 sensor
#elif defined(SENSOR_MODE_BMP280_DHT22)
  Adafruit_BMP280 bmp; // I2C BMP280 sensor
  DHT dht(PIN_DHT, DHT22); // Digital DHT22 sensor
#elif defined(SENSOR_MODE_DHT22_ONLY)
  DHT dht(PIN_DHT, DHT22); // Digital DHT22 sensor
#endif

/**
 * @brief Initializes the selected sensor configurations.
 * Sets up I2C on the correct pins and initializes the sensor chips.
 * @return true if initialization succeeded, false otherwise.
 */
bool initSensors() {
  Serial.println("\n--- Initializing Sensors ---");
  bool success = true;

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
  Serial.println("Attempting to connect to BME280 sensor...");
  // 0x76 or 0x77 are standard BME280 I2C addresses
  if (!bme.begin(0x76, &Wire)) {
    // Try alternative address if 0x76 fails
    if (!bme.begin(0x77, &Wire)) {
      Serial.println("[ERROR] Could not find a valid BME280 sensor! Check I2C address (0x76 or 0x77) and wiring.");
      success = false;
    }
  }
  if (success) {
    Serial.println("[SUCCESS] BME280 found and ready.");
  }

#elif defined(SENSOR_MODE_BMP280_DHT22)
  Serial.println("Attempting to connect to BMP280 sensor...");
  if (!bmp.begin(0x76)) {
    if (!bmp.begin(0x77)) {
      Serial.println("[ERROR] Could not find a valid BMP280 sensor! Check I2C address and wiring.");
      success = false;
    }
  }
  if (success) {
    Serial.println("[SUCCESS] BMP280 found.");
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
 * @brief Reads the current temperature and humidity from the active sensors.
 * Performs validation checks to catch sensor disconnected/failure states.
 * @return SensorData structure containing the readings.
 */
SensorData readSensors() {
  SensorData data;
  data.temperature = 0.0f;
  data.humidity = 0.0f;
  data.isValid = false;

#if defined(SENSOR_MODE_BME280)
  float temp = bme.readTemperature();
  float humid = bme.readHumidity();

  // Validate BME280 readings (checks for communication failure)
  if (isnan(temp) || isnan(humid) || temp < -40.0f || temp > 85.0f) {
    Serial.println("[WARNING] BME280 read failed or returned out-of-bounds values!");
  } else {
    data.temperature = temp;
    data.humidity = humid;
    data.isValid = true;
  }

#elif defined(SENSOR_MODE_BMP280_DHT22)
  float temp = bmp.readTemperature();
  float humid = dht.readHumidity();

  // Validate readings
  if (isnan(temp) || isnan(humid) || temp < -40.0f || temp > 85.0f || humid < 0.0f || humid > 100.0f) {
    Serial.println("[WARNING] BMP280/DHT22 read failure!");
  } else {
    data.temperature = temp;
    data.humidity = humid;
    data.isValid = true;
  }

#elif defined(SENSOR_MODE_DHT22_ONLY)
  float temp = dht.readTemperature();
  float humid = dht.readHumidity();

  // Validate DHT22 readings
  if (isnan(temp) || isnan(humid) || temp < -40.0f || temp > 80.0f || humid < 0.0f || humid > 100.0f) {
    Serial.println("[WARNING] DHT22 read failed! Check pull-up resistor and connections.");
  } else {
    data.temperature = temp;
    data.humidity = humid;
    data.isValid = true;
  }
#endif

  return data;
}

#endif // SENSOR_HELPER_H
