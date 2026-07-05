/**
 * @file chamber_controller.ino
 * @brief Main firmware for ESP32-C6-Zero Chamber Temp & Humidity Controller.
 * Implements a non-blocking state machine, proportional heater regulation,
 * fan speed control, and safety fault-handling.
 */

#include "config.h"
#include "sensor_helper.h"

// System state enumeration
enum SystemState {
  STATE_HEATING_UP,  // Phase 1: Active dehumidifying/heating
  STATE_MAINTAINING  // Phase 2: Humidity target reached, maintaining idle levels
};

// Global variables
SystemState currentState = STATE_HEATING_UP;
unsigned long lastMeasureTime = 0;
bool sensorInitOk = false;

// PWM state holders
int currentHeaterPWM = 0;
int currentFanPWM = 0;

// LED pattern timings
unsigned long lastLedToggle = 0;
bool ledState = false;

/**
 * @brief Set up GPIOs, serial debugging, and sensor initializations.
 */
void setup() {
  // Initialize serial communication
  Serial.begin(SERIAL_BAUD);
  delay(1500); // Wait for USB CDC terminal to connect on ESP32-C6
  
  Serial.println("\n==============================================");
  Serial.println("  ESP32-C6 Chamber Temp & Humidity Controller");
  Serial.println("==============================================\n");

  // Configure output pins
  pinMode(PIN_HEATER_GATE, OUTPUT);
  pinMode(PIN_FAN_GATE, OUTPUT);
  pinMode(PIN_STATUS_LED, OUTPUT);

  // Initialize outputs to safe, OFF states
  analogWrite(PIN_HEATER_GATE, 0);
  analogWrite(PIN_FAN_GATE, 0);
  digitalWrite(PIN_STATUS_LED, LOW);

  // Initialize sensors
  sensorInitOk = initSensors();
  if (!sensorInitOk) {
    Serial.println("[CRITICAL] Sensor initialization failed! System is running in FAULT mode.");
  } else {
    Serial.println("[SYSTEM] Hardware successfully initialized.\n");
  }
}

/**
 * @brief Main execution loop.
 */
void loop() {
  unsigned long now = millis();

  // If sensors failed to initialize on startup, force safe fault state
  if (!sensorInitOk) {
    handleSafetyFault("Sensor initialization failed!");
    blinkLED(now, 100); // Fast 5Hz error flash
    return;
  }

  // Non-blocking environmental measurement & control update
  if (now - lastMeasureTime >= MEASURE_INTERVAL) {
    lastMeasureTime = now;
    
    // 1. Read environmental data
    SensorData env = readSensors();
    
    // 2. Validate sensor reading safety
    if (!env.isValid) {
      handleSafetyFault("Sensor reading became invalid!");
      currentState = STATE_HEATING_UP; // Reset to default state on next recovery
      return;
    }

    // 3. Process State Machine and PID-like Heating logic
    updateControlLogic(env);
  }

  // 4. Update the visual Status LED indicator based on current state
  updateStatusLED(now);
}

/**
 * @brief Computes proportional heater PWM and manages state transitions.
 * @param env Clean sensor data struct with temperature and humidity.
 */
