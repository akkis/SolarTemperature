
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// OLED 128x64 με controller SH1106 και I2C
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
    U8G2_R0,
    U8X8_PIN_NONE
);

void setup() {
    Serial.begin(115200);

    // SDA = GPIO21, SCL = GPIO22
    Wire.begin(21, 22);

    display.begin();
    display.clearBuffer();

    display.setContrast(60);

    display.setFont(u8g2_font_6x12_tf);
    display.drawStr(0, 7, "Solar Water Heater");
    display.drawHLine(0, 19, 128);

    display.setFont(u8g2_font_ncenB14_tr);
    display.drawStr(0, 43, "Hello OLED!");

    display.setFont(u8g2_font_6x12_tf);
    display.drawStr(0, 61, "ESP32 + I2C");

    display.sendBuffer();

    Serial.println("OLED test completed");
}

void loop() {
    // Δεν χρειάζεται κάτι άλλο για τη δοκιμή.
}
