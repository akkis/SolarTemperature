
#include <WiFi.h>
#include <Wire.h>
#include <SPI.h>
#include <PubSubClient.h>
#include <LoRa.h>
#include <U8g2lib.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#include "receiver_secrets.h"

// LoRa pins
#define LORA_SS 4
#define LORA_RST 5
#define LORA_DIO0 2

// OLED I2C pins
#define OLED_SDA 21
#define OLED_SCL 22

// Timing
const unsigned long DISPLAY_UPDATE_INTERVAL = 1000;
const unsigned long SIGNAL_TIMEOUT = 15UL * 60UL * 1000UL;
const unsigned long RECONNECT_INTERVAL = 5000;

// OLED SH1106 128x64
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
    U8G2_R0,
    U8X8_PIN_NONE
);

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastReconnectAttempt = 0;
unsigned long lastPacketTime = 0;
unsigned long lastDisplayUpdate = 0;

bool hasReceivedTemperature = false;
bool hasValidTemperature = false;

float lastTemperature = NAN;

// Time of last valid LoRa temperature
String lastReceivedTime = "--:--";


// --------------------------------------------------
// Time / NTP
// --------------------------------------------------

void setupTime() {
  // Greece: EET (UTC+2) / EEST (UTC+3)
  // Automatic daylight saving time changes
  configTzTime(
      "EET-2EEST,M3.5.0/3,M10.5.0/4",
      "pool.ntp.org",
      "time.google.com"
  );

  Serial.println("NTP time synchronization started");
}

String getCurrentTime() {
  struct tm timeinfo;

  // Non-blocking-ish: wait at most 100 ms
  if (!getLocalTime(&timeinfo, 100)) {
    return "--:--";
  }

  char buffer[6];
  strftime(buffer, sizeof(buffer), "%H:%M", &timeinfo);

  return String(buffer);
}


// --------------------------------------------------
// OLED helpers
// --------------------------------------------------

void drawTemperature(float temperature, int baseline) {
  char tempText[20];
  snprintf(tempText, sizeof(tempText), "%.1f", temperature);

  display.setFont(u8g2_font_logisoso24_tn);
  display.drawStr(0, baseline, tempText);

  int width = display.getStrWidth(tempText);

  // Degree symbol
  display.drawCircle(width + 5, baseline - 18, 2);

  display.setFont(u8g2_font_6x12_tf);
  display.drawStr(width + 11, baseline, "C");
}

void drawLastReceivedTime() {
  String text = "Last RX: " + lastReceivedTime;

  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(0, 63, text.c_str());
}


// --------------------------------------------------
// OLED display
// --------------------------------------------------

void drawDisplay() {
  display.clearBuffer();

  // Header
  display.setFont(u8g2_font_6x12_tf);
  display.drawStr(0, 10, "SOLAR WATER HEATER");
  display.drawHLine(0, 14, 128);

  // No valid temperature received yet
  if (!hasReceivedTemperature) {
    display.drawStr(0, 34, "Waiting for LoRa...");
    display.drawStr(0, 51, "No data received");
    display.sendBuffer();
    return;
  }

  // Signal timeout (based on last VALID temperature)
  bool signalLost =
      (millis() - lastPacketTime) >= SIGNAL_TIMEOUT;

  if (signalLost) {
    display.setFont(u8g2_font_7x14B_tf);
    display.drawStr(0, 27, "NO SIGNAL");

    if (hasValidTemperature) {
      drawTemperature(lastTemperature, 52);
      drawLastReceivedTime();
    } else {
      display.setFont(u8g2_font_6x12_tf);
      display.drawStr(0, 47, "No valid data");
    }

    display.sendBuffer();
    return;
  }

  // Invalid temperature state
  if (!hasValidTemperature) {
    display.setFont(u8g2_font_6x12_tf);
    display.drawStr(0, 32, "Invalid temperature");
    display.drawStr(0, 49, "Check sender data");
    display.sendBuffer();
    return;
  }

  // Temperature below 40 C
  if (lastTemperature < 40.0f) {
    display.setFont(u8g2_font_6x12_tf);
    display.drawStr(0, 25, "Temperature");
  }

  // Temperature 40 C to below 60 C
  else if (lastTemperature < 60.0f) {
    display.setFont(u8g2_font_6x12_tf);
    display.drawStr(0, 25, "Warm water");
  }

  // Temperature 60 C and above
  else {
    display.setFont(u8g2_font_7x14B_tf);
    display.drawStr(0, 26, "Hot water!");
  }

  drawTemperature(lastTemperature, 52);
  drawLastReceivedTime();

  display.sendBuffer();
}


