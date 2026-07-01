#include <WiFi.h>
#include <PubSubClient.h>

// =====================================================
// WIFI + MQTT / RASPBERRY PI
// =====================================================

const char* WIFI_SSID = "";
const char* WIFI_PASSWORD = "";

const char* RASPBERRY_MQTT_IP = "";
const int MQTT_PORT = 1883;

const char* TOPIC_EVENT  = "smart_gate/event";   // ESP32 -> Raspberry: CAR_AT_GATE
const char* TOPIC_CMD    = "smart_gate/cmd";     // Raspberry/Hermes -> ESP32: OPEN/CLOSE/STOP
const char* TOPIC_STATE  = "smart_gate/state";   // ESP32 -> gate state status
const char* TOPIC_STATUS = "smart_gate/status";  // ESP32 -> JSON sensor status

WiFiClient espClient;
PubSubClient mqtt(espClient);

unsigned long lastWifiAttempt = 0;
unsigned long lastWifiStatusPrint = 0;
unsigned long lastMqttAttempt = 0;
unsigned long lastStatusPublish = 0;
unsigned long lastOcrPublishAttempt = 0;

const unsigned long WIFI_RETRY_MS = 10000;
const unsigned long WIFI_STATUS_PRINT_MS = 5000;
const unsigned long MQTT_RETRY_MS = 3000;
const unsigned long STATUS_HEARTBEAT_MS = 10000;
const unsigned long OCR_RETRY_MS = 2000;
const unsigned long CAR_CLEAR_RESET_MS = 1200;
const unsigned long WAITING_EMPTY_RESET_MS = 8000;

bool wifiWasConnected = false;
bool mqttWasConnected = false;

// =====================================================
// PINS
// =====================================================

// ESP32 -> CMOS -> H-bridge
#define ESP_OUT_A 18
#define ESP_OUT_B 19

// Set to true if the CMOS stage inverts the signal.
// Keep false if the CMOS stage only works as a buffer.
#define CMOS_INVERTED false

// Gate IR sensors
#define IR_SENSOR_1 34
#define IR_SENSOR_2 35

// Ultrasonic sensor - entry / outside
#define ULTRA_IN_TRIG 32
#define ULTRA_IN_ECHO 14

// Ultrasonic sensor - exit / inside
#define ULTRA_OUT_TRIG 33
#define ULTRA_OUT_ECHO 26

// LEDs
#define LED_OPENING 23
#define LED_CLOSING 21
#define LED_STATUS  22

// =====================================================
// CONFIGURATION
// =====================================================

#define IR_ACTIVE_LOW true

const int CAR_DISTANCE_CM = 10;

const unsigned long MAX_OPEN_TIME_MS = 25000;
const unsigned long MAX_CLOSE_TIME_MS = 25000;
const unsigned long OCR_DECISION_TIMEOUT_MS = 90000;  // 1 min 30 s

// =====================================================
// STATES
// =====================================================

enum GateState {
  CLOSED,
  WAITING_DECISION,
  OPENING,
  OPEN,
  CLOSING,
  STOPPED,
  ERROR_STATE
};

GateState state = STOPPED;

unsigned long stateStartedAt = 0;

long distanceIn = -1;
long distanceOut = -1;

bool ir1 = false;
bool ir2 = false;

bool gateClosed = false;
bool gateOpen = false;
bool gateBetween = false;

bool carIn = false;
bool carOut = false;
bool anyCar = false;

bool lastCarIn = false;
bool lastCarOut = false;

bool ocrRequestedForCurrentCar = false;
bool blockNewOcrUntilCarLeaves = false;
unsigned long carInClearStartedAt = 0;
unsigned long waitingNoCarStartedAt = 0;

// =====================================================
// HELPERS
// =====================================================

const char* stateName(GateState s) {
  switch (s) {
    case CLOSED:           return "CLOSED";
    case WAITING_DECISION: return "WAITING_DECISION";
    case OPENING:          return "OPENING";
    case OPEN:             return "OPEN";
    case CLOSING:          return "CLOSING";
    case STOPPED:          return "STOPPED";
    case ERROR_STATE:      return "ERROR";
    default:               return "UNKNOWN";
  }
}

