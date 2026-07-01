# Smart Gate Access Control System

## Project summary

Smart Gate is a student embedded-systems project that demonstrates vehicle access control with license-plate recognition. The system detects a vehicle at the gate, captures the license plate with a Raspberry Pi camera, checks the plate against a local allowed-plates database, and controls a DC gate motor through an ESP32 controller.

The ESP32 handles low-level hardware control: motor direction, IR end-position sensors, ultrasonic vehicle sensors, safety states, and MQTT communication. The Raspberry Pi handles higher-level logic: MQTT broker communication, camera capture, OCR, Telegram notifications, and Hermes/MCP tools for user decisions.

## Academic context

This project was developed by second-year students at the Faculty of Electrical Engineering, University of Sarajevo (ETF), as part of the course **Ugradbeni sistemi**.

Year: **2026**

Responsible teacher: **R. prof. dr. Samim Konjicija, dipl. ing. el.**  
Assistant: **Selmir Gajip, BA ing.**

Team members: **Amir Džafić, Dino Tahirović, Imran Čustović**

## Main features

- ESP32 firmware written in Arduino/C++.
- Raspberry Pi 5 as the MQTT/OCR/Hermes integration node.
- MQTT communication between ESP32 and Raspberry Pi.
- USB camera capture and EasyOCR license-plate reading.
- Local JSON database for allowed plates.
- Telegram notifications for unknown or similar plates.
- MCP tools for Hermes Agent integration.
- Safety logic: the gate does not close while a vehicle is detected.
- Manual MQTT commands for testing and diagnostics.

## Hardware overview

| Component | Role |
|---|---|
| ESP32-WROOM-32D | Gate controller, GPIO control, MQTT client |
| Raspberry Pi 5 | MQTT/OCR/Hermes integration node |
| USB camera | License-plate image capture |
| H-bridge + CMOS logic | DC motor direction control |
| 12 V DC motor | Gate movement |
| 2 ultrasonic sensors | Vehicle detection at entrance and exit |
| 2 IR sensors | Gate end-position detection |
| LEDs | Opening, closing, and idle status indication |

## ESP32 pinout

| GPIO | Code name | Purpose |
|---:|---|---|
| 18 | `ESP_OUT_A` | H-bridge direction input A |
| 19 | `ESP_OUT_B` | H-bridge direction input B |
| 34 | `IR_SENSOR_1` | Gate end-position IR sensor |
| 35 | `IR_SENSOR_2` | Gate end-position IR sensor |
| 32 | `ULTRA_IN_TRIG` | Entrance ultrasonic trigger |
| 14 | `ULTRA_IN_ECHO` | Entrance ultrasonic echo |
| 33 | `ULTRA_OUT_TRIG` | Exit ultrasonic trigger |
| 26 | `ULTRA_OUT_ECHO` | Exit ultrasonic echo |
| 23 | `LED_OPENING` | Opening indicator |
| 21 | `LED_CLOSING` | Closing indicator |
| 22 | `LED_STATUS` | Idle/status indicator |

## MQTT topics

| Topic | Direction | Payload | Purpose |
|---|---|---|---|
| `smart_gate/event` | ESP32 to Raspberry Pi | `CAR_AT_GATE` | Vehicle detected at the entrance |
| `smart_gate/cmd` | Raspberry Pi/Hermes to ESP32 | `OPEN`, `DENY`, `CLOSE`, `STOP`, `RESET`, `STATUS` | Gate commands |
| `smart_gate/state` | ESP32 to all clients | `CLOSED`, `WAITING_DECISION`, `OPENING`, `OPEN`, `CLOSING`, `STOPPED`, `ERROR` | Retained gate state |
| `smart_gate/status` | ESP32 to all clients | JSON or text status | Sensor telemetry and diagnostics |

## System workflow

