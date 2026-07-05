/**
 * @file chamber_controller.ino
 * @brief Smart IoT Filament Dryer firmware for ESP32-C6-Zero.
 * Implements a web setup captive portal, non-blocking WiFi/MQTT connection state machine,
 * material library presets, manual overrides, safety thermal runaway protection,
 * periodic ventilation purge, and Home Assistant auto-discovery.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h> // Ensure "ArduinoJson" is installed in Arduino IDE
#include "config.h"
#include "sensor_helper.h"

#define FIRMWARE_VERSION "0.0.1-b01"


// =============================================================================
// GLOBAL INSTANCES & CONFIGURATION
// =============================================================================
Preferences preferences;
DeviceConfig devCfg;
bool isProvisioned = false;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
WebServer webServer(80);
DNSServer dnsServer;

// Topic schemas (derived dynamically after provisioning)
char STATE_TOPIC[128];
char CMD_POWER_TOPIC[128];
char CMD_FILAMENT_TOPIC[128];
char CMD_TEMP_TOPIC[128];
char CMD_HUMID_TOPIC[128];
char CMD_TIMER_TOPIC[128];

// =============================================================================
// DRYING PARAMETERS & STATE MACHINE
// =============================================================================
enum SystemState {
  STATE_HEATING_UP,  // Phase 1: Active drying (heater running at ideal temp, fan at 100%)
  STATE_MAINTAINING, // Phase 2: Humidity target achieved, holding state (low heat/fan)
  STATE_OFF          // System is idle/powered off
};

SystemState currentDryingState = STATE_OFF;

// Material profiles struct
struct FilamentProfile {
  const char* name;
  float dryingTemp;
  int defaultTimer; // in minutes (0 = continuous)
};

// Material library
const FilamentProfile materialLibrary[] = {
  { "Manual", 40.0f, 0 },   // Option 0: Manual controls
  { "PLA",    45.0f, 240 }, // Option 1: PLA: 45°C, 4 hours
  { "PETG",   55.0f, 240 }, // Option 2: PETG: 55°C, 4 hours
  { "CoPE",   55.0f, 240 }, // Option 3: CoPE: 55°C, 4 hours
  { "ASA",    65.0f, 360 }, // Option 4: ASA: 65°C, 6 hours
  { "ABS",    65.0f, 360 }, // Option 5: ABS: 65°C, 6 hours
  { "TPU",    50.0f, 240 }  // Option 6: TPU: 50°C, 4 hours
};
const int materialCount = sizeof(materialLibrary) / sizeof(materialLibrary[0]);

// Active values
int activeMaterialIndex = 1;     // Default to PLA preset
float activeTargetTemp = 45.0f;   // Managed dynamically based on preset
float activeTargetHumid = 15.0f;  // Target humidity limit (default 15% RH)
int timerDurationMin = 240;       // Remaining duration in minutes
unsigned long lastTimerTick = 0;  // For countdown decrementing
bool isSystemActive = false;      // True if dryer is ON, false if OFF

// Safety failsafe states
bool isSafetyLockout = false;
const char* lockoutReason = "";

// For Proportional (P) controller
int currentHeaterPWM = 0;
int currentFanPWM = 0;

// Non-blocking intervals
unsigned long lastMeasureTime = 0;
unsigned long lastTelemetryTime = 0;

// Safety checking variables
unsigned long heaterStartTime = 0;
float tempAtHeaterStart = 0.0f;
bool safetyTimerArmed = false;

// Periodic purge variables
unsigned long lastPurgeCycleTime = 0;
bool isPurging = false;
unsigned long purgeStartTime = 0;

// LED pattern timing
unsigned long lastLedToggle = 0;
bool ledState = false;

// =============================================================================
// CAPTIVE CONFIGURATION PORTAL HTML
// =============================================================================
const char CONFIG_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Filament Dryer Setup</title>
<style>
  body{font-family:sans-serif;max-width:460px;margin:40px auto;padding:0 16px;background:#f5f5f5}
  h1{font-size:1.3em;color:#e65c00;margin-bottom:4px}
  p.sub{color:#666;font-size:.85em;margin-top:0}
  label{display:block;margin-top:14px;font-size:.9em;color:#333;font-weight:600}
  input,select{width:100%;padding:8px;margin-top:4px;border:1px solid #ccc;
    border-radius:6px;font-size:1em;box-sizing:border-box}
  .optional{color:#888;font-weight:400;font-size:.8em}
  .section{background:#fff;border-radius:10px;padding:16px;margin:16px 0;
    box-shadow:0 1px 4px rgba(0,0,0,.08)}
  button{width:100%;padding:12px;background:#e65c00;color:#fff;border:none;
    border-radius:8px;font-size:1em;cursor:pointer;margin-top:20px}
  button:hover{background:#cc5200}
  .hint{font-size:.78em;color:#888;margin-top:2px}
  .pw-wrap{position:relative;margin-top:4px}
  .pw-wrap input{margin-top:0;padding-right:38px}
  .pw-toggle{position:absolute;right:6px;top:50%;transform:translateY(-50%);
    width:auto;padding:4px;background:none;border:none;margin:0;
    color:#888;cursor:pointer;display:flex;align-items:center;line-height:1}
  .pw-toggle:hover{background:none;color:#333}
</style>
</head>
<body>
<h1>🔥 Filament Dryer Setup</h1>
<p class="sub">Configure this dryer then click Save. It will restart and begin controlling and reporting.</p>

<form method="POST" action="/save">

  <div class="section">
    <label>Dryer unit number
      <input type="number" name="unit" id="unit" min="1" max="99" value="1" required>
    </label>
    <p class="hint">Sets the unique dryer ID and MQTT topic name segment</p>
  </div>

  <div class="section">
    <label>WiFi SSID
      <input type="text" name="ssid" placeholder="Your network name" required>
    </label>
    <label>WiFi password <span class="optional">(optional)</span>
      <div class="pw-wrap">
        <input type="password" name="pass" placeholder="Leave blank for open networks">
        <button type="button" class="pw-toggle" onclick="togglePw(this)" aria-label="Show password"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/></svg></button>
      </div>
    </label>
  </div>

  <div class="section">
    <label>MQTT broker address
      <input type="text" name="broker" placeholder="192.168.1.100" required>
    </label>
    <label>MQTT port
      <input type="number" name="port" value="1883" required>
    </label>
    <label>MQTT username <span class="optional">(optional)</span>
      <input type="text" name="muser" placeholder="Leave blank if not required">
    </label>
    <label>MQTT password <span class="optional">(optional)</span>
      <div class="pw-wrap">
        <input type="password" name="mpass" placeholder="Leave blank if not required">
        <button type="button" class="pw-toggle" onclick="togglePw(this)" aria-label="Show password"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/></svg></button>
      </div>
    </label>
  </div>

  <div id="err" style="display:none;background:#fde8e8;border:1px solid #c0392b;
    border-radius:8px;padding:12px 16px;margin-top:12px;color:#c0392b;
    font-size:.9em;font-weight:600"></div>

  <button type="submit">Save &amp; Restart</button>
</form>

<script>
var EYE     = '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/></svg>';
var EYE_OFF = '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24"/><line x1="1" y1="1" x2="23" y2="23"/></svg>';
function togglePw(btn) {
  var inp = btn.previousElementSibling;
  var show = inp.type === 'password';
  inp.type = show ? 'text' : 'password';
  btn.innerHTML = show ? EYE_OFF : EYE;
  btn.setAttribute('aria-label', show ? 'Hide password' : 'Show password');
}
function isValidIP(s) {
  var dots = (s.match(/\./g)||[]).length;
  if (dots !== 3) return false;
  var p = s.split('.');
  if (p.length !== 4) return false;
  return p.every(function(o) {
    return /^\d+$/.test(o) && parseInt(o,10) >= 0 && parseInt(o,10) <= 255;
  });
}
function isValidHost(s) {
  if (!s || !s.length) return false;
  if (s.indexOf('.') === -1) return false;
  if (!/^[A-Za-z0-9.\-]+$/.test(s)) return false;
  if (s[0]==='.' || s[0]==='-' || s[s.length-1]==='.' || s[s.length-1]==='-') return false;
  if (/^[\d.]+$/.test(s)) return isValidIP(s);
  return true;
}
function v(name) {
  var el = document.querySelector('[name="'+name+'"]');
  return el ? el.value.trim() : '';
}
function fail(e, msg) {
  var el = document.getElementById('err');
  el.textContent = msg;
  el.style.display = 'block';
  el.scrollIntoView({behavior:'smooth', block:'center'});
  e.preventDefault();
}
function validateForm(e) {
  document.getElementById('err').style.display = 'none';
  var n = parseInt(v('unit'),10);
  if (isNaN(n)||n<1||n>99)
    return fail(e, 'Dryer unit number must be between 1 and 99.');
  if (!v('ssid'))
    return fail(e, 'WiFi SSID is required.');
  if (v('ssid').length>63)
    return fail(e, 'WiFi SSID is too long (max 63 characters).');
  if (v('pass').length>63)
    return fail(e, 'WiFi password is too long (max 63 characters).');
  var broker = v('broker');
  if (!broker)
    return fail(e, 'MQTT broker address is required.');
  if (!isValidHost(broker))
    return fail(e, 'MQTT broker must be a valid IP address or fully-qualified hostname (e.g. 192.168.1.1 or mqtt.local).');
  var mp = parseInt(v('port'),10);
  if (isNaN(mp)||mp<1||mp>65535)
    return fail(e, 'MQTT port must be between 1 and 65535.');
  if (v('muser').length>31)
    return fail(e, 'MQTT username is too long (max 31 characters).');
  if (v('mpass').length>63)
    return fail(e, 'MQTT password is too long (max 63 characters).');
}
document.querySelector('form').addEventListener('submit', validateForm);
</script>
</body>
</html>
)rawhtml";

const char SAVED_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Saved</title>
<style>
  body{font-family:sans-serif;max-width:420px;margin:80px auto;padding:0 16px;
    text-align:center;background:#f5f5f5}
  h1{color:#e65c00}p{color:#555}
</style>
</head>
<body>
<h1>🔥 Saved!</h1>
<p>Configuration saved. The dryer will restart and connect to your network.</p>
<p><small>You can close this page.</small></p>
</body>
</html>
)rawhtml";

// =============================================================================
// NVS SAVING & LOADING
// =============================================================================
const char* NVS_MAGIC_KEY   = "magic";
const char* NVS_MAGIC_VALUE = "dryer-1";

void loadConfiguration() {
  preferences.begin(NVS_NAMESPACE, true);
  String magic = preferences.getString(NVS_MAGIC_KEY, "");
  preferences.end();

  if (magic.length() > 0 && magic != NVS_MAGIC_VALUE) {
    Serial.printf("Config    — NVS magic mismatch ('%s'), clearing NVS...\n", magic.c_str());
    preferences.begin(NVS_NAMESPACE, false);
    preferences.clear();
    preferences.end();
    isProvisioned = false;
    return;
  }

  preferences.begin(NVS_NAMESPACE, true);
  isProvisioned = preferences.getBool("provisioned", false);
  
  if (isProvisioned) {
    devCfg.unitNumber = preferences.getInt("unitNumber", 1);
    preferences.getString("wifiSSID", devCfg.wifiSSID, sizeof(devCfg.wifiSSID));
    preferences.getString("wifiPassword", devCfg.wifiPassword, sizeof(devCfg.wifiPassword));
    preferences.getString("mqttBroker", devCfg.mqttBroker, sizeof(devCfg.mqttBroker));
    devCfg.mqttPort = preferences.getInt("mqttPort", 1883);
    preferences.getString("mqttUser", devCfg.mqttUser, sizeof(devCfg.mqttUser));
    preferences.getString("mqttPassword", devCfg.mqttPassword, sizeof(devCfg.mqttPassword));
    
    // Load last operational overrides if any
    activeMaterialIndex = preferences.getInt("lastPreset", 1);
    activeTargetTemp = preferences.getFloat("lastTemp", 45.0f);
    activeTargetHumid = preferences.getFloat("lastHumid", 15.0f);
    timerDurationMin = preferences.getInt("lastTimer", 240);
  }
  preferences.end();

  // Passive migration: write magic key so future boots pass strict check
  if (isProvisioned && magic.length() == 0) {
    Serial.println("Config    — NVS magic absent, writing (passive migration)...");
    preferences.begin(NVS_NAMESPACE, false);
    preferences.putString(NVS_MAGIC_KEY, NVS_MAGIC_VALUE);
    preferences.end();
  }
}

void saveConfiguration(const DeviceConfig &newCfg) {
  preferences.begin(NVS_NAMESPACE, false);
  preferences.putString(NVS_MAGIC_KEY, NVS_MAGIC_VALUE);
  preferences.putInt("unitNumber", newCfg.unitNumber);
  preferences.putString("wifiSSID", newCfg.wifiSSID);
  preferences.putString("wifiPassword", newCfg.wifiPassword);
  preferences.putString("mqttBroker", newCfg.mqttBroker);
  preferences.putInt("mqttPort", newCfg.mqttPort);
  preferences.putString("mqttUser", newCfg.mqttUser);
  preferences.putString("mqttPassword", newCfg.mqttPassword);
  preferences.putBool("provisioned", true);
  preferences.end();
}

void saveOperationalState() {
  preferences.begin(NVS_NAMESPACE, false);
  preferences.putInt("lastPreset", activeMaterialIndex);
  preferences.putFloat("lastTemp", activeTargetTemp);
  preferences.putFloat("lastHumid", activeTargetHumid);
  preferences.putInt("lastTimer", timerDurationMin);
  preferences.end();
}

// =============================================================================
// CAPTIVE CONFIGURATION PORTAL WEB HANDLERS
// =============================================================================
void handlePortalRoot() {
  webServer.send(200, "text/html", CONFIG_HTML);
}

void handlePortalSave() {
  DeviceConfig newCfg;
  newCfg.unitNumber = webServer.arg("unit").toInt();
  if (newCfg.unitNumber <= 0) newCfg.unitNumber = 1;

  strlcpy(newCfg.wifiSSID, webServer.arg("ssid").c_str(), sizeof(newCfg.wifiSSID));
  strlcpy(newCfg.wifiPassword, webServer.arg("pass").c_str(), sizeof(newCfg.wifiPassword));
  strlcpy(newCfg.mqttBroker, webServer.arg("broker").c_str(), sizeof(newCfg.mqttBroker));
  
  newCfg.mqttPort = webServer.arg("port").toInt();
  if (newCfg.mqttPort == 0) newCfg.mqttPort = 1883;
  
  strlcpy(newCfg.mqttUser, webServer.arg("muser").c_str(), sizeof(newCfg.mqttUser));
  strlcpy(newCfg.mqttPassword, webServer.arg("mpass").c_str(), sizeof(newCfg.mqttPassword));

  saveConfiguration(newCfg);

  webServer.send(200, "text/html", SAVED_HTML);
  delay(1500);
  ESP.restart();
}

void handlePortalNotFound() {
  String redirectUrl = "http://" + WiFi.softAPIP().toString() + "/";
  webServer.sendHeader("Location", redirectUrl, true);
  webServer.send(302, "text/plain", "");
}

void startCaptivePortal() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char apSSID[32];
  // Format SSID as requested: DRYER-XXXXXX
  snprintf(apSSID, sizeof(apSSID), "DRYER-%02X%02X%02X", mac[3], mac[4], mac[5]);

  Serial.println("\n==============================================");
  Serial.print("Starting Provisioning Hotspot: ");
  Serial.println(apSSID);
  Serial.print("Password: ");
  Serial.println(AP_PASSWORD);
  Serial.println("Access IP: http://192.168.4.1/");
  Serial.println("==============================================\n");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID, AP_PASSWORD);
  delay(100);

  dnsServer.start(53, "*", WiFi.softAPIP());

  webServer.on("/", HTTP_GET, handlePortalRoot);
  webServer.on("/save", HTTP_POST, handlePortalSave);
  webServer.onNotFound(handlePortalNotFound);
  webServer.begin();

  unsigned long timeout = millis() + ((unsigned long)AP_TIMEOUT_MIN * 60 * 1000UL);

  while (millis() < timeout) {
    dnsServer.processNextRequest();
    webServer.handleClient();
    
    // Safety check on heater in case portal is opened while heater was on
    analogWrite(PIN_HEATER_GATE, 0);
    analogWrite(PIN_FAN_GATE, 0);

    // AP Fast LED indicator (2Hz)
    unsigned long now = millis();
    if (now - lastLedToggle >= 250) {
      lastLedToggle = now;
      ledState = !ledState;
      digitalWrite(PIN_STATUS_LED, ledState ? HIGH : LOW);
    }
    delay(10);
  }

  // AP Timeout
  Serial.println("Provisioning portal timed out! Rebooting...");
  ESP.restart();
}

// =============================================================================
// HOME ASSISTANT MQTT AUTO-DISCOVERY
// =============================================================================
void publishMqttDiscovery() {
  Serial.println("[MQTT] Publishing Home Assistant Auto-Discovery configurations...");
  
  char discTopic[128];
  char payload[512];
  char devUid[32];
  snprintf(devUid, sizeof(devUid), "dryer_%d", devCfg.unitNumber);

  // Common Device Definition
  char devInfo[320];
  snprintf(devInfo, sizeof(devInfo), 
    "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"Filament Dryer %d\",\"model\":\"ESP32-C6 Chamber Controller\",\"manufacturer\":\"Waveshare\",\"sw_version\":\"%s\"}",
    devUid, devCfg.unitNumber, FIRMWARE_VERSION);


  // 1. Temperature Sensor Discovery
  snprintf(discTopic, sizeof(discTopic), "homeassistant/sensor/%s/%s_temp/config", devUid, devUid);
  snprintf(payload, sizeof(payload), 
    "{\"name\":\"Temperature\",\"unique_id\":\"%s_temp\",\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.temperature }}\",\"device_class\":\"temperature\",\"unit_of_measurement\":\"°C\",\"icon\":\"mdi:thermometer\",%s}",
    devUid, STATE_TOPIC, devInfo);
  mqttClient.publish(discTopic, payload, true);

  // 1b. Heater Exhaust Temperature Sensor Discovery
  if (hasHeaterExhaustSensor) {
    snprintf(discTopic, sizeof(discTopic), "homeassistant/sensor/%s/%s_exhaust_temp/config", devUid, devUid);
    snprintf(payload, sizeof(payload), 
      "{\"name\":\"Heater Exhaust Temp\",\"unique_id\":\"%s_exhaust_temp\",\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.heater_exhaust_temp }}\",\"device_class\":\"temperature\",\"unit_of_measurement\":\"°C\",\"icon\":\"mdi:fire\",%s}",
      devUid, STATE_TOPIC, devInfo);
    mqttClient.publish(discTopic, payload, true);
  } else {
    // Clean up entity from HA if secondary sensor is absent
    snprintf(discTopic, sizeof(discTopic), "homeassistant/sensor/%s/%s_exhaust_temp/config", devUid, devUid);
    mqttClient.publish(discTopic, "", true);
  }

  // 1c. Firmware Version Sensor Discovery
  snprintf(discTopic, sizeof(discTopic), "homeassistant/sensor/%s/%s_fw/config", devUid, devUid);
  snprintf(payload, sizeof(payload), 
    "{\"name\":\"Firmware Version\",\"unique_id\":\"%s_fw\",\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.fw_version }}\",\"icon\":\"mdi:chip\",%s}",
    devUid, STATE_TOPIC, devInfo);
  mqttClient.publish(discTopic, payload, true);



  // 2. Humidity Sensor Discovery
  snprintf(discTopic, sizeof(discTopic), "homeassistant/sensor/%s/%s_humid/config", devUid, devUid);
  snprintf(payload, sizeof(payload), 
    "{\"name\":\"Humidity\",\"unique_id\":\"%s_humid\",\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.humidity }}\",\"device_class\":\"humidity\",\"unit_of_measurement\":\"%%\",\"icon\":\"mdi:water\",%s}",
    devUid, STATE_TOPIC, devInfo);
  mqttClient.publish(discTopic, payload, true);

  // 3. System State Sensor Discovery
  snprintf(discTopic, sizeof(discTopic), "homeassistant/sensor/%s/%s_state/config", devUid, devUid);
  snprintf(payload, sizeof(payload), 
    "{\"name\":\"Status\",\"unique_id\":\"%s_state\",\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.state }}\",\"icon\":\"mdi:information-outline\",%s}",
    devUid, STATE_TOPIC, devInfo);
  mqttClient.publish(discTopic, payload, true);

  // 4. Timer Remaining Sensor Discovery
  snprintf(discTopic, sizeof(discTopic), "homeassistant/sensor/%s/%s_timer_rem/config", devUid, devUid);
  snprintf(payload, sizeof(payload), 
    "{\"name\":\"Time Remaining\",\"unique_id\":\"%s_timer_rem\",\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.timer_remaining }}\",\"unit_of_measurement\":\"min\",\"icon\":\"mdi:timer-sand\",%s}",
    devUid, STATE_TOPIC, devInfo);
  mqttClient.publish(discTopic, payload, true);

  // 5. Active Power Switch Discovery
  snprintf(discTopic, sizeof(discTopic), "homeassistant/switch/%s/%s_power/config", devUid, devUid);
  snprintf(payload, sizeof(payload), 
    "{\"name\":\"Power\",\"unique_id\":\"%s_power\",\"command_topic\":\"%s\",\"state_topic\":\"%s\",\"value_template\":\"{{ 'ON' if value_json.is_active else 'OFF' }}\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"icon\":\"mdi:power\",%s}",
    devUid, CMD_POWER_TOPIC, STATE_TOPIC, devInfo);
  mqttClient.publish(discTopic, payload, true);

  // 6. Filament Selection Discovery
  snprintf(discTopic, sizeof(discTopic), "homeassistant/select/%s/%s_filament/config", devUid, devUid);
  snprintf(payload, sizeof(payload), 
    "{\"name\":\"Filament Type\",\"unique_id\":\"%s_filament\",\"command_topic\":\"%s\",\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.filament }}\",\"options\":[\"Manual\",\"PLA\",\"PETG\",\"CoPE\",\"ASA\",\"ABS\",\"TPU\"],\"icon\":\"mdi:printer-3d-nozzle\",%s}",
    devUid, CMD_FILAMENT_TOPIC, STATE_TOPIC, devInfo);
  mqttClient.publish(discTopic, payload, true);

  // 7. Temperature Target Override Number Discovery
  snprintf(discTopic, sizeof(discTopic), "homeassistant/number/%s/%s_target_temp/config", devUid, devUid);
  snprintf(payload, sizeof(payload), 
    "{\"name\":\"Target Temp Override\",\"unique_id\":\"%s_target_temp\",\"command_topic\":\"%s\",\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.target_temp }}\",\"min\":20,\"max\":75,\"step\":1,\"unit_of_measurement\":\"°C\",\"icon\":\"mdi:thermometer-chevron-up\",%s}",
    devUid, CMD_TEMP_TOPIC, STATE_TOPIC, devInfo);
  mqttClient.publish(discTopic, payload, true);

  // 8. Humidity Target Override Number Discovery
  snprintf(discTopic, sizeof(discTopic), "homeassistant/number/%s/%s_target_humid/config", devUid, devUid);
  snprintf(payload, sizeof(payload), 
    "{\"name\":\"Target Humidity Override\",\"unique_id\":\"%s_target_humid\",\"command_topic\":\"%s\",\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.target_humidity }}\",\"min\":5,\"max\":50,\"step\":1,\"unit_of_measurement\":\"%%\",\"icon\":\"mdi:water-percent\",%s}",
    devUid, CMD_HUMID_TOPIC, STATE_TOPIC, devInfo);
  mqttClient.publish(discTopic, payload, true);

  // 9. Timer Number Discovery
  snprintf(discTopic, sizeof(discTopic), "homeassistant/number/%s/%s_timer/config", devUid, devUid);
  snprintf(payload, sizeof(payload), 
    "{\"name\":\"Drying Timer\",\"unique_id\":\"%s_timer\",\"command_topic\":\"%s\",\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.timer_duration }}\",\"min\":0,\"max\":1440,\"step\":15,\"unit_of_measurement\":\"min\",\"icon\":\"mdi:timer-outline\",%s}",
    devUid, CMD_TIMER_TOPIC, STATE_TOPIC, devInfo);
  mqttClient.publish(discTopic, payload, true);

  Serial.println("[MQTT] HA Auto-Discovery configs published successfully.");
}

// =============================================================================
// MQTT INCOMING MESSAGE CALLBACK HANDLER (Subscribed Commands)
// =============================================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Safe parsing buffer
  char val[32] = "";
  if (length < sizeof(val)) {
    memcpy(val, payload, length);
    val[length] = '\0';
  } else {
    return;
  }

  Serial.print("[MQTT RX] ");
  Serial.print(topic);
  Serial.print(" => ");
  Serial.println(val);

  bool operationalChange = false;

  // 1. Power Command Set
  if (strcmp(topic, CMD_POWER_TOPIC) == 0) {
    if (strcmp(val, "ON") == 0) {
      if (!isSystemActive && !isSafetyLockout) {
        isSystemActive = true;
        currentDryingState = STATE_HEATING_UP;
        lastTimerTick = millis();
        
        // Arm the thermal runaway protection timer
        heaterStartTime = millis();
        safetyTimerArmed = true;
        
        // Load target details from preset
        activeTargetTemp = materialLibrary[activeMaterialIndex].dryingTemp;
        timerDurationMin = materialLibrary[activeMaterialIndex].defaultTimer;
        
        Serial.println("[SYSTEM] Dryer turned ON.");
        operationalChange = true;
      }
    } else if (strcmp(val, "OFF") == 0) {
      if (isSystemActive) {
        isSystemActive = false;
        currentDryingState = STATE_OFF;
        safetyTimerArmed = false;
        Serial.println("[SYSTEM] Dryer turned OFF.");
        operationalChange = true;
      }
    }
  }

  // 2. Filament Preset Selection
  else if (strcmp(topic, CMD_FILAMENT_TOPIC) == 0) {
    for (int i = 0; i < materialCount; i++) {
      if (strcmp(val, materialLibrary[i].name) == 0) {
        activeMaterialIndex = i;
        
        // If system is active or changing presets, update default parameters automatically
        if (i > 0) { // Not manual mode
          activeTargetTemp = materialLibrary[i].dryingTemp;
          timerDurationMin = materialLibrary[i].defaultTimer;
        }
        Serial.print("[SYSTEM] Material preset updated: ");
        Serial.println(materialLibrary[i].name);
        operationalChange = true;
        break;
      }
    }
  }

  // 3. Target Temperature Override Command
  else if (strcmp(topic, CMD_TEMP_TOPIC) == 0) {
    float tempVal = atof(val);
    if (tempVal >= 20.0f && tempVal <= 75.0f) {
      activeTargetTemp = tempVal;
      activeMaterialIndex = 0; // Automatically force "Manual" profile on override
      Serial.print("[SYSTEM] Custom Temperature Override: ");
      Serial.print(activeTargetTemp);
      Serial.println(" °C");
      operationalChange = true;
    }
  }

  // 4. Target Humidity Override Command
  else if (strcmp(topic, CMD_HUMID_TOPIC) == 0) {
    float humidVal = atof(val);
    if (humidVal >= 5.0f && humidVal <= 50.0f) {
      activeTargetHumid = humidVal;
      Serial.print("[SYSTEM] Custom Humidity Override: ");
      Serial.print(activeTargetHumid);
      Serial.println(" % RH");
      operationalChange = true;
    }
  }

  // 5. Timer Override Command
  else if (strcmp(topic, CMD_TIMER_TOPIC) == 0) {
    int timerVal = atoi(val);
    if (timerVal >= 0 && timerVal <= 1440) {
      timerDurationMin = timerVal;
      activeMaterialIndex = 0; // Force "Manual" profile on override
      lastTimerTick = millis();
      Serial.print("[SYSTEM] Custom Timer set: ");
      Serial.print(timerDurationMin);
      Serial.println(" minutes");
      operationalChange = true;
    }
  }

  if (operationalChange) {
    saveOperationalState();
    publishTelemetryNow(); // Instant telemetry update on HA adjustments
  }
}

// =============================================================================
// NON-BLOCKING WIFI & MQTT STATE MACHINE
// =============================================================================
void maintainWiFiAndMqtt() {
  static unsigned long lastReconnectAttempt = 0;
  unsigned long now = millis();

  // 1. Maintain WiFi Connection
  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastReconnectAttempt >= 10000 || lastReconnectAttempt == 0) {
      lastReconnectAttempt = now;
      Serial.print("[WIFI] Reconnecting to SSID: ");
      Serial.println(devCfg.wifiSSID);
      WiFi.disconnect();
      WiFi.mode(WIFI_STA);
      WiFi.begin(devCfg.wifiSSID, devCfg.wifiPassword);
    }
    return; // Block MQTT process until WiFi succeeds
  }

  // 2. Maintain MQTT Broker Connection
  if (!mqttClient.connected()) {
    if (now - lastReconnectAttempt >= 8000 || lastReconnectAttempt == 0) {
      lastReconnectAttempt = now;
      
      char clientId[32];
      snprintf(clientId, sizeof(clientId), "filament-dryer-%d", devCfg.unitNumber);
      
      Serial.print("[MQTT] Connecting to Broker ");
      Serial.print(devCfg.mqttBroker);
      Serial.println("...");

      bool connResult = false;
      if (strlen(devCfg.mqttUser) > 0) {
        connResult = mqttClient.connect(clientId, devCfg.mqttUser, devCfg.mqttPassword);
      } else {
        connResult = mqttClient.connect(clientId);
      }

      if (connResult) {
        Serial.println("[MQTT] Connected to Broker!");
        
        // Re-publish auto discovery configurations
        publishMqttDiscovery();

        // Subscribe to HA write/command topics
        mqttClient.subscribe(CMD_POWER_TOPIC);
        mqttClient.subscribe(CMD_FILAMENT_TOPIC);
        mqttClient.subscribe(CMD_TEMP_TOPIC);
        mqttClient.subscribe(CMD_HUMID_TOPIC);
        mqttClient.subscribe(CMD_TIMER_TOPIC);
        
        publishTelemetryNow();
      } else {
        Serial.print("[MQTT] Connection failed, rc=");
        Serial.println(mqttClient.state());
      }
    }
  } else {
    // Keep connection alive and process incoming packets
    mqttClient.loop();
  }
}

// =============================================================================
// TELEMETRY LOGGING AND PUBLISHING
// =============================================================================
void publishTelemetry(const SensorData &env) {
  if (!mqttClient.connected()) return;

  StaticJsonDocument<512> doc;
  
  // Clean Sensor Telemetry
  doc["temperature"] = env.isValid ? (double)env.temperature : 0.0;
  doc["humidity"] = env.isValid ? (double)env.humidity : 0.0;
  doc["heater_exhaust_temp"] = env.heaterExhaustValid ? (double)env.heaterExhaustTemp : 0.0;
  doc["exhaust_sensor_present"] = hasHeaterExhaustSensor;
  doc["fw_version"] = FIRMWARE_VERSION;


  // Active drying metrics
  doc["is_active"] = isSystemActive;
  doc["filament"] = materialLibrary[activeMaterialIndex].name;
  doc["target_temp"] = activeTargetTemp;
  doc["target_humidity"] = activeTargetHumid;
  doc["timer_duration"] = timerDurationMin;
  
  if (isSystemActive && timerDurationMin > 0) {
    doc["timer_remaining"] = timerDurationMin;
  } else {
    doc["timer_remaining"] = 0;
  }

  doc["heater_power"] = isSystemActive ? (int)((currentHeaterPWM / 255.0f) * 100.0f) : 0;
  doc["fan_power"] = isSystemActive ? (int)((currentFanPWM / 255.0f) * 100.0f) : 0;

  if (isSafetyLockout) {
    doc["state"] = "safety_lockout";
    doc["error"] = lockoutReason;
  } else if (!isSystemActive) {
    doc["state"] = "off";
  } else if (currentDryingState == STATE_HEATING_UP) {
    doc["state"] = "drying";
  } else if (currentDryingState == STATE_MAINTAINING) {
    doc["state"] = "holding";
  }

  char payload[512];
  serializeJson(doc, payload);
  mqttClient.publish(STATE_TOPIC, payload, true);
  
  Serial.print("[MQTT TX] Telemetry => ");
  Serial.println(payload);
}

void publishTelemetryNow() {
  SensorData env = readSensors();
  publishTelemetry(env);
}

// =============================================================================
// SETUP SYSTEM INITIALIZATIONS
// =============================================================================
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1500); // Wait for terminal on ESP32-C6 USB-C

  Serial.println("\n==============================================");
  Serial.printf("  ESP32-C6-Zero Smart Filament Dryer v%s\n", FIRMWARE_VERSION);
  Serial.println("==============================================\n");

  pinMode(PIN_HEATER_GATE, OUTPUT);
  pinMode(PIN_FAN_GATE, OUTPUT);
  pinMode(PIN_STATUS_LED, OUTPUT);

  analogWrite(PIN_HEATER_GATE, 0);
  analogWrite(PIN_FAN_GATE, 0);
  digitalWrite(PIN_STATUS_LED, LOW);

  // Load configuration from Preferences/NVS
  loadConfiguration();

  // If not provisioned, force Captive Portal on boot
  if (!isProvisioned) {
    Serial.println("[SYSTEM] Unprovisioned! Starting Captive Portal...");
    startCaptivePortal();
  }

  // Set up derived topics
  snprintf(STATE_TOPIC, sizeof(STATE_TOPIC), "garden/dryer%d/state", devCfg.unitNumber);
  snprintf(CMD_POWER_TOPIC, sizeof(CMD_POWER_TOPIC), "garden/dryer%d/cmd/power", devCfg.unitNumber);
  snprintf(CMD_FILAMENT_TOPIC, sizeof(CMD_FILAMENT_TOPIC), "garden/dryer%d/cmd/filament", devCfg.unitNumber);
  snprintf(CMD_TEMP_TOPIC, sizeof(CMD_TEMP_TOPIC), "garden/dryer%d/cmd/target_temp", devCfg.unitNumber);
  snprintf(CMD_HUMID_TOPIC, sizeof(CMD_HUMID_TOPIC), "garden/dryer%d/cmd/target_humidity", devCfg.unitNumber);
  snprintf(CMD_TIMER_TOPIC, sizeof(CMD_TIMER_TOPIC), "garden/dryer%d/cmd/timer", devCfg.unitNumber);

  Serial.print("[SYSTEM] Configured as Dryer Unit #");
  Serial.println(devCfg.unitNumber);

  // Initialize sensors
  if (!initSensors()) {
    isSafetyLockout = true;
    lockoutReason = "Sensor init failure on boot";
    Serial.println("[CRITICAL] Sensor initialization failed! System running in safety lockout.");
  }

  // Set up MQTT broker endpoints
  mqttClient.setServer(devCfg.mqttBroker, devCfg.mqttPort);
  mqttClient.setCallback(mqttCallback);
  
  // Set larger packet buffer to accommodate HA config strings safely
  mqttClient.setBufferSize(1024);

  // Initial Wi-Fi STA setup
  WiFi.mode(WIFI_STA);
  WiFi.begin(devCfg.wifiSSID, devCfg.wifiPassword);

  lastPurgeCycleTime = millis();
}

// =============================================================================
// CORE REGULATION & CONTROL FUNCTION LOOP
// =============================================================================
void loop() {
  unsigned long now = millis();

  // Maintain Smart home connectivity in a completely non-blocking way
  maintainWiFiAndMqtt();

  // Safety override - immediately power off loads on lockout
  if (isSafetyLockout) {
    analogWrite(PIN_HEATER_GATE, 0);
    analogWrite(PIN_FAN_GATE, 255); // Spin fan to purge residual heat
    blinkLED(now, 100); // Fast 5Hz strobe for fault alert
    return;
  }

  // 1. Process Option Countdown Timer (Decrements every 60 seconds)
  if (isSystemActive && timerDurationMin > 0) {
    if (now - lastTimerTick >= 60000) {
      lastTimerTick = now;
      timerDurationMin--;
      Serial.print("[TIMER] Countdown: ");
      Serial.print(timerDurationMin);
      Serial.println(" minutes remaining.");

      // Check if time expired
      if (timerDurationMin == 0) {
        isSystemActive = false;
        currentDryingState = STATE_OFF;
        safetyTimerArmed = false;
        Serial.println("[TIMER] Time reached! Powering off.");
        saveOperationalState();
        publishTelemetryNow();
      }
    }
  }

  // 2. Perform non-blocking environmental measurement & thermal control loop
  if (now - lastMeasureTime >= MEASURE_INTERVAL) {
    lastMeasureTime = now;
    
    SensorData env = readSensors();
    
    if (!env.isValid) {
      isSafetyLockout = true;
      lockoutReason = "Chamber sensor disconnected/faulty";
      Serial.println("[CRITICAL] Sensor read error! Triggering safety shutdown.");
      return;
    }

    if (isSystemActive) {
      // Manage state transitions based on Measured Humidity and overrides
      switch (currentDryingState) {
        case STATE_HEATING_UP:
          currentFanPWM = FAN_SPEED_ACTIVE;
          
          // Transition to MAINTAINING once target humidity limit is satisfied
          if (env.humidity <= activeTargetHumid) {
            currentDryingState = STATE_MAINTAINING;
            Serial.println("[SYSTEM] Target humidity achieved! Dropping to MAINTENANCE holding mode.");
          }
          break;

        case STATE_MAINTAINING:
          currentFanPWM = FAN_SPEED_IDLE;
          
          // Re-arm active heating if humidity spikes back up beyond hysteresis limit
          if (env.humidity > (activeTargetHumid + HUMID_HYSTERESIS)) {
            currentDryingState = STATE_HEATING_UP;
            Serial.println("[SYSTEM] Humidity increased above threshold. Reactivating ACTIVE drying.");
          }
          break;
          
        case STATE_OFF:
          // Recover state logic in case active state mismatch occurred
          currentDryingState = STATE_HEATING_UP;
          break;
      }

      // Proportional heat regulation
      float currentTarget = (currentDryingState == STATE_HEATING_UP) ? activeTargetTemp : TARGET_TEMP_HOLD;
      float tempError = currentTarget - env.temperature;

      if (tempError <= 0.0f) {
        currentHeaterPWM = 0;
      } else if (tempError >= 3.0f) {
        currentHeaterPWM = HEATER_MAX_DUTY;
      } else {
        currentHeaterPWM = (int)((tempError / 3.0f) * HEATER_MAX_DUTY);
      }

      // Safe control loop limit based on heater exhaust safety sensor
      if (env.heaterExhaustValid && env.heaterExhaustTemp > MAX_HEATER_EXHAUST_TEMP) {
        currentHeaterPWM = 0;
        currentFanPWM = 255; // Force fan to 100% to cool down the heater exhaust
        Serial.printf("[WARNING] Heater exhaust temperature (%.1fC) exceeded safety threshold (%.1fC)! Heater disabled, fan forced to 100%%.\n", 
                      env.heaterExhaustTemp, MAX_HEATER_EXHAUST_TEMP);
      }


      // 3. THERMAL RUNAWAY PROTECTION (Checks after 5 mins of heating)
      if (safetyTimerArmed) {
        // Record starting condition if newly armed
        if (tempAtHeaterStart == 0.0f) {
          tempAtHeaterStart = env.temperature;
          heaterStartTime = now;
        }

        // Evaluate after 5 minutes of continuous heating at > 50% power
        if (now - heaterStartTime >= 300000UL) { 
          if (currentHeaterPWM > 128) {
            float tempRise = env.temperature - tempAtHeaterStart;
            if (tempRise < 2.0f) {
              isSafetyLockout = true;
              lockoutReason = "Thermal Runaway protection triggered! No temp rise.";
              Serial.println("[CRITICAL] Heater is on but temperature failed to rise. Lockout active!");
              return;
            }
          }
          // Reset starting condition for next checking window
          tempAtHeaterStart = env.temperature;
          heaterStartTime = now;
        }
      }

      // 4. PERIODIC PURGE VENTILATION (30 seconds of high fan every 10 minutes)
      if (!isPurging) {
        if (now - lastPurgeCycleTime >= 600000UL) { // 10 minutes
          isPurging = true;
          purgeStartTime = now;
          Serial.println("[SYSTEM] Starting periodic ventilation purge cycle (30 seconds)...");
        }
      } else {
        if (now - purgeStartTime >= 30000UL) { // 30 seconds
          isPurging = false;
          lastPurgeCycleTime = now;
          Serial.println("[SYSTEM] Purge completed. Resuming standard fan circulation speed.");
        } else {
          currentFanPWM = 255; // Force fan to full power to purge chamber boundary layers
        }
      }

      // Set physical output pins
      analogWrite(PIN_HEATER_GATE, currentHeaterPWM);
      analogWrite(PIN_FAN_GATE, currentFanPWM);

    } else {
      // System is OFF
      currentHeaterPWM = 0;
      currentFanPWM = 0;
      analogWrite(PIN_HEATER_GATE, 0);
      analogWrite(PIN_FAN_GATE, 0);
    }
  }

  // 5. Publish Telemetry at standard intervals (every 5 seconds)
  if (now - lastTelemetryTime >= TELEMETRY_INTERVAL) {
    lastTelemetryTime = now;
    SensorData env = readSensors();
    publishTelemetry(env);
  }

  // 6. Update Visual Status Indicators
  updateVisualIndicators(now);
}

// =============================================================================
// SYSTEM INDICATOR LED CONTROLLER
// =============================================================================
void blinkLED(unsigned long now, unsigned long interval) {
  if (now - lastLedToggle >= interval) {
    lastLedToggle = now;
    ledState = !ledState;
    digitalWrite(PIN_STATUS_LED, ledState ? HIGH : LOW);
  }
}

void updateVisualIndicators(unsigned long now) {
  if (isSafetyLockout) {
    blinkLED(now, 100); // 5Hz Rapid flashing: Critical Alarm
  } else if (!isSystemActive) {
    // Brief tiny blip every 5 seconds to show alive in standby
    unsigned long standbyCycle = now % 5000;
    if (standbyCycle < 50) {
      digitalWrite(PIN_STATUS_LED, HIGH);
    } else {
      digitalWrite(PIN_STATUS_LED, LOW);
    }
  } else {
    // Normal operation
    if (currentDryingState == STATE_HEATING_UP) {
      blinkLED(now, 1000); // 1Hz slow pulse: actively drying
    } else if (currentDryingState == STATE_MAINTAINING) {
      // Steady short blip every 2 seconds to show maintaining healthy environment
      unsigned long maintCycle = now % 2000;
      if (maintCycle < 100) {
        digitalWrite(PIN_STATUS_LED, HIGH);
      } else {
        digitalWrite(PIN_STATUS_LED, LOW);
      }
    }
  }
}