void updateControlLogic(const SensorData &env) {
  float targetTemp = 0.0f;
  
  // Display Current Telemetry
  Serial.print("Telemetry -> Temp: ");
  Serial.print(env.temperature, 1);
  Serial.print(" °C | Humid: ");
  Serial.print(env.humidity, 1);
  Serial.print(" % RH | State: ");

  // Evaluate transitions and state configurations
  switch (currentState) {
    case STATE_HEATING_UP:
      Serial.println("ACTIVE HEATING / DEHUMIDIFYING");
      targetTemp = TARGET_TEMP_HEAT;
      currentFanPWM = FAN_SPEED_ACTIVE;

      // Transition to MAINTAINING when humidity drops below target
      if (env.humidity <= TARGET_HUMIDITY) {
        Serial.println("\n>>> Target humidity reached! Transitioning to MAINTENANCE mode. <<<\n");
        currentState = STATE_MAINTAINING;
      }
      break;

    case STATE_MAINTAINING:
      Serial.println("MAINTENANCE / HOLDING");
      targetTemp = TARGET_TEMP_HOLD;
      currentFanPWM = FAN_SPEED_IDLE;

      // Transition back to HEATING if humidity climbs above target + hysteresis
      if (env.humidity > (TARGET_HUMIDITY + HUMID_HYSTERESIS)) {
        Serial.println("\n>>> Humidity rose above threshold. Transitioning to ACTIVE HEATING. <<<\n");
        currentState = STATE_HEATING_UP;
      }
      break;
  }

  // Proportional (P) Heating Control to prevent temperature overshoot.
  // Full power when temp is > 3°C below target. Drops linearly as it approaches target.
  float tempError = targetTemp - env.temperature;
  
  if (tempError <= 0.0f) {
    // We are at or above target temperature
    currentHeaterPWM = 0;
  } else if (tempError >= 3.0f) {
    // We are far below target temperature, run heater at max
    currentHeaterPWM = HEATER_MAX_DUTY;
  } else {
    // Proportional band (within 3°C of target), scale PWM duty smoothly
    currentHeaterPWM = (int)((tempError / 3.0f) * HEATER_MAX_DUTY);
    if (currentHeaterPWM < 0) currentHeaterPWM = 0;
    if (currentHeaterPWM > HEATER_MAX_DUTY) currentHeaterPWM = HEATER_MAX_DUTY;
  }

  // 4. Set Hardware Outputs via PWM
  analogWrite(PIN_HEATER_GATE, currentHeaterPWM);
  analogWrite(PIN_FAN_GATE, currentFanPWM);

  // Print Control Actions
  Serial.print("Control   -> Heater PWM: ");
  Serial.print(currentHeaterPWM);
  Serial.print(" (");
  Serial.print((currentHeaterPWM / 255.0f) * 100.0f, 0);
  Serial.print("%) | Fan PWM: ");
  Serial.print(currentFanPWM);
  Serial.print(" (");
  Serial.print((currentFanPWM / 255.0f) * 100.0f, 0);
  Serial.println("%)\n");
}

/**
 * @brief Safety fallback function to immediately power down heating and alarm.
 */
void handleSafetyFault(const char* reason) {
  analogWrite(PIN_HEATER_GATE, 0); // SHUT OFF HEATER IMMEDIATELY
  analogWrite(PIN_FAN_GATE, 255);  // Run fan at 100% to purge heat / circulate
  
  static unsigned long lastFaultLog = 0;
  if (millis() - lastFaultLog >= 5000) {
    lastFaultLog = millis();
    Serial.print("[CRITICAL FAULT] Heater disabled! Reason: ");
    Serial.println(reason);
  }
}

/**
 * @brief Non-blocking toggle for the status LED.
 */
void blinkLED(unsigned long now, unsigned long interval) {
  if (now - lastLedToggle >= interval) {
    lastLedToggle = now;
    ledState = !ledState;
    digitalWrite(PIN_STATUS_LED, ledState ? HIGH : LOW);
  }
}

/**
 * @brief Sets different visual LED blink rates based on current system state.
 */
void updateStatusLED(unsigned long now) {
  if (!sensorInitOk) {
    blinkLED(now, 100); // 5Hz rapid flash: Sensor Init Failure
  } else {
    // Check if we are currently in a runtime reading fault
    SensorData check = readSensors();
    if (!check.isValid) {
      blinkLED(now, 200); // 2.5Hz flash: Runtime Sensor Read Fault
    } else {
      // Normal operating modes
      if (currentState == STATE_HEATING_UP) {
        blinkLED(now, 500); // 1Hz slow flash: Active Dehumidification / Heating
      } else {
        // STATE_MAINTAINING: Brief periodic pulse every 3 seconds to indicate idle/healthy
        unsigned long cycleTime = now % 3000;
        if (cycleTime < 100) {
          digitalWrite(PIN_STATUS_LED, HIGH);
        } else {
          digitalWrite(PIN_STATUS_LED, LOW);
        }
      }
    }
  }
}
