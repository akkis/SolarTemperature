# Solar Temperature

An ESP32 solar water heater temperature monitor using LoRa. A battery-oriented sender reads a DS18B20 sensor and transmits the temperature to a receiver, which displays it on an OLED and forwards it to an MQTT broker over Wi-Fi.

```text
DS18B20 → ESP32 sender → LoRa (433 MHz) → ESP32 receiver → MQTT broker
                                               ↓
                                         SH1106 OLED
```

## Hardware

- Two ESP32 development boards (`esp32dev`).
- Two RA-02 / SX1278 LoRa modules and suitable antennas.
- One DS18B20 temperature sensor for the sender.
- One SH1106 128×64 I2C OLED for the receiver.
- An optional push button to wake the sender manually.
- A Wi-Fi network and an MQTT broker accessible to the receiver.

### Wiring

Both boards use these LoRa connections. SPI uses the ESP32 Arduino default pins because the code does not override them.

| LoRa signal | ESP32 GPIO |
| --- | --- |
| SCK | 18 |
| MISO | 19 |
| MOSI | 23 |
| NSS / SS | 4 |
| RST | 5 |
| DIO0 | 2 |

| Board | Component connection | ESP32 GPIO |
| --- | --- | --- |
| Sender | DS18B20 data | 14 |
| Sender | Wake button, other terminal to GND | 33 |
| Receiver | OLED SDA | 21 |
| Receiver | OLED SCL | 22 |

Use a common ground and power the RA-02 modules from 3.3 V. For a three-wire DS18B20 connection, use a data pull-up resistor, typically 4.7 kΩ to 3.3 V, unless your sensor board already provides one.

## Firmware behavior

### Sender

- Reads the temperature on startup and on every wake-up.
- Sends a plain-text Celsius value with two decimal places, such as `52.38`.
- Skips transmission if the sensor is disconnected or the reading is outside −55 to 125 °C.
- Puts the radio and ESP32 to sleep after each reading.
- Wakes every 10 minutes or when the button connects GPIO33 to ground.

### Receiver

- Connects to Wi-Fi and MQTT, then starts listening for LoRa packets.
- Displays the latest valid temperature and its local reception time.
- Shows `Warm water` at 40 °C and above, and `Hot water!` at 60 °C and above.
- Shows `NO SIGNAL` after 15 minutes without a valid temperature, keeping the last valid reading visible.
- Ignores invalid payloads for display updates and signal timeout tracking.
- Publishes the trimmed received payload to MQTT when connected, including payloads that fail temperature validation.
- Attempts to reconnect after Wi-Fi or MQTT connection loss.

The receiver uses NTP with Greece's EET/EEST timezone and automatic daylight saving changes. Until time synchronization succeeds, the reception time may appear as `--:--`.

Both radios use `433E6` and sync word `0x12`. If you change these settings, update both firmware files. The sync word is a radio setting, not an authentication key.

## Configuration

Copy the example configuration in the repository root:

```sh
cp .env.example .env
```

Edit `.env` with your network and MQTT settings:

```dotenv
WIFI_SSID="your-wifi-name"
WIFI_PASSWORD="your-wifi-password"
MQTT_USERNAME="your-mqtt-username"
MQTT_PASSWORD="your-mqtt-password"
MQTT_BROKER="your-broker-ip-address"
MQTT_PORT=1883
MQTT_TOPIC="lora/solar/temperature"
```

All seven keys are required for a receiver build. `MQTT_PORT` must be an integer from 1 to 65535. Values may be unquoted or enclosed in matching single or double quotes. Quoted values are literal: the loader does not expand variables or process escape sequences. Put comments on separate lines.

PlatformIO runs `scripts/load_env.py` before building the receiver. The script generates `.pio/build/receiver/generated/receiver_secrets.h` and adds its directory to the compiler's include path. `receiver.cpp` includes this header to access the values as C++ macros.

The ESP32 does not read `.env` at runtime. Rebuild and upload the receiver after changing configuration. Sender and OLED test builds do not require `.env`.

`.env` and `.pio/` are excluded from Git. Credentials are still embedded in the compiled receiver firmware; keep firmware and generated headers private. The current MQTT connection uses plain TCP, and LoRa payloads are sent without application-level encryption or authentication.

## Build and upload

Use PlatformIO with the Arduino framework. Dependencies are declared in `platformio.ini`: LoRa, OneWire, DallasTemperature, PubSubClient, and U8g2.

Run these commands from the repository root with the intended board connected:

```sh
# Build and upload the sender
pio run -e sender
pio run -e sender -t upload

# Build and upload the receiver
pio run -e receiver
pio run -e receiver -t upload

# Open the serial monitor (115200 baud)
pio device monitor
```

The default build environment is `receiver`, so `pio run` builds the receiver. The configured upload and monitor port is `/dev/cu.usbserial-0001`. Change `upload_port` and `monitor_port` in `platformio.ini` for your machine, or supply a port explicitly:

```sh
pio device list
pio run -e receiver -t upload --upload-port /dev/ttyUSB0
pio device monitor --port /dev/ttyUSB0 --baud 115200
```

To test the receiver's OLED separately:

```sh
pio run -e oled_test -t upload
```

This firmware displays a static test screen on the SH1106 OLED.

## Troubleshooting

- **Missing `.env` or keys:** copy `.env.example`, fill in every required key, and rebuild.
- **Receiver stays on the startup screen:** check serial output. Startup waits for Wi-Fi and MQTT connections before initializing LoRa.
- **LoRa initialization fails:** check power and wiring, especially NSS, RST, and SPI connections.
- **No temperature packets:** ensure both radios use the same frequency and sync word, and check the sender's serial output for sensor errors.
- **`NO SIGNAL`:** no valid temperature has arrived for at least 15 minutes. Check sender power, sensor wiring, and radio reception.
- **MQTT messages missing:** verify the broker address, port, credentials, and topic. Packets received while MQTT is disconnected are not queued for later delivery.

## Repository layout

| File | Purpose |
| --- | --- |
| `src/sender.cpp` | Sensor reading, LoRa transmission, and deep sleep |
| `src/receiver.cpp` | LoRa reception, OLED display, Wi-Fi, NTP, and MQTT |
| `src/oled_test.cpp` | Standalone OLED test |
| `scripts/load_env.py` | Build-time configuration header generation |
| `.env.example` | Configuration template without credentials |
| `platformio.ini` | Board, dependencies, ports, and build environments |

## Acknowledgments

This project was inspired by the original idea in
[Niiikoc/Solar_Temperature](https://github.com/Niiikoc/Solar_Temperature).
