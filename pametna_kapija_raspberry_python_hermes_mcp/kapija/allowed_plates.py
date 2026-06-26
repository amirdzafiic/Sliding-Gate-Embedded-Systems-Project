import json
import re
from pathlib import Path
from difflib import SequenceMatcher

DATA_DIR = Path("/home/pi/hermes-timovi/tim09-root/kapija_data")
DATA_DIR.mkdir(parents=True, exist_ok=True)

PLATES_FILE = DATA_DIR / "allowed_plates.json"
PENDING_FILE = DATA_DIR / "pending_plate.json"
SIMILARITY_THRESHOLD = 0.80


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


def is_allowed(plate: str) -> bool:
    plate = normalize_plate(plate)
    return plate in load_allowed()


def add_allowed(plate: str) -> str:
    plate = normalize_plate(plate)
    plates = load_allowed()
    if plate and plate not in plates:
        plates.append(plate)
    save_allowed(plates)
    return plate


def similarity(a: str, b: str) -> float:
    a = normalize_plate(a)
    b = normalize_plate(b)
    if not a or not b:
        return 0.0
    return SequenceMatcher(None, a, b).ratio()


def classify_plate(plate: str) -> dict:
    plate = normalize_plate(plate)
    allowed = load_allowed()

    if not plate:
        return {
            "status": "NO_TEXT",
            "tablica": "",
            "predlozena_tablica": "",
            "slicnost": 0.0,
        }

    if plate in allowed:
        return {
            "status": "EXACT_MATCH",
            "tablica": plate,
            "predlozena_tablica": plate,
            "slicnost": 1.0,
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
            "tablica": plate,
            "predlozena_tablica": best_plate,
            "slicnost": round(best_score, 2),
        }

    # Vazno: ako je slicnost mala, ne saljemo predlozenu tablicu.
    return {
        "status": "NO_MATCH",
        "tablica": plate,
        "predlozena_tablica": "",
        "slicnost": 0.0,
    }


def save_pending(plate: str, confidence: float = 0.0, match_info: dict | None = None) -> None:
    plate = normalize_plate(plate)
    if match_info is None:
        match_info = classify_plate(plate)

    data = {
        "tablica": plate,
        "pouzdanost_ocr": confidence,
        "status": match_info.get("status", "NO_MATCH"),
        "predlozena_tablica": match_info.get("predlozena_tablica", ""),
        "slicnost": match_info.get("slicnost", 0.0),
    }
    PENDING_FILE.write_text(json.dumps(data, indent=2), encoding="utf-8")


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