const char* wifiStatusText(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "SSID not found";
    case WL_SCAN_COMPLETED: return "SCAN zavrsen";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "Connection failed";
    case WL_CONNECTION_LOST: return "Connection lost";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "Unknown status";
  }
}

void logImportant(const char* text) {
  Serial.println(text);
}

void publishState() {
  if (mqtt.connected()) {
    mqtt.publish(TOPIC_STATE, stateName(state), true);
  }
}

void setState(GateState newState) {
  if (state != newState) {
    Serial.print("[STATE] ");
    Serial.print(stateName(state));
    Serial.print(" -> ");
    Serial.println(stateName(newState));
  }

  state = newState;
  stateStartedAt = millis();
  publishState();
}

void printDistance(long d) {
  if (d < 0) {
    Serial.print("N/A");
  } else {
    Serial.print(d);
    Serial.print("cm");
  }
}


// Forward declarations for functions used before their definitions
void publishStatus(bool force);
void publishStatusText(const char* text);

// =====================================================
// MOTOR
// =====================================================

void writeMotorPin(int pin, bool level) {
  if (CMOS_INVERTED) {
    digitalWrite(pin, level ? LOW : HIGH);
  } else {
    digitalWrite(pin, level ? HIGH : LOW);
  }
}

void setMotorOutputs(bool a, bool b) {
  writeMotorPin(ESP_OUT_A, a);
  writeMotorPin(ESP_OUT_B, b);
}

void motorStop() {
  setMotorOutputs(false, false);

  digitalWrite(LED_OPENING, LOW);
  digitalWrite(LED_CLOSING, LOW);
  digitalWrite(LED_STATUS, HIGH);
}

void motorOpen() {
  // ESP_OUT_A inactive, ESP_OUT_B active
  setMotorOutputs(false, true);

  digitalWrite(LED_OPENING, HIGH);
  digitalWrite(LED_CLOSING, LOW);
  digitalWrite(LED_STATUS, LOW);
}

void motorClose() {
  // ESP_OUT_A active, ESP_OUT_B inactive
  setMotorOutputs(true, false);

  digitalWrite(LED_OPENING, LOW);
  digitalWrite(LED_CLOSING, HIGH);
  digitalWrite(LED_STATUS, LOW);
}

// =====================================================
// SENSORS
// =====================================================

bool readIR(int pin) {
  int value = digitalRead(pin);

  if (IR_ACTIVE_LOW) {
    return value == LOW;
  } else {
    return value == HIGH;
  }
}

long readDistanceCm(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(3);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, 30000);

  if (duration == 0) {
    return -1;
  }

  return duration * 0.0343 / 2.0;
}

void readSensors() {
  ir1 = readIR(IR_SENSOR_1);
  ir2 = readIR(IR_SENSOR_2);

  // both IR sensors active = closed
  // both IR sensors inactive = open
  // one IR sensor active = between end positions
  gateClosed = ir1 && ir2;
  gateOpen = !ir1 && !ir2;
  gateBetween = ir1 != ir2;

  distanceIn = readDistanceCm(ULTRA_IN_TRIG, ULTRA_IN_ECHO);
  delay(50);

  distanceOut = readDistanceCm(ULTRA_OUT_TRIG, ULTRA_OUT_ECHO);
  delay(50);

  carIn = distanceIn > 0 && distanceIn <= CAR_DISTANCE_CM;
  carOut = distanceOut > 0 && distanceOut <= CAR_DISTANCE_CM;

  anyCar = carIn || carOut;
}

void updateOcrUnlockByCarLeaving() {
  if (!carIn) {
    if (carInClearStartedAt == 0) {
      carInClearStartedAt = millis();
    }

    if (millis() - carInClearStartedAt > CAR_CLEAR_RESET_MS) {
      if (ocrRequestedForCurrentCar || blockNewOcrUntilCarLeaves) {
        Serial.println("[OCR] Entry sensor is clear. A new vehicle arrival is allowed.");
      }

      ocrRequestedForCurrentCar = false;
      blockNewOcrUntilCarLeaves = false;
    }
  } else {
    carInClearStartedAt = 0;
  }
}

