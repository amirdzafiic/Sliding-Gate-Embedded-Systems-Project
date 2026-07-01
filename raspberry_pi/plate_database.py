from __future__ import annotations

import json
import os
import re
from difflib import SequenceMatcher
from pathlib import Path

DATA_DIR = Path(os.getenv("SMART_GATE_DATA_DIR", "/home/pi/hermes-timovi/tim09-root/data"))
DATA_DIR.mkdir(parents=True, exist_ok=True)

PLATES_FILE = DATA_DIR / "allowed_plates.json"
PENDING_FILE = DATA_DIR / "pending_plate.json"
SIMILARITY_THRESHOLD = 0.80


def normalize_plate(plate: str) -> str:
    """Normalize a license plate by keeping only uppercase letters and digits."""
    plate = plate.upper()
    return re.sub(r"[^A-Z0-9]", "", plate)


def load_allowed() -> list[str]:
    """Load the list of license plates that are allowed to open the gate automatically."""
    if not PLATES_FILE.exists():
        PLATES_FILE.write_text("[]", encoding="utf-8")
    return json.loads(PLATES_FILE.read_text(encoding="utf-8"))


def save_allowed(plates: list[str]) -> None:
    """Save a normalized and sorted list of allowed license plates."""
    unique = sorted(set(normalize_plate(p) for p in plates if normalize_plate(p)))
    PLATES_FILE.write_text(json.dumps(unique, indent=2), encoding="utf-8")


def is_allowed(plate: str) -> bool:
    """Return True when a normalized plate exists in the allowed list."""
    plate = normalize_plate(plate)
    return plate in load_allowed()


def add_allowed(plate: str) -> str:
    """Add a license plate to the allowed list and return the normalized value."""
    plate = normalize_plate(plate)
    plates = load_allowed()
    if plate and plate not in plates:
        plates.append(plate)
    save_allowed(plates)
    return plate


def similarity(a: str, b: str) -> float:
    """Calculate text similarity between two normalized license plates."""
    a = normalize_plate(a)
    b = normalize_plate(b)
    if not a or not b:
        return 0.0
    return SequenceMatcher(None, a, b).ratio()


def classify_plate(plate: str) -> dict:
    """Classify an OCR plate result against the local allowed-plates database."""
    plate = normalize_plate(plate)
    allowed = load_allowed()

    if not plate:
        return {
            "status": "NO_TEXT",
            "plate": "",
            "suggested_plate": "",
            "similarity": 0.0,
        }

    if plate in allowed:
        return {
            "status": "EXACT_MATCH",
            "plate": plate,
            "suggested_plate": plate,
            "similarity": 1.0,
        }

    best_plate = ""
    best_score = 0.0

    for allowed_plate in allowed:
        score = similarity(plate, allowed_plate)
        if score > best_score:
            best_score = score
            best_plate = allowed_plate

    if best_score >= SIMILARITY_THRESHOLD:
        return {
            "status": "POSSIBLE_MATCH",
            "plate": plate,
            "suggested_plate": best_plate,
            "similarity": round(best_score, 2),
        }

    return {
        "status": "NO_MATCH",
        "plate": plate,
        "suggested_plate": "",
        "similarity": 0.0,
    }


def save_pending(plate: str, confidence: float = 0.0, match_info: dict | None = None) -> None:
    """Save the last unknown or similar plate while the user decision is pending."""
    plate = normalize_plate(plate)
    if match_info is None:
        match_info = classify_plate(plate)

    data = {
        "plate": plate,
        "ocr_confidence": confidence,
        "status": match_info.get("status", "NO_MATCH"),
        "suggested_plate": match_info.get("suggested_plate", ""),
        "similarity": match_info.get("similarity", 0.0),
    }
    PENDING_FILE.write_text(json.dumps(data, indent=2), encoding="utf-8")


def load_pending() -> dict:
    """Load the current pending plate decision, if one exists."""
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
