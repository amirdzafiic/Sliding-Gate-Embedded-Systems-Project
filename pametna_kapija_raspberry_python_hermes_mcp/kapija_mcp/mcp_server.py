from mcp.server.fastmcp import FastMCP
import paho.mqtt.publish as publish
from allowed_plates import (
    load_allowed,
    add_allowed,
    remove_allowed,
    load_pending,
    clear_pending,
)

MQTT_HOST = "localhost"
TOPIC_CMD = "kapija/cmd"

mcp = FastMCP("Pametna kapija")


def mqtt_command(cmd: str) -> None:
    publish.single(TOPIC_CMD, cmd, hostname=MQTT_HOST)


@mcp.tool()
def lista_dozvoljenih_tablica() -> list[str]:
    """Vraca listu registarskih tablica koje se automatski pustaju kroz kapiju."""
    return load_allowed()


@mcp.tool()
def dodaj_dozvoljenu_tablicu(tablica: str) -> dict:
    """Dodaje registarsku tablicu u bazu dozvoljenih tablica."""
    normalized = add_allowed(tablica)
    return {"status": "DODANO", "tablica": normalized, "lista": load_allowed()}


@mcp.tool()
def obrisi_dozvoljenu_tablicu(tablica: str) -> dict:
    """Brise jednu registarsku tablicu iz baze dozvoljenih tablica."""
    return remove_allowed(tablica)


@mcp.tool()
def provjeri_zadnju_nepoznatu_tablicu() -> dict:
    """Vraca zadnju registarsku tablicu koja je ocitana, ali nije bila u bazi."""
    return load_pending()


@mcp.tool()
def pusti_zadnju_nepoznatu_jednom() -> dict:
    """Otvara kapiju za zadnju nepoznatu tablicu samo ovaj put, bez dodavanja u bazu."""
    pending = load_pending()
    plate = pending.get("tablica", "")

    if not plate:
        return {"status": "NEMA_NEPOZNATE_TABLICE"}

    mqtt_command("OPEN")
    clear_pending()
    return {"status": "PUSTENO_JEDNOM", "tablica": plate}


@mcp.tool()
def dodaj_zadnju_nepoznatu_i_pusti() -> dict:
    """Dodaje zadnju nepoznatu tablicu u bazu dozvoljenih i otvara kapiju."""
    pending = load_pending()
    plate = pending.get("tablica", "")

    if not plate:
        return {"status": "NEMA_NEPOZNATE_TABLICE"}

    add_allowed(plate)
    mqtt_command("OPEN")
    clear_pending()
    return {"status": "DODANO_I_PUSTENO", "tablica": plate, "lista": load_allowed()}


@mcp.tool()
def odbij_zadnju_nepoznatu_tablicu() -> dict:
    """Odbija zadnju nepoznatu tablicu i brise pending bez otvaranja kapije."""
    pending = load_pending()
    plate = pending.get("tablica", "")

    if not plate:
        return {"status": "NEMA_NEPOZNATE_TABLICE"}

    mqtt_command("DENY")
    clear_pending()
    return {"status": "ODBIJENO", "tablica": plate}


@mcp.tool()
def otvori_kapiju() -> str:
    """Salje MQTT komandu OPEN prema ESP32 kontroleru kapije."""
    mqtt_command("OPEN")
    return "Komanda OPEN poslana."


@mcp.tool()
def zatvori_kapiju() -> str:
    """Salje MQTT komandu CLOSE prema ESP32 kontroleru kapije."""
    mqtt_command("CLOSE")
    return "Komanda CLOSE poslana."


@mcp.tool()
def zaustavi_kapiju() -> str:
    """Salje MQTT komandu STOP prema ESP32 kontroleru kapije."""
    mqtt_command("STOP")
    return "Komanda STOP poslana."


if __name__ == "__main__":
    mcp.run()
