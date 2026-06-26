import json
import re
from pathlib import Path

DATA_DIR = Path("/root/kapija_data")
DATA_DIR.mkdir(parents=True, exist_ok=True)

PLATES_FILE = DATA_DIR / "allowed_plates.json"
PENDING_FILE = DATA_DIR / "pending_plate.json"


def normalize_plate(plate: str) -> str:
    plate = plate.upper()
    plate = re.sub(r"[^A-Z0-9]", "", plate)
    return plate


def load_allowed() -> list[str]:
    if not PLATES_FILE.exists():
        PLATES_FILE.write_text("[]", encoding="utf-8")
    return json.loads(PLATES_FILE.read_text(encoding="utf-8"))


def save_allowed(plates: list[str]) -> None:
    unique = sorted(set(normalize_plate(p) for p in plates if normalize_plate(p)))
    PLATES_FILE.write_text(json.dumps(unique, indent=2), encoding="utf-8")


def add_allowed(plate: str) -> str:
    plate = normalize_plate(plate)
    plates = load_allowed()
    if plate and plate not in plates:
        plates.append(plate)
    save_allowed(plates)
    return plate


def remove_allowed(plate: str) -> dict:
    plate = normalize_plate(plate)
    plates = load_allowed()

    if plate not in plates:
        return {"status": "NIJE_PRONADJENA", "tablica": plate, "lista": plates}

    plates = [p for p in plates if normalize_plate(p) != plate]
    save_allowed(plates)
    return {"status": "OBRISANO", "tablica": plate, "lista": load_allowed()}


def load_pending() -> dict:
    if not PENDING_FILE.exists():
        return {
            "tablica": "",
            "pouzdanost_ocr": 0.0,
            "status": "NEMA_PENDING_TABLICE",
            "predlozena_tablica": "",
            "slicnost": 0.0,
        }
    return json.loads(PENDING_FILE.read_text(encoding="utf-8"))


def clear_pending() -> None:
    if PENDING_FILE.exists():
        PENDING_FILE.unlink()
