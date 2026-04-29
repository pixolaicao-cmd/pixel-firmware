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

// ── Pixel 表情：黑底 + 眉毛 + 眼睛 + 嘴
// 不画脸盘 — 让表情漂浮在屏幕中段更轻盈
// 状态：idle = 微笑；listening = "o"嘴
static inline void drawPixelFace(int cx, int cy, bool listening) {
    // ── 眉毛 — 22px 宽、3px 厚、中段微抬（友好/醒着）─────
    int browY = cy - 22;
    for (int i = -11; i <= 11; i++) {
        int absI = i < 0 ? -i : i;
        int lift = (11 - absI) / 6;  // 中间最高 +1，两端 0
        for (int t = 0; t < 3; t++) {
            M5.Display.drawPixel(cx - 28 + i, browY - lift + t, TFT_WHITE);
            M5.Display.drawPixel(cx + 28 + i, browY - lift + t, TFT_WHITE);
        }
    }

    // ── 眼睛 — 白色，黑瞳，小高光 ─────────────────────
    int eyeY = cy - 5;
    int eyeR = 9;
    M5.Display.fillCircle(cx - 28, eyeY, eyeR, TFT_WHITE);
    M5.Display.fillCircle(cx + 28, eyeY, eyeR, TFT_WHITE);
    M5.Display.fillCircle(cx - 28, eyeY, 4, TFT_BLACK);
    M5.Display.fillCircle(cx + 28, eyeY, 4, TFT_BLACK);
    M5.Display.fillCircle(cx - 28 + 2, eyeY - 2, 1, TFT_WHITE);
    M5.Display.fillCircle(cx + 28 + 2, eyeY - 2, 1, TFT_WHITE);

    // ── 嘴巴 ──────────────────────────────────────────
    if (listening) {
        // "O" 形 — 张嘴在听
        M5.Display.fillCircle(cx, cy + 20, 7, TFT_WHITE);
        M5.Display.fillCircle(cx, cy + 20, 5, TFT_BLACK);
    } else {
        // 微笑：抛物线 — 中点最低、两端上翘（这次方向对了）
        // y_center = cy + 22（最大 y）；y_corners = cy + 22 - (14*14)/14 = cy + 8（更小，向上翘）
        for (int i = -14; i <= 14; i++) {
            int y = cy + 22 - (i * i) / 14;
            M5.Display.drawPixel(cx + i, y,     TFT_WHITE);
            M5.Display.drawPixel(cx + i, y + 1, TFT_WHITE);
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

// ── 说话动画 ─────────────────────────────────────
// 实现思路：face 静态部分（眉、眼）只画一次；嘴巴每帧重画一个小区域。
// 这样不需要 fillScreen，避免明显闪烁。
//
// 嘴形 4 阶段循环：闭→微开→大张→微开，每 100ms 切一帧 → 接近真人讲话节奏。
static inline void _drawFaceStatic(int cx, int cy) {
    // 眉毛
    int browY = cy - 22;
    for (int i = -11; i <= 11; i++) {
        int absI = i < 0 ? -i : i;
        int lift = (11 - absI) / 6;
        for (int t = 0; t < 3; t++) {
            M5.Display.drawPixel(cx - 28 + i, browY - lift + t, TFT_WHITE);
            M5.Display.drawPixel(cx + 28 + i, browY - lift + t, TFT_WHITE);
        }
    }
    // 眼睛
    int eyeY = cy - 5;
    int eyeR = 9;
    M5.Display.fillCircle(cx - 28, eyeY, eyeR, TFT_WHITE);
    M5.Display.fillCircle(cx + 28, eyeY, eyeR, TFT_WHITE);
    M5.Display.fillCircle(cx - 28, eyeY, 4, TFT_BLACK);
    M5.Display.fillCircle(cx + 28, eyeY, 4, TFT_BLACK);
    M5.Display.fillCircle(cx - 28 + 2, eyeY - 2, 1, TFT_WHITE);
    M5.Display.fillCircle(cx + 28 + 2, eyeY - 2, 1, TFT_WHITE);
}

static inline void _drawMouthPhase(int cx, int cy, int phase) {
    // 先清嘴的区域 — 36×26
    M5.Display.fillRect(cx - 18, cy + 8, 36, 26, TFT_BLACK);
    switch (phase) {
        case 0: {
            // 闭嘴 — 微笑曲线
            for (int i = -12; i <= 12; i++) {
                int y = cy + 22 - (i * i) / 24;
                M5.Display.drawPixel(cx + i, y,     TFT_WHITE);
                M5.Display.drawPixel(cx + i, y + 1, TFT_WHITE);
            }
            break;
        }
        case 1: {
            // 微开
            M5.Display.fillCircle(cx, cy + 20, 4, TFT_WHITE);
            M5.Display.fillCircle(cx, cy + 20, 2, TFT_BLACK);
            break;
        }
        case 2: {
            // 大张
            M5.Display.fillCircle(cx, cy + 21, 8, TFT_WHITE);
            M5.Display.fillCircle(cx, cy + 21, 6, TFT_BLACK);
            break;
        }
        default: {
            // 微开（回程）
            M5.Display.fillCircle(cx, cy + 20, 5, TFT_WHITE);
            M5.Display.fillCircle(cx, cy + 20, 3, TFT_BLACK);
            break;
        }
    }
}

// 进入说话画面 — 一次性画背景 + 静态脸（眉、眼）
void displaySpeakingInit() {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.fillCircle(10, 10, 6, TFT_CYAN);
    M5.Display.setCursor(24, 4);
    M5.Display.print("Pixel");
    _drawFaceStatic(160, 100);
    _drawMouthPhase(160, 100, 0);
}

// 每帧调用 — 根据 elapsedMs 选当前嘴形阶段
void displaySpeakingTick(uint32_t elapsedMs) {
    int phase = (elapsedMs / 110) % 4;
    _drawMouthPhase(160, 100, phase);
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
