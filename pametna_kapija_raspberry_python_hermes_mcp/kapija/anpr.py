import cv2
import easyocr
import re
import time
from datetime import datetime
from pathlib import Path

reader = easyocr.Reader(["en"], gpu=False)
IMAGE_PATH = Path("/home/pi/kapija/zadnja_slika_tablice.jpg")


def normalize_plate(text: str) -> str:
    text = text.upper()
    text = re.sub(r"[^A-Z0-9]", "", text)
    return text


def capture_image() -> str:
    for attempt in range(1, 6):
        print(f"Pokusaj otvaranja kamere {attempt}/5...")
        camera = cv2.VideoCapture(0, cv2.CAP_V4L2)

        if camera.isOpened():
            time.sleep(0.5)
            for _ in range(10):
                camera.read()

            ret, frame = camera.read()
            camera.release()

            if ret and frame is not None:
                cv2.imwrite(str(IMAGE_PATH), frame)
                print("Slika sacuvana:", IMAGE_PATH)
                return str(IMAGE_PATH)

        try:
            camera.release()
        except Exception:
            pass

        time.sleep(1)

    raise RuntimeError("Kamera nije pronadjena ili nije uspjela uslikati nakon 5 pokusaja.")


def read_plate_from_image(image_path: str) -> dict:
    results = reader.readtext(image_path)

    if not results:
        return {
            "tablica": "",
            "pouzdanost": 0.0,
            "raw_text": "",
            "status": "NO_TEXT",
            "vrijeme": datetime.now().isoformat(),
        }

    best_text = ""
    best_confidence = 0.0

    for box, text, confidence in results:
        if confidence > best_confidence:
            best_text = text
            best_confidence = float(confidence)

    plate = normalize_plate(best_text)

    return {
        "tablica": plate,
        "pouzdanost": round(best_confidence, 2),
        "raw_text": best_text,
        "status": "OK" if plate else "EMPTY_AFTER_CLEANUP",
        "vrijeme": datetime.now().isoformat(),
    }


def procitaj_tablicu() -> dict:
    image_path = capture_image()
    return read_plate_from_image(image_path)


if __name__ == "__main__":
    print(procitaj_tablicu())