1. The gate is closed and the ESP32 monitors the entrance ultrasonic sensor.
2. A vehicle is detected at the entrance.
3. ESP32 publishes `CAR_AT_GATE` on `smart_gate/event` and waits for a decision.
4. Raspberry Pi captures an image and runs OCR.
5. The detected plate is normalized and checked against `allowed_plates.json`.
6. If the plate is allowed, Raspberry Pi sends `OPEN` to `smart_gate/cmd`.
7. If the plate is unknown or only similar, the system saves it as pending and sends a Telegram message.
8. Hermes/MCP can allow once, add and allow, or deny the pending plate.
9. ESP32 opens the gate and keeps it open while any ultrasonic sensor detects a vehicle.
10. When both ultrasonic sensors are clear, ESP32 closes the gate.
11. If a vehicle appears during closing, ESP32 stops closing and opens again.

## Raspberry Pi setup

Install Mosquitto:

```bash
sudo apt update
sudo apt install -y mosquitto mosquitto-clients python3-venv
sudo systemctl enable --now mosquitto
```

Create and activate a Python environment:

```bash
cd /home/pi/smart_gate/raspberry_pi
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

Run the Raspberry Pi controller:

```bash
export $(grep -v '^#' .env | xargs)
python3 gate_controller.py
```

## ESP32 setup

Open the MQTT firmware in Arduino IDE:

```text
firmware/esp32_mqtt_gate_controller/esp32_mqtt_gate_controller.ino
```

Set these values before upload:

```cpp
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* RASPBERRY_MQTT_IP = "RASPBERRY_PI_IP_ADDRESS";
```

Upload the firmware to the ESP32.

The standalone firmware is included for testing the gate without Raspberry Pi, WiFi, MQTT, OCR, or Hermes:

```text
firmware/esp32_standalone_gate_controller/esp32_standalone_gate_controller.ino
```

## MCP server setup

Install dependencies:

```bash
cd /home/pi/smart_gate/mcp_server
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

Run the MCP server:

```bash
export MQTT_HOST=localhost
export SMART_GATE_DATA_DIR=/home/pi/hermes-timovi/tim09-root/data
python3 mcp_server.py
```

The MCP server exposes these tools:

| Tool | Purpose |
|---|---|
| `list_allowed_plates()` | Lists all allowed plates |
| `add_allowed_plate(plate)` | Adds a plate to the database |
| `remove_allowed_plate(plate)` | Removes a plate from the database |
| `get_last_pending_plate()` | Returns the last unknown/similar plate |
| `allow_last_pending_once()` | Opens the gate once without saving the plate |
| `add_last_pending_and_open()` | Saves the pending plate and opens the gate |
| `deny_last_pending_plate()` | Denies the pending plate |
| `open_gate()` | Sends `OPEN` manually |
| `close_gate()` | Sends `CLOSE` manually |
| `stop_gate()` | Sends `STOP` manually |

## Manual MQTT testing

Subscribe to all Smart Gate topics:

```bash
mosquitto_sub -h <RASPBERRY_PI_IP> -t "smart_gate/#" -v
```

Send test commands:

```bash
mosquitto_pub -h <RASPBERRY_PI_IP> -t "smart_gate/cmd" -m "OPEN"
mosquitto_pub -h <RASPBERRY_PI_IP> -t "smart_gate/cmd" -m "CLOSE"
mosquitto_pub -h <RASPBERRY_PI_IP> -t "smart_gate/cmd" -m "STOP"
mosquitto_pub -h <RASPBERRY_PI_IP> -t "smart_gate/cmd" -m "STATUS"
```

Simulate vehicle arrival:

```bash
mosquitto_pub -h <RASPBERRY_PI_IP> -t "smart_gate/event" -m "CAR_AT_GATE"
```

## Data files

`data/allowed_plates.json` is the only persistent allowed-plates database. It starts as an empty list:

```json
[]
```

`pending_plate.json` is generated at runtime when the OCR result is unknown or only similar to an allowed plate. It is not committed as a live data file because it is temporary runtime state.


- OCR quality depends on lighting, camera position, and plate visibility.
- The MQTT broker IP must match the Raspberry Pi network address.
- The JSON database is suitable for a demonstration, not for production access control.
- A production version would require stronger authentication, audit logs, input validation, and safer physical motor-control hardware.