// --------------------------------------------------
// Wi-Fi
// --------------------------------------------------

void setupWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  Serial.println(WiFi.localIP());
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");

    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;

    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi reconnected");
    } else {
      Serial.println("\nFailed to reconnect WiFi");
    }
  }
}


// --------------------------------------------------
// MQTT
// --------------------------------------------------

void setupMQTT() {
  client.setServer(MQTT_BROKER, MQTT_PORT);

  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");

    if (client.connect(
          "LoRaReceiverClient",
          MQTT_USERNAME,
          MQTT_PASSWORD)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

boolean reconnectMQTT() {
  if (client.connect(
        "LoRaReceiverClient",
        MQTT_USERNAME,
        MQTT_PASSWORD)) {
    Serial.println("MQTT reconnected");
    return true;
  }

  return false;
}


// --------------------------------------------------
// Setup
// --------------------------------------------------

void setup() {
  Serial.begin(115200);

  // Initialize OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin();
  

  // Keep existing low contrast setting
  display.setContrast(1);

  display.clearBuffer();
  display.setFont(u8g2_font_6x12_tf);
  display.drawStr(0, 25, "Solar Water Heater");
  display.drawStr(0, 45, "Starting...");
  display.sendBuffer();

  // Wi-Fi
  setupWiFi();

  // Start NTP synchronization
  setupTime();

  // MQTT
  setupMQTT();

  // LoRa
  Serial.println("Initializing LoRa...");

  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(433E6)) {
    Serial.println("Starting LoRa failed!");

    display.clearBuffer();
    display.setFont(u8g2_font_6x12_tf);
    display.drawStr(0, 25, "LoRa ERROR!");
    display.drawStr(0, 45, "Check wiring");
    display.sendBuffer();

    while (1) {
      delay(1000);
    }
  }

  LoRa.setSyncWord(0x12);

  Serial.println("LoRa receiver initialized.");

  display.clearBuffer();
  display.setFont(u8g2_font_6x12_tf);
  display.drawStr(0, 25, "LoRa ready");
  display.drawStr(0, 45, "Waiting for data...");
  display.sendBuffer();
}


// --------------------------------------------------
// Main loop
// --------------------------------------------------

void loop() {
  checkWiFiConnection();

  // MQTT reconnect handling
  if (!client.connected()) {
    unsigned long now = millis();

    if (now - lastReconnectAttempt > RECONNECT_INTERVAL) {
      lastReconnectAttempt = now;

      Serial.println(
          "MQTT disconnected. Attempting to reconnect...");

      if (reconnectMQTT()) {
        lastReconnectAttempt = 0;
      } else {
        Serial.print("MQTT reconnection failed, rc=");
        Serial.println(client.state());
      }
    }
  } else {
    client.loop();
  }

  // Receive LoRa packet
  int packetSize = LoRa.parsePacket();

  if (packetSize) {
    String received = "";

    while (LoRa.available()) {
      received += (char)LoRa.read();
    }

    received.trim();

    Serial.println("Received via LoRa: " + received);

    // Validate numeric payload
    char *endPtr = nullptr;
    const char *startPtr = received.c_str();

    float temperature = strtof(startPtr, &endPtr);

    while (endPtr && *endPtr == ' ') {
      endPtr++;
    }

    bool valid =
        endPtr != startPtr &&
        endPtr != nullptr &&
        *endPtr == '\0' &&
        isfinite(temperature);

    if (valid) {
      // Update only on valid temperature
      lastTemperature = temperature;
      hasValidTemperature = true;
      hasReceivedTemperature = true;

      lastPacketTime = millis();

      // Record the actual local time of reception
      lastReceivedTime = getCurrentTime();

      Serial.print("Temperature: ");
      Serial.println(lastTemperature);

      Serial.print("Last received at: ");
      Serial.println(lastReceivedTime);

    } else {
      Serial.println("Invalid temperature payload");

      // Preserve last valid temperature and timestamp.
      // Invalid packets do NOT reset signal timeout.
    }

    // Publish original payload to MQTT as before
    if (client.connected()) {
      if (client.publish(MQTT_TOPIC, received.c_str())) {
        Serial.println("Published to MQTT successfully");
      } else {
        Serial.println("Failed to publish to MQTT");
      }
    } else {
      Serial.println(
          "MQTT not connected - message not published");
    }

    // Immediately refresh display on reception
    drawDisplay();
  }

  // Refresh OLED once per second
  unsigned long now = millis();

  if (now - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    lastDisplayUpdate = now;
    drawDisplay();
  }
}
