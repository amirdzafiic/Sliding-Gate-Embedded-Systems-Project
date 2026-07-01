from __future__ import annotations

import os

import paho.mqtt.client as mqtt

from license_plate_ocr import read_license_plate
from plate_database import classify_plate, save_pending
from telegram_notifier import send_telegram_message

MQTT_HOST = os.getenv("MQTT_HOST", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
TOPIC_EVENT = os.getenv("TOPIC_EVENT", "smart_gate/event")
TOPIC_CMD = os.getenv("TOPIC_CMD", "smart_gate/cmd")


def publish_command(client: mqtt.Client, command: str) -> None:
    """Publish a gate command to the ESP32 controller."""
    client.publish(TOPIC_CMD, command)


def on_connect(client, userdata, flags, rc):
    print("Connected to MQTT broker, rc =", rc)
    client.subscribe(TOPIC_EVENT)
    print("Listening for ESP32 events on:", TOPIC_EVENT)


def on_message(client, userdata, msg):
    event = msg.payload.decode("utf-8")
    print("ESP32 event:", event)

    if event != "CAR_AT_GATE":
        return

    print("Vehicle detected at the entrance. Starting camera capture and OCR.")

    try:
        result = read_license_plate()
    except Exception as exc:
        print("License-plate reading error:", exc)
        publish_command(client, "DENY")
        send_telegram_message(
            "License-plate reading failed.\n\n"
            f"Details: {exc}\n\n"
            "The gate was not opened."
        )
        return

    plate = result.get("plate", "")
    confidence = result.get("confidence", 0.0)

    print("Detected plate:", plate)
    print("OCR confidence:", confidence)

    match_info = classify_plate(plate)
    status = match_info["status"]
    print("Plate status:", status)

    if status == "EXACT_MATCH":
        print("EXACT_MATCH: Plate exists in the database. Sending OPEN.")
        publish_command(client, "OPEN")
        send_telegram_message(
            "Allowed license plate detected.\n\n"
            f"Plate: {plate}\n"
            f"OCR confidence: {confidence}\n\n"
            "The gate was opened automatically."
        )
        return

    if status == "POSSIBLE_MATCH":
        print("POSSIBLE_MATCH: Plate is not an exact match, but it is similar to an allowed plate.")
        save_pending(plate, confidence, match_info)
        send_telegram_message(
            "Similar license plate detected.\n\n"
            f"Detected: {match_info['plate']}\n"
            f"Closest allowed plate: {match_info['suggested_plate']}\n"
            f"Similarity: {match_info['similarity']}\n"
            f"OCR confidence: {confidence}\n\n"
            "Choose one action through Hermes/MCP:\n"
            "1. allow once\n"
            "2. add and allow\n"
            "3. deny"
        )
        return

    if status == "NO_MATCH":
        print("NO_MATCH: Plate does not exist in the database.")
        save_pending(plate, confidence, match_info)
        send_telegram_message(
            "Unknown license plate detected.\n\n"
            f"Plate: {plate}\n"
            f"OCR confidence: {confidence}\n\n"
            "Choose one action through Hermes/MCP:\n"
            "1. allow once\n"
            "2. add and allow\n"
            "3. deny"
        )
        return

    if status == "NO_TEXT":
        print("NO_TEXT: OCR did not read a license plate.")
        publish_command(client, "DENY")
        send_telegram_message(
            "OCR did not read a license plate.\n\n"
            "The gate was not opened. Capture another image or check the vehicle manually."
        )
        return

    print("Unknown plate status:", status)
    publish_command(client, "DENY")
    send_telegram_message(
        "Unknown license-plate status.\n\n"
        f"Status: {status}\n"
        f"Plate: {plate}\n\n"
        "The gate was not opened."
    )


client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message
client.connect(MQTT_HOST, MQTT_PORT, 60)

print("gate_controller.py started.")
print("Waiting for MQTT event CAR_AT_GATE...")
client.loop_forever()