void resetDecisionAfterDenyOrFail(const char* reason) {
  Serial.print("[DECISION] ");
  Serial.print(reason);
  Serial.println(". Gate remains closed. Waiting for the vehicle to leave the sensor.");

  motorStop();

  ocrRequestedForCurrentCar = false;
  blockNewOcrUntilCarLeaves = true;
  waitingNoCarStartedAt = 0;

  readSensors();

  if (gateClosed) {
    setState(CLOSED);
  } else {
    setState(STOPPED);
  }

  publishStatusText(reason);
  publishStatus(true);
}

bool logSensorChanges(bool force = false) {
  static bool initialized = false;

  static bool prevIr1 = false;
  static bool prevIr2 = false;
  static bool prevGateClosed = false;
  static bool prevGateOpen = false;
  static bool prevGateBetween = false;
  static bool prevCarIn = false;
  static bool prevCarOut = false;
  static bool prevAnyCar = false;

  bool changed =
    !initialized ||
    ir1 != prevIr1 ||
    ir2 != prevIr2 ||
    gateClosed != prevGateClosed ||
    gateOpen != prevGateOpen ||
    gateBetween != prevGateBetween ||
    carIn != prevCarIn ||
    carOut != prevCarOut ||
    anyCar != prevAnyCar;

  if (force || changed) {
    Serial.print("[SENSORS] ");

    Serial.print("IR1=");
    Serial.print(ir1 ? "ACTIVE" : "INACTIVE");

    Serial.print(" | IR2=");
    Serial.print(ir2 ? "ACTIVE" : "INACTIVE");

    Serial.print(" | gateClosed=");
    Serial.print(gateClosed ? "YES" : "NO");

    Serial.print(" | gateOpen=");
    Serial.print(gateOpen ? "YES" : "NO");

    Serial.print(" | gateBetween=");
    Serial.print(gateBetween ? "YES" : "NO");

    Serial.print(" | IN=");
    printDistance(distanceIn);
    Serial.print(" carIn=");
    Serial.print(carIn ? "YES" : "NO");

    Serial.print(" | OUT=");
    printDistance(distanceOut);
    Serial.print(" carOut=");
    Serial.print(carOut ? "YES" : "NO");

    Serial.print(" | anyCar=");
    Serial.println(anyCar ? "YES" : "NO");

    prevIr1 = ir1;
    prevIr2 = ir2;
    prevGateClosed = gateClosed;
    prevGateOpen = gateOpen;
    prevGateBetween = gateBetween;
    prevCarIn = carIn;
    prevCarOut = carOut;
    prevAnyCar = anyCar;

    initialized = true;
  }

  return force || changed;
}

// =====================================================
// MQTT STATUS
// =====================================================

void publishStatus(bool force = false) {
  if (!mqtt.connected()) {
    return;
  }

  if (!force && millis() - lastStatusPublish < STATUS_HEARTBEAT_MS) {
    return;
  }

  char payload[520];
  snprintf(
    payload,
    sizeof(payload),
    "{\"state\":\"%s\",\"ir1\":%s,\"ir2\":%s,\"gateOpen\":%s,\"gateClosed\":%s,\"gateBetween\":%s,\"distanceIn\":%ld,\"distanceOut\":%ld,\"carIn\":%s,\"carOut\":%s,\"anyCar\":%s,\"ocrRequested\":%s,\"blockUntilLeave\":%s,\"wifi\":%s,\"mqtt\":%s}",
    stateName(state),
    ir1 ? "true" : "false",
    ir2 ? "true" : "false",
    gateOpen ? "true" : "false",
    gateClosed ? "true" : "false",
    gateBetween ? "true" : "false",
    distanceIn,
    distanceOut,
    carIn ? "true" : "false",
    carOut ? "true" : "false",
    anyCar ? "true" : "false",
    ocrRequestedForCurrentCar ? "true" : "false",
    blockNewOcrUntilCarLeaves ? "true" : "false",
    WiFi.status() == WL_CONNECTED ? "true" : "false",
    mqtt.connected() ? "true" : "false"
  );

  mqtt.publish(TOPIC_STATUS, payload, false);
  lastStatusPublish = millis();
}

