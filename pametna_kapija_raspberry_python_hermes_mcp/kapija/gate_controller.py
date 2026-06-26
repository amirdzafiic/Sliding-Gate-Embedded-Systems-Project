import paho.mqtt.client as mqtt
from anpr import procitaj_tablicu
from allowed_plates import classify_plate, save_pending
from telegram_notify import send_telegram_message

MQTT_HOST = "localhost"
MQTT_PORT = 1883
TOPIC_EVENT = "kapija/event"
TOPIC_CMD = "kapija/cmd"


def on_connect(client, userdata, flags, rc):
    print("Spojen na MQTT broker, rc =", rc)
    client.subscribe(TOPIC_EVENT)
    print("Slusam evente sa ESP32:", TOPIC_EVENT)


def on_message(client, userdata, msg):
    event = msg.payload.decode("utf-8")
    print("ESP32 event:", event)

    if event != "CAR_AT_GATE":
        return

    print("Auto je na ulazu. Pokrecem kameru i OCR.")

    try:
        result = procitaj_tablicu()
    except Exception as exc:
        print("Greska pri citanju tablice:", exc)
        client.publish(TOPIC_CMD, "DENY")
        send_telegram_message(
            "⚠️ Greška pri čitanju tablice.\n\n"
            f"Detalji: {exc}\n\n"
            "Kapija nije otvorena."
        )
        return

    plate = result.get("tablica", "")
    confidence = result.get("pouzdanost", 0.0)

    print("Procitana tablica:", plate)
    print("OCR pouzdanost:", confidence)

    match_info = classify_plate(plate)
    status = match_info["status"]
    print("Status tablice:", status)

    if status == "EXACT_MATCH":
        print("EXACT_MATCH: Tablica je u bazi. Saljem OPEN.")
        client.publish(TOPIC_CMD, "OPEN")
        send_telegram_message(
            "✅ Dozvoljena tablica očitana\n\n"
            f"Tablica: {plate}\n"
            f"OCR pouzdanost: {confidence}\n\n"
            "Kapija je automatski otvorena."
        )
        return

    if status == "POSSIBLE_MATCH":
        print("POSSIBLE_MATCH: Tablica nije tacno u bazi, ali lici na dozvoljenu.")
        save_pending(plate, confidence, match_info)
        send_telegram_message(
            "⚠️ Slična tablica očitana\n\n"
            f"Pročitano: {match_info['tablica']}\n"
            f"Najviše liči na: {match_info['predlozena_tablica']}\n"
            f"Sličnost: {match_info['slicnost']}\n"
            f"OCR pouzdanost: {confidence}\n\n"
            "Šta želiš uraditi?\n"
            "1. pusti samo ovaj put\n"
            "2. dodaj i pusti\n"
            "3. odbij\n\n"
            "Odgovori Hermesu na Telegramu jednom od ovih opcija."
        )
        return

    if status == "NO_MATCH":
        print("NO_MATCH: Tablica nije u bazi.")
        save_pending(plate, confidence, match_info)
        send_telegram_message(
            "🚫 Nepoznata tablica očitana\n\n"
            f"Tablica: {plate}\n"
            f"OCR pouzdanost: {confidence}\n\n"
            "Šta želiš uraditi?\n"
            "1. pusti samo ovaj put\n"
            "2. dodaj i pusti\n"
            "3. odbij\n\n"
            "Odgovori Hermesu na Telegramu jednom od ovih opcija."
        )
        return

    if status == "NO_TEXT":
        print("NO_TEXT: OCR nije uspio procitati tablicu.")
        client.publish(TOPIC_CMD, "DENY")
        send_telegram_message(
            "⚠️ OCR nije uspio pročitati tablicu.\n\n"
            "Kapija nije otvorena.\n"
            "Ponovi slikanje ili ručno provjeri vozilo."
        )
        return

    print("Nepoznat status:", status)
    client.publish(TOPIC_CMD, "DENY")
    send_telegram_message(
        "⚠️ Nepoznat status tablice.\n\n"
        f"Status: {status}\n"
        f"Tablica: {plate}\n\n"
        "Kapija nije otvorena."
    )


client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message
client.connect(MQTT_HOST, MQTT_PORT, 60)

print("gate_controller.py pokrenut.")
print("Cekam MQTT event CAR_AT_GATE...")
client.loop_forever()
