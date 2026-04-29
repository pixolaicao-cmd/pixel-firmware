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

// ── Pixel 表情：在 idle 屏中段画一个可爱的小脸，替换无效的音量条
// 状态：idle = 微笑；listening = "o"嘴；可后续扩展为 thinking / sleeping
// 单纯用基本图元，避免依赖 emoji 字库
static inline void drawPixelFace(int cx, int cy, bool listening) {
    // 脸部圆背景
    M5.Display.fillCircle(cx, cy, 38, 0x05FF /*青蓝*/);
    M5.Display.drawCircle(cx, cy, 38, TFT_WHITE);

    // 两只眼
    int eyeY = cy - 6;
    int eyeOffsetX = 14;
    int eyeR = 5;
    M5.Display.fillCircle(cx - eyeOffsetX, eyeY, eyeR, TFT_BLACK);
    M5.Display.fillCircle(cx + eyeOffsetX, eyeY, eyeR, TFT_BLACK);
    // 高光
    M5.Display.fillCircle(cx - eyeOffsetX + 1, eyeY - 1, 1, TFT_WHITE);
    M5.Display.fillCircle(cx + eyeOffsetX + 1, eyeY - 1, 1, TFT_WHITE);

    // 嘴
    if (listening) {
        // "o" 形 — 表示正在听
        M5.Display.drawCircle(cx, cy + 14, 5, TFT_BLACK);
        M5.Display.drawCircle(cx, cy + 14, 6, TFT_BLACK);
    } else {
        // 微笑曲线 — 用抛物线点阵近似画粗弧
        for (int i = -12; i <= 12; i++) {
            int x = cx + i;
            int y = cy + 12 + (i * i) / 18;  // 向上微弯成笑
            M5.Display.drawPixel(x, y, TFT_BLACK);
            M5.Display.drawPixel(x, y + 1, TFT_BLACK);
        }
    }
}

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

// 待机画面：上方显示状态，下方画一个明显的"按住说话"按钮
// pressing=true 时按钮变红色（录音中）；false 是青色待机色
void displayIdle(const char* topLine = "Pixel AI", const char* subLine = "", bool pressing = false) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.fillCircle(10, 10, 6, TFT_CYAN);

    if (topLine && strlen(topLine) > 0) {
        M5.Display.setCursor(24, 4);
        M5.Display.print(topLine);
    }
    if (subLine && strlen(subLine) > 0) {
        M5.Display.setCursor(4, 4 + LINE_H);
        M5.Display.print(subLine);
    }

    // 屏幕中段 (y≈80-130) 画 Pixel 表情
    // pressing=true 视为正在听，画 "o" 形嘴；否则画微笑
    drawPixelFace(160, 100, pressing);

    // 屏幕下半 (y=140-220) 画按钮
    const int bx = 20, by = 140, bw = 280, bh = 80;
    uint16_t btnColor   = pressing ? TFT_RED   : 0x05FF;     // 青蓝色
    uint16_t textColor  = TFT_WHITE;
    M5.Display.fillRoundRect(bx, by, bw, bh, 16, btnColor);
    M5.Display.drawRoundRect(bx, by, bw, bh, 16, TFT_WHITE);

    M5.Display.setTextColor(textColor, btnColor);
    M5.Display.setTextSize(3);
    const char* label = pressing ? "RECORDING" : "HOLD TO TALK";
    int textW = (int)strlen(label) * 18;  // textsize 3 ≈ 18px/char
    M5.Display.setCursor(bx + (bw - textW) / 2, by + (bh - 24) / 2);
    M5.Display.print(label);
    M5.Display.setTextSize(2);  // 复位到默认
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
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
