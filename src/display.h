#pragma once
#include <U8g2lib.h>
#include <Wire.h>
#include "config.h"

// 根据 config.h 选择 OLED 驱动
#if USE_SH1106
  U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE, OLED_SCL_PIN, OLED_SDA_PIN);
#else
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE, OLED_SCL_PIN, OLED_SDA_PIN);
#endif

void displayInit() {
    display.begin();
    display.setFont(u8g2_font_6x10_tf);
    display.setFontRefHeightExtendedText();
    display.setDrawColor(1);
    display.setFontPosTop();
    display.setFontDirection(0);
}

void displayShow(const char* line1, const char* line2 = "", const char* line3 = "") {
    display.clearBuffer();
    // Pixel logo 小点
    display.drawCircle(6, 6, 4, U8G2_DRAW_ALL);
    display.setFont(u8g2_font_6x10_tf);
    if (line1 && strlen(line1) > 0) display.drawStr(16, 0,  line1);
    if (line2 && strlen(line2) > 0) display.drawStr(0,  18, line2);
    if (line3 && strlen(line3) > 0) display.drawStr(0,  36, line3);
    display.sendBuffer();
}

// 滚动显示长文字（每次调用推进一帧）
void displayScrollText(const String& text) {
    static int offset = 0;
    static unsigned long lastTime = 0;
    if (millis() - lastTime > 80) {
        display.clearBuffer();
        display.setFont(u8g2_font_6x10_tf);
        display.drawStr(0, 0, "Pixel says:");
        // 截取可见部分（约20字符宽）
        String visible = text.substring(offset, offset + 20);
        display.drawStr(0, 18, visible.c_str());
        display.sendBuffer();
        if (offset + 20 < (int)text.length()) offset++;
        else offset = 0;
        lastTime = millis();
    }
}

void displayReset() {
    display.clearBuffer();
    display.sendBuffer();
}
