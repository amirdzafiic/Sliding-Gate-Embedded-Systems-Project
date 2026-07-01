// ================= PINS =================

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

// ================= CONFIGURATION =================

#define IR_ACTIVE_LOW true

const int CAR_DISTANCE_CM = 10;

const unsigned long MAX_OPEN_TIME_MS  = 25000;
const unsigned long MAX_CLOSE_TIME_MS = 25000;

// ================= STATES =================

enum GateState {
  CLOSED,
  OPENING,
  OPEN,
  CLOSING,
  STOPPED,
  ERROR_STATE
};

GateState state = STOPPED;

unsigned long stateStartedAt = 0;
unsigned long clearStartedAt = 0;

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

// ================= HELPERS =================

const char* stateName(GateState s) {
  switch (s) {
    case CLOSED:      return "CLOSED";
    case OPENING:     return "OPENING";
    case OPEN:        return "OPEN";
    case CLOSING:     return "CLOSING";
    case STOPPED:     return "STOPPED";
    case ERROR_STATE: return "ERROR";
    default:          return "UNKNOWN";
  }
}

void logImportant(const char* text) {
  Serial.println(text);
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
}

// ================= MOTOR =================

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
  // ESP_OUT_A active, ESP_OUT_B inactive
  setMotorOutputs(false, true);

  digitalWrite(LED_OPENING, HIGH);
  digitalWrite(LED_CLOSING, LOW);
  digitalWrite(LED_STATUS, LOW);
}

void motorClose() {
  // ESP_OUT_A inactive, ESP_OUT_B active
  setMotorOutputs(true, false);

  digitalWrite(LED_OPENING, LOW);
  digitalWrite(LED_CLOSING, HIGH);
  digitalWrite(LED_STATUS, LOW);
}

// ================= SENSORS =================

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

  // Logika:
  // both IR sensors active    = gate closed
  // both IR sensors inactive  = gate open
  // one IR sensor active      = gate between end positions
  gateClosed = ir1 && ir2;
  gateOpen = !ir1 && !ir2;
  gateBetween = ir1 != ir2;

  distanceIn = readDistanceCm(ULTRA_IN_TRIG, ULTRA_IN_ECHO);
  delay(80);

  distanceOut = readDistanceCm(ULTRA_OUT_TRIG, ULTRA_OUT_ECHO);
  delay(80);

  carIn = distanceIn > 0 && distanceIn <= CAR_DISTANCE_CM;
  carOut = distanceOut > 0 && distanceOut <= CAR_DISTANCE_CM;

  anyCar = carIn || carOut;
}

void printDistance(long d) {
  if (d < 0) {
    Serial.print("N/A");
  } else {
    Serial.print(d);
    Serial.print("cm");
  }
}

void logSensorChanges(bool force = false) {
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
}

// ================= ACTIONS =================

void startOpening() {
  readSensors();
  logSensorChanges();

  if (gateOpen) {
    logImportant("[INFO] Gate is already open.");

    motorStop();
    setState(OPEN);

    clearStartedAt = 0;
    return;
  }

  logImportant("[GATE] Opening.");

  motorOpen();
  setState(OPENING);

  clearStartedAt = 0;
}

void startClosing() {
  readSensors();
  logSensorChanges();

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

    clearStartedAt = 0;
    return;
  }

  if (gateClosed) {
    logImportant("[INFO] Gate is already closed.");

    motorStop();
    setState(CLOSED);

    clearStartedAt = 0;
    return;
  }

  logImportant("[GATE] Closing.");

  motorClose();
  setState(CLOSING);

  clearStartedAt = 0;
}

// ================= GATE LOGIC =================

void updateGateLogic() {
  readSensors();
  logSensorChanges();

  switch (state) {

    case CLOSED:
      // No Raspberry Pi, OCR, or WiFi in this standalone version.
      // A vehicle detected at entry or exit immediately starts opening.
      if (carIn && !lastCarIn) {
        logImportant("[ENTRY] Vehicle detected. Opening.");
        startOpening();
      }
      else if (carOut && !lastCarOut) {
        logImportant("[EXIT] Vehicle detected. Opening.");
        startOpening();
      }
      break;

    case STOPPED:
      if (gateOpen) {
        setState(OPEN);
        clearStartedAt = 0;
      }
      else if (gateClosed) {
        setState(CLOSED);
        clearStartedAt = 0;
      }
      else if (carIn && !lastCarIn) {
        logImportant("[ENTRY] Vehicle detected while the gate is between end positions. Opening.");
        startOpening();
      }
      else if (carOut && !lastCarOut) {
        logImportant("[EXIT] Vehicle detected while the gate is between end positions. Opening.");
        startOpening();
      }
      break;

    case OPENING:
      if (gateOpen) {
        logImportant("[GATE] Gate open.");

        motorStop();
        setState(OPEN);

        clearStartedAt = 0;
      }
      else if (millis() - stateStartedAt > MAX_OPEN_TIME_MS) {
        logImportant("[ERROR] Opening timeout.");

        motorStop();
        setState(ERROR_STATE);
      }
      break;

    case OPEN:
      // The gate remains open while any ultrasonic sensor is active.
      // As soon as both sensors are clear, closing starts immediately.
      if (!anyCar) {
        logImportant("[GATE] Sensors are clear. Closing immediately.");
        startClosing();
      } else {
        clearStartedAt = 0;
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

        clearStartedAt = 0;
      }
      else if (millis() - stateStartedAt > MAX_CLOSE_TIME_MS) {
        logImportant("[ERROR] Closing timeout.");

        motorStop();
        setState(ERROR_STATE);
      }
      break;

    case ERROR_STATE:
      motorStop();
      break;
  }

  lastCarIn = carIn;
  lastCarOut = carOut;
}

// ================= SETUP / LOOP =================

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

  logImportant("ESP32 SMART GATE START - STANDALONE MODE / NO WIFI / NO MQTT / NO RASPBERRY PI / NO ARDUINO");

  readSensors();
  logSensorChanges(true);

  if (gateOpen) {
    setState(OPEN);
  }
  else if (gateClosed) {
    setState(CLOSED);
  }
  else {
    setState(STOPPED);
  }
}

void loop() {
  updateGateLogic();
  delay(150);
}