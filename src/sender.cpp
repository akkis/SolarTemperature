
#include <OneWire.h>
#include <DallasTemperature.h>
#include <SPI.h>
#include <LoRa.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

// DS18B20
#define ONE_WIRE_BUS 14

// LoRa RA-02 (SX1278)
#define LORA_SS   4
#define LORA_RST  5
#define LORA_DIO0 2

// Push button: GPIO33 -> GND
#define BUTTON_PIN GPIO_NUM_33

// Send every 10 minutes
#define SLEEP_TIME_DURATION (10ULL * 60ULL * 1000000ULL)

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);


// --------------------------------------------------
// Identify wake-up reason
// --------------------------------------------------

void printWakeupReason() {
  esp_sleep_wakeup_cause_t reason =
      esp_sleep_get_wakeup_cause();

  switch (reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("Wake-up: BUTTON");
      break;

    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("Wake-up: TIMER");
      break;

    default:
      Serial.println("Wake-up: POWER ON / RESET");
      break;
  }
}


// --------------------------------------------------
// Setup
// --------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("Solar Temperature LoRa Sender");

  // Restore GPIO33 to normal GPIO operation
  rtc_gpio_deinit(BUTTON_PIN);

  // Button is active LOW
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  printWakeupReason();

  // Initialize DS18B20
  sensors.begin();

  // Initialize LoRa
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(433E6)) {
    Serial.println("ERROR: Starting LoRa failed!");
    Serial.println("Check wiring and RA-02 power.");

    while (true) {
      delay(1000);
    }
  }

  LoRa.setSyncWord(0x12);

  Serial.println("LoRa initialized.");

  // Read temperature
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);

  if (tempC == DEVICE_DISCONNECTED_C ||
      tempC < -55.0 ||
      tempC > 125.0) {

    Serial.println("ERROR: DS18B20 not detected.");

  } else {

    char payload[16];
    snprintf(payload, sizeof(payload), "%.2f", tempC);

    Serial.print("Temperature: ");
    Serial.print(payload);
    Serial.println(" C");

    LoRa.beginPacket();
    LoRa.print(payload);

    int result = LoRa.endPacket();

    if (result == 1) {
      Serial.println("LoRa packet sent successfully.");
    } else {
      Serial.println("ERROR: LoRa packet transmission failed.");
    }
  }

  // Put LoRa module to sleep
  LoRa.sleep();

  // Wait until button is released.
  // Prevents repeated immediate wake-ups.
  if (digitalRead(BUTTON_PIN) == LOW) {
    Serial.println("Waiting for button release...");

    while (digitalRead(BUTTON_PIN) == LOW) {
      delay(20);
    }

    delay(100); // Debounce
  }

  // Timer wake-up after 10 minutes
  esp_sleep_enable_timer_wakeup(SLEEP_TIME_DURATION);

  // Wake-up when GPIO33 becomes LOW
  esp_sleep_enable_ext0_wakeup(BUTTON_PIN, 0);

  Serial.println("Entering deep sleep...");
  Serial.println("Wake-up: 10-minute timer or button");
  Serial.flush();

  esp_deep_sleep_start();
}


// --------------------------------------------------
// Loop
// --------------------------------------------------

void loop() {
  // Not used: ESP32 restarts after deep sleep.
}
