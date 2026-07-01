from __future__ import annotations

import json
import os
import re
from pathlib import Path

DATA_DIR = Path(os.getenv("SMART_GATE_DATA_DIR", "/root/smart_gate_data"))
DATA_DIR.mkdir(parents=True, exist_ok=True)

PLATES_FILE = DATA_DIR / "allowed_plates.json"
PENDING_FILE = DATA_DIR / "pending_plate.json"


def normalize_plate(plate: str) -> str:
    """Normalize a license plate by keeping only uppercase letters and digits."""
    plate = plate.upper()
    return re.sub(r"[^A-Z0-9]", "", plate)


def load_allowed() -> list[str]:
    """Load all license plates that are allowed to open the gate automatically."""
    if not PLATES_FILE.exists():
        PLATES_FILE.write_text("[]", encoding="utf-8")
    return json.loads(PLATES_FILE.read_text(encoding="utf-8"))


def save_allowed(plates: list[str]) -> None:
    """Save a normalized and sorted list of allowed license plates."""
    unique = sorted(set(normalize_plate(p) for p in plates if normalize_plate(p)))
    PLATES_FILE.write_text(json.dumps(unique, indent=2), encoding="utf-8")


def add_allowed(plate: str) -> str:
    """Add one license plate to the allowed list."""
    plate = normalize_plate(plate)
    plates = load_allowed()
    if plate and plate not in plates:
        plates.append(plate)
    save_allowed(plates)
    return plate


def remove_allowed(plate: str) -> dict:
    """Remove one license plate from the allowed list."""
    plate = normalize_plate(plate)
    plates = load_allowed()

    if plate not in plates:
        return {"status": "NOT_FOUND", "plate": plate, "allowed_plates": plates}

    plates = [p for p in plates if normalize_plate(p) != plate]
    save_allowed(plates)
    return {"status": "DELETED", "plate": plate, "allowed_plates": load_allowed()}


def load_pending() -> dict:
    """Load the last unknown or similar plate that is waiting for a user decision."""
    if not PENDING_FILE.exists():
        return {
            "plate": "",
            "ocr_confidence": 0.0,
            "status": "NO_PENDING_PLATE",
            "suggested_plate": "",
            "similarity": 0.0,
        }
    return json.loads(PENDING_FILE.read_text(encoding="utf-8"))


def clear_pending() -> None:
    """Remove the pending plate decision file."""
    if PENDING_FILE.exists():
        PENDING_FILE.unlink()
