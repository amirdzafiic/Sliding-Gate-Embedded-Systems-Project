from __future__ import annotations

import os

import requests

TELEGRAM_BOT_TOKEN = os.getenv("TELEGRAM_BOT_TOKEN", "YOUR_BOTFATHER_TOKEN")
TELEGRAM_CHAT_ID = os.getenv("TELEGRAM_CHAT_ID", "YOUR_CHAT_ID")


def send_telegram_message(text: str) -> None:
    """Send a Telegram message to the configured chat."""
    url = f"https://api.telegram.org/bot{TELEGRAM_BOT_TOKEN}/sendMessage"
    data = {
        "chat_id": TELEGRAM_CHAT_ID,
        "text": text,
    }

    try:
        response = requests.post(url, data=data, timeout=10)
        if response.status_code != 200:
            print("Telegram error:", response.text)
    except Exception as exc:
        print("Could not send Telegram message:", exc)
