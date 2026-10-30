"""Generate the receiver's private header from the project-root .env file."""

import json
from pathlib import Path

Import("env")

keys = ("WIFI_SSID", "WIFI_PASSWORD", "MQTT_USERNAME", "MQTT_PASSWORD",
        "MQTT_BROKER", "MQTT_PORT", "MQTT_TOPIC")
source = Path(env.subst("$PROJECT_DIR")) / ".env"
if not source.is_file():
    raise RuntimeError("Missing .env: copy .env.example to .env and fill in the credentials.")

values = {}
for number, line in enumerate(source.read_text().splitlines(), 1):
    line = line.strip()
    if not line or line.startswith("#"):
        continue
    key, separator, value = line.partition("=")
    key = key.strip()
    if not separator:
        raise RuntimeError(f"Invalid .env entry on line {number}; expected KEY=value.")
    if key not in keys:
        continue
    value = value.strip()
    if value.startswith(("'", '"')):
        if len(value) < 2 or value[-1] != value[0]:
            raise RuntimeError(f"Unclosed quote in .env on line {number}.")
        value = value[1:-1]
    values[key] = value

missing = [key for key in keys if key not in values]
if missing:
    raise RuntimeError("Missing .env keys: " + ", ".join(missing))

if not values["MQTT_PORT"].isascii() or not values["MQTT_PORT"].isdecimal():
    raise RuntimeError("MQTT_PORT must be an integer between 1 and 65535.")
port = int(values["MQTT_PORT"])
if not 1 <= port <= 65535:
    raise RuntimeError("MQTT_PORT must be an integer between 1 and 65535.")

directory = Path(env.subst("$BUILD_DIR")) / "generated"
directory.mkdir(parents=True, exist_ok=True)
header = directory / "receiver_secrets.h"
content = "#pragma once\n" + "".join(
    f"#define {key} {port if key == 'MQTT_PORT' else json.dumps(values[key], ensure_ascii=False)}\n"
    for key in keys
)
if not header.exists() or header.read_text() != content:
    header.write_text(content)
env.Append(CPPPATH=[str(directory)])