void publishStatusText(const char* text) {
  if (mqtt.connected()) {
    mqtt.publish(TOPIC_STATUS, text, false);
  }
}

// =====================================================
// WIFI / MQTT
// =====================================================

void connectWiFiNonBlocking() {
  unsigned long now = millis();

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiWasConnected) {
      Serial.println("[WIFI] Connected.");
      Serial.print("[WIFI] SSID: ");
      Serial.println(WIFI_SSID);
      Serial.print("[WIFI] IP adresa: ");
      Serial.println(WiFi.localIP());
      wifiWasConnected = true;
    }
    return;
  }

  if (wifiWasConnected) {
    Serial.println("[WIFI] Connection lost.");
    wifiWasConnected = false;
  }

  if (now - lastWifiAttempt > WIFI_RETRY_MS) {
    lastWifiAttempt = now;

    Serial.print("[WIFI] Trying to connect to: ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect(false, true);
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  if (now - lastWifiStatusPrint > WIFI_STATUS_PRINT_MS) {
    lastWifiStatusPrint = now;
    Serial.print("[WIFI] Status: ");
    Serial.println(wifiStatusText(WiFi.status()));
  }
}

void onMqttMessage(char* topic, byte* payload, unsigned int length);

void connectMqttNonBlocking() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (mqtt.connected()) {
    if (!mqttWasConnected) {
      Serial.println("[MQTT] Connected.");
      mqttWasConnected = true;
    }
    return;
  }

  if (mqttWasConnected) {
    Serial.println("[MQTT] Connection lost.");
    mqttWasConnected = false;
  }

  if (millis() - lastMqttAttempt < MQTT_RETRY_MS) {
    return;
  }

  lastMqttAttempt = millis();

  String clientId = "esp32-smart-gate-" + String((uint32_t)ESP.getEfuseMac(), HEX);

  Serial.print("[MQTT] Connecting to ");
  Serial.print(RASPBERRY_MQTT_IP);
  Serial.print(":");
  Serial.println(MQTT_PORT);

  if (mqtt.connect(clientId.c_str(), TOPIC_STATE, 0, true, "OFFLINE")) {
    Serial.println("[MQTT] Connected.");
    mqttWasConnected = true;
    mqtt.subscribe(TOPIC_CMD);
    mqtt.publish(TOPIC_STATE, "ONLINE", true);
    publishState();
    publishStatus(true);
  } else {
    Serial.print("[MQTT] Connection failed. rc=");
    Serial.println(mqtt.state());
  }
}

void maintainNetwork() {
  connectWiFiNonBlocking();
  connectMqttNonBlocking();

  if (mqtt.connected()) {
    mqtt.loop();
  }
}

// =====================================================
// ACTIONS
// =====================================================

void startOpening() {
  readSensors();
  bool changed = logSensorChanges();
  if (changed) publishStatus(true);

  if (gateOpen) {
    logImportant("[INFO] Gate is already open.");
    motorStop();
    setState(OPEN);
    publishStatus(true);
    return;
  }

  logImportant("[GATE] Opening.");
  motorOpen();
  setState(OPENING);
  publishStatus(true);
}

void startClosing() {
  readSensors();
  bool changed = logSensorChanges();
  if (changed) publishStatus(true);

  if (anyCar) {
    logImportant("[SAFETY] Closing blocked. A vehicle is still on the sensor.");
    motorStop();

    if (gateOpen) {
      setState(OPEN);
    } else if (gateClosed) {
      setState(CLOSED);
    } else {
      setState(STOPPED);
    }

    publishStatus(true);
    return;
  }

  if (gateClosed) {
    logImportant("[INFO] Gate is already closed.");
    motorStop();
    setState(CLOSED);
    publishStatus(true);
    return;
  }

  logImportant("[GATE] Closing.");
  motorClose();
  setState(CLOSING);
  publishStatus(true);
}

