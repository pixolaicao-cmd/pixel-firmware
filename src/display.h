#pragma once
/**
 * Pixel AI — 显示层
 * 目标：M5Stack CoreS3（2" IPS LCD 320×240，由 M5Unified 驱动）
 *
 * 接口与旧 OLED 版保持兼容，调用方无需改动。
 */

#include <M5Unified.h>
#include "config.h"

static const int LINE_H = 32;  // 行高（像素），TextSize=2 约 32px

void displayInit() {
    // M5.begin() 已在 setup() 中调用；这里只设置初始样式
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.fillScreen(TFT_BLACK);
    Serial.println("[DISP] LCD initialized (CoreS3)");
}

// 显示最多三行文字（黑底白字）
void displayShow(const char* line1,
                 const char* line2 = "",
                 const char* line3 = "") {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    // Pixel 小圆点 logo
    M5.Display.fillCircle(10, 10, 6, TFT_CYAN);
    if (line1 && strlen(line1) > 0) {
        M5.Display.setCursor(24, 4);
        M5.Display.print(line1);
    }
    if (line2 && strlen(line2) > 0) {
        M5.Display.setCursor(4, 4 + LINE_H);
        M5.Display.print(line2);
    }
    if (line3 && strlen(line3) > 0) {
        M5.Display.setCursor(4, 4 + LINE_H * 2);
        M5.Display.print(line3);
    }
}

void displayShow(const String& line1,
                 const String& line2 = "",
                 const String& line3 = "") {
    displayShow(line1.c_str(), line2.c_str(), line3.c_str());
}

// 清屏
void displayReset() {
    M5.Display.fillScreen(TFT_BLACK);
}

// 滚动显示长文字（兼容旧接口）
void displayScrollText(const String& text) {
    static int offset = 0;
    static unsigned long lastTime = 0;
    if (millis() - lastTime > 80) {
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextSize(2);
        M5.Display.setCursor(4, 4);
        M5.Display.print("Pixel says:");
        M5.Display.setCursor(4, 4 + LINE_H);
        M5.Display.print(text.substring(offset, offset + 18));
        if (offset + 18 < (int)text.length()) offset++;
        else offset = 0;
        lastTime = millis();
    }
}
