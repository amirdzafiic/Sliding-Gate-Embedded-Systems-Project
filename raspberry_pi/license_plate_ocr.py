from __future__ import annotations

import os
import re
import time
from datetime import datetime
from pathlib import Path

import cv2
import easyocr

reader = easyocr.Reader(["en"], gpu=False)
IMAGE_PATH = Path(os.getenv("SMART_GATE_LAST_IMAGE", "/home/pi/smart_gate/last_license_plate_image.jpg"))


def normalize_plate(text: str) -> str:
    """Normalize raw OCR text into a compact license-plate string."""
    text = text.upper()
    return re.sub(r"[^A-Z0-9]", "", text)


def capture_image() -> str:
    """Capture one image from the USB camera and return the saved file path."""
    IMAGE_PATH.parent.mkdir(parents=True, exist_ok=True)

    for attempt in range(1, 6):
        print(f"Camera open attempt {attempt}/5...")
        camera = cv2.VideoCapture(0, cv2.CAP_V4L2)

        if camera.isOpened():
            time.sleep(0.5)
            for _ in range(10):
                camera.read()

            ret, frame = camera.read()
            camera.release()

            if ret and frame is not None:
                cv2.imwrite(str(IMAGE_PATH), frame)
                print("Image saved:", IMAGE_PATH)
                return str(IMAGE_PATH)

        try:
            camera.release()
        except Exception:
            pass

        time.sleep(1)

    raise RuntimeError("Camera was not found or failed to capture an image after 5 attempts.")


def read_plate_from_image(image_path: str) -> dict:
    """Run EasyOCR on an image and return the best license-plate candidate."""
    results = reader.readtext(image_path)

    if not results:
        return {
            "plate": "",
            "confidence": 0.0,
            "raw_text": "",
            "status": "NO_TEXT",
            "timestamp": datetime.now().isoformat(),
        }

    best_text = ""
    best_confidence = 0.0

    for _box, text, confidence in results:
        if confidence > best_confidence:
            best_text = text
            best_confidence = float(confidence)

    plate = normalize_plate(best_text)

    return {
        "plate": plate,
        "confidence": round(best_confidence, 2),
        "raw_text": best_text,
        "status": "OK" if plate else "EMPTY_AFTER_CLEANUP",
        "timestamp": datetime.now().isoformat(),
    }


def read_license_plate() -> dict:
    """Capture a new image and read the license plate from it."""
    image_path = capture_image()
    return read_plate_from_image(image_path)


if __name__ == "__main__":
    print(read_license_plate())