bool sendCarAtGateToRaspberry() {
  if (!mqtt.connected()) {
    logImportant("[MQTT] Not connected. Cannot send CAR_AT_GATE.");
    return false;
  }

  bool ok = mqtt.publish(TOPIC_EVENT, "CAR_AT_GATE", false);

  if (ok) {
    logImportant("[ENTRY] CAR_AT_GATE sent to Raspberry Pi. Waiting for OPEN.");
    ocrRequestedForCurrentCar = true;
    blockNewOcrUntilCarLeaves = false;
    waitingNoCarStartedAt = 0;
    publishStatusText("WAITING_DECISION");
    setState(WAITING_DECISION);
    publishStatus(true);
  } else {
    logImportant("[MQTT] CAR_AT_GATE publish failed.");
  }

  return ok;
}

void stopGate() {
  logImportant("[GATE] STOP.");
  motorStop();
  setState(STOPPED);
  publishStatus(true);
}

void resetFromErrorOrStopped() {
  readSensors();
  bool changed = logSensorChanges(true);
  if (changed) publishStatus(true);

  motorStop();

  if (gateOpen) {
    setState(OPEN);
  } else if (gateClosed) {
    setState(CLOSED);
  } else {
    setState(STOPPED);
  }

  publishStatus(true);
}

// =====================================================
// MQTT CALLBACK
// =====================================================

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  char cmd[40];
  unsigned int n = min(length, sizeof(cmd) - 1);

  for (unsigned int i = 0; i < n; i++) {
    cmd[i] = (char)payload[i];
  }
  cmd[n] = '\0';

  Serial.print("[MQTT CMD] ");
  Serial.println(cmd);

  if (strcmp(cmd, "OPEN") == 0) {
    ocrRequestedForCurrentCar = false;
    blockNewOcrUntilCarLeaves = true;
    waitingNoCarStartedAt = 0;
    startOpening();
    return;
  }

  if (strcmp(cmd, "DENY") == 0 ||
      strcmp(cmd, "REJECT") == 0 ||
      strcmp(cmd, "CANCEL") == 0 ||
      strcmp(cmd, "NO_TEXT") == 0 ||
      strcmp(cmd, "OCR_FAILED") == 0) {
    resetDecisionAfterDenyOrFail(cmd);
    return;
  }

  if (strcmp(cmd, "CLOSE") == 0) {
    startClosing();
    return;
  }

  if (strcmp(cmd, "STOP") == 0) {
    stopGate();
    return;
  }

  if (strcmp(cmd, "RESET") == 0) {
    resetFromErrorOrStopped();
    return;
  }

  if (strcmp(cmd, "STATUS") == 0) {
    publishStatus(true);
    return;
  }

  Serial.println("[MQTT CMD] Unknown command.");
}

// =====================================================
// GATE LOGIC
// =====================================================

