import requests

TELEGRAM_BOT_TOKEN = "TOKEN_OD_BOTFATHERA"
TELEGRAM_CHAT_ID = "TVOJ_CHAT_ID"


def send_telegram_message(text: str) -> None:
    url = f"https://api.telegram.org/bot{TELEGRAM_BOT_TOKEN}/sendMessage"
    data = {
        "chat_id": TELEGRAM_CHAT_ID,
        "text": text,
    }

    try:
        response = requests.post(url, data=data, timeout=10)
        if response.status_code != 200:
            print("Telegram greska:", response.text)
    except Exception as exc:
        print("Ne mogu poslati Telegram poruku:", exc)
