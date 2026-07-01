from __future__ import annotations

import os

import paho.mqtt.publish as publish
from mcp.server.fastmcp import FastMCP

from plate_database import (
    add_allowed,
    clear_pending,
    load_allowed,
    load_pending,
    remove_allowed,
)

MQTT_HOST = os.getenv("MQTT_HOST", "localhost")
TOPIC_CMD = os.getenv("TOPIC_CMD", "smart_gate/cmd")

mcp = FastMCP("Smart Gate")


def mqtt_command(command: str) -> None:
    """Send one command to the ESP32 gate controller through MQTT."""
    publish.single(TOPIC_CMD, command, hostname=MQTT_HOST)


@mcp.tool()
def list_allowed_plates() -> list[str]:
    """Return all license plates that are allowed to open the gate automatically."""
    return load_allowed()


@mcp.tool()
def add_allowed_plate(plate: str) -> dict:
    """Add a license plate to the allowed-plates database."""
    normalized = add_allowed(plate)
    return {"status": "ADDED", "plate": normalized, "allowed_plates": load_allowed()}


@mcp.tool()
def remove_allowed_plate(plate: str) -> dict:
    """Remove one license plate from the allowed-plates database."""
    return remove_allowed(plate)


@mcp.tool()
def get_last_pending_plate() -> dict:
    """Return the last license plate that was read but not automatically allowed."""
    return load_pending()


@mcp.tool()
def allow_last_pending_once() -> dict:
    """Open the gate for the last pending plate once, without adding it to the database."""
    pending = load_pending()
    plate = pending.get("plate", "")

    if not plate:
        return {"status": "NO_PENDING_PLATE"}

    mqtt_command("OPEN")
    clear_pending()
    return {"status": "ALLOWED_ONCE", "plate": plate}


@mcp.tool()
def add_last_pending_and_open() -> dict:
    """Add the last pending plate to the allowed list and open the gate."""
    pending = load_pending()
    plate = pending.get("plate", "")

    if not plate:
        return {"status": "NO_PENDING_PLATE"}

    add_allowed(plate)
    mqtt_command("OPEN")
    clear_pending()
    return {"status": "ADDED_AND_OPENED", "plate": plate, "allowed_plates": load_allowed()}


@mcp.tool()
def deny_last_pending_plate() -> dict:
    """Deny the last pending plate and clear the pending decision without opening the gate."""
    pending = load_pending()
    plate = pending.get("plate", "")

    if not plate:
        return {"status": "NO_PENDING_PLATE"}

    mqtt_command("DENY")
    clear_pending()
    return {"status": "DENIED", "plate": plate}


@mcp.tool()
def open_gate() -> str:
    """Send the OPEN command to the ESP32 gate controller."""
    mqtt_command("OPEN")
    return "OPEN command sent."


@mcp.tool()
def close_gate() -> str:
    """Send the CLOSE command to the ESP32 gate controller."""
    mqtt_command("CLOSE")
    return "CLOSE command sent."


@mcp.tool()
def stop_gate() -> str:
    """Send the STOP command to the ESP32 gate controller."""
    mqtt_command("STOP")
    return "STOP command sent."


if __name__ == "__main__":
    mcp.run()