void updateGateLogic() {
  readSensors();
  updateOcrUnlockByCarLeaving();
  bool sensorChanged = logSensorChanges();

  if (sensorChanged) {
    publishStatus(true);
  } else {
    publishStatus(false);
  }

  switch (state) {

    case CLOSED:
      motorStop();

      // ENTRY SENSOR: DOES NOT OPEN THE GATE BY ITSELF.
      // It only sends CAR_AT_GATE to the Raspberry Pi and waits for OPEN.
      if (carIn && !ocrRequestedForCurrentCar && !blockNewOcrUntilCarLeaves && millis() - lastOcrPublishAttempt > OCR_RETRY_MS) {
        lastOcrPublishAttempt = millis();
        if (sendCarAtGateToRaspberry()) {
          ocrRequestedForCurrentCar = true;
        }
      }

      // EXIT SENSOR: no longer opens the gate by itself.
      // It is used only later as a safety/hold-open sensor while the gate is open or closing.
      if (carOut && !lastCarOut) {
        logImportant("[EXIT] Vehicle detected, but the gate will not open without an OPEN command.");
      }
      break;

    case WAITING_DECISION:
      motorStop();

      // While waiting for OCR/Telegram, the ESP32 does not start the motor.
      // Opening may start only after Raspberry Pi/Hermes sends OPEN to smart_gate/cmd.
      if (!carIn) {
        if (waitingNoCarStartedAt == 0) {
          waitingNoCarStartedAt = millis();
        }

        // Fallback protection: if the Raspberry Pi does not send DENY after rejection,
        // and the vehicle leaves the entry sensor, allow a new attempt.
        if (millis() - waitingNoCarStartedAt > WAITING_EMPTY_RESET_MS) {
          resetDecisionAfterDenyOrFail("WAITING_RESET_CAR_LEFT");
          break;
        }
      } else {
        waitingNoCarStartedAt = 0;
      }

      if (millis() - stateStartedAt > OCR_DECISION_TIMEOUT_MS) {
        resetDecisionAfterDenyOrFail("ERROR_OCR_TIMEOUT");
      }
      break;

    case STOPPED:
      motorStop();

      if (gateOpen) {
        setState(OPEN);
      } else if (gateClosed) {
        setState(CLOSED);
      } else if (carIn && !ocrRequestedForCurrentCar && !blockNewOcrUntilCarLeaves && millis() - lastOcrPublishAttempt > OCR_RETRY_MS) {
        lastOcrPublishAttempt = millis();
        if (sendCarAtGateToRaspberry()) {
          ocrRequestedForCurrentCar = true;
        }
      } else if (carOut && !lastCarOut) {
        logImportant("[EXIT] Vehicle detected while the gate is between end positions. Not opening without an OPEN command.");
      }
      break;

    case OPENING:
      if (gateOpen) {
        logImportant("[GATE] Gate open.");
        motorStop();
        setState(OPEN);
        publishStatus(true);
      } else if (millis() - stateStartedAt > MAX_OPEN_TIME_MS) {
        logImportant("[ERROR] Opening timeout.");
        motorStop();
        setState(ERROR_STATE);
        publishStatusText("ERROR_OPEN_TIMEOUT");
        publishStatus(true);
      }
      break;

    case OPEN:
      motorStop();

      // The gate remains open while any ultrasonic sensor sees a vehicle.
      // When both sensors are clear, closing starts immediately.
      if (!anyCar) {
        logImportant("[GATE] Sensors are clear. Closing immediately.");
        startClosing();
      }
      break;

    case CLOSING:
      if (anyCar) {
        logImportant("[SAFETY] Vehicle detected during closing. Reopening.");
        motorStop();
        delay(300);
        startOpening();
        return;
      }

      if (gateClosed) {
        logImportant("[GATE] Gate closed.");
        motorStop();
        setState(CLOSED);
        publishStatus(true);
      } else if (millis() - stateStartedAt > MAX_CLOSE_TIME_MS) {
        logImportant("[ERROR] Closing timeout.");
        motorStop();
        setState(ERROR_STATE);
        publishStatusText("ERROR_CLOSE_TIMEOUT");
        publishStatus(true);
      }
      break;

    case ERROR_STATE:
      motorStop();
      break;
  }

  lastCarIn = carIn;
  lastCarOut = carOut;
}

// =====================================================
// SETUP / LOOP
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(ESP_OUT_A, OUTPUT);
  pinMode(ESP_OUT_B, OUTPUT);

  pinMode(IR_SENSOR_1, INPUT);
  pinMode(IR_SENSOR_2, INPUT);

  pinMode(ULTRA_IN_TRIG, OUTPUT);
  pinMode(ULTRA_IN_ECHO, INPUT);

  pinMode(ULTRA_OUT_TRIG, OUTPUT);
  pinMode(ULTRA_OUT_ECHO, INPUT);

  pinMode(LED_OPENING, OUTPUT);
  pinMode(LED_CLOSING, OUTPUT);
  pinMode(LED_STATUS, OUTPUT);

  digitalWrite(ULTRA_IN_TRIG, LOW);
  digitalWrite(ULTRA_OUT_TRIG, LOW);

  motorStop();

  Serial.println("ESP32 SMART GATE START - WIFI + MQTT + RASPBERRY PI - RETRY AFTER DENY");
  Serial.print("WiFi SSID: ");
  Serial.println(WIFI_SSID);
  Serial.print("MQTT broker: ");
  Serial.println(RASPBERRY_MQTT_IP);

  mqtt.setServer(RASPBERRY_MQTT_IP, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(512);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  readSensors();
  logSensorChanges(true);

  if (gateOpen) {
    setState(OPEN);
  } else if (gateClosed) {
    setState(CLOSED);
  } else {
    setState(STOPPED);
  }
}

void loop() {
  maintainNetwork();
  updateGateLogic();
  maintainNetwork();
  delay(120);
}
