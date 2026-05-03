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

// ── 顶部状态栏 ─────────────────────────────────────
// 高度 22px：左 WiFi SSID、右 电量百分比 + 充电图标
// 整条状态栏可点 → 进设置页（在 main.cpp 里检测）
// 用 ssid="" 表示未连，会画红点
static inline void drawStatusBar(const char* ssid, int batteryPct, bool charging) {
    // 清掉顶栏区域（避免叠加显示残留）
    M5.Display.fillRect(0, 0, 320, 22, TFT_BLACK);

    // 左：Pixel logo + WiFi 状态点 + SSID
    M5.Display.fillCircle(10, 11, 5, TFT_CYAN);
    bool connected = (ssid && strlen(ssid) > 0);
    M5.Display.fillCircle(24, 11, 3, connected ? TFT_GREEN : TFT_RED);

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(34, 7);
    if (connected) {
        // 截断长 SSID（textsize=1，6px/char，留出右边电量空间 → 最多 ~22 字符）
        char buf[24];
        size_t n = strlen(ssid);
        if (n > 22) {
            memcpy(buf, ssid, 19);
            memcpy(buf + 19, "...", 4);
        } else {
            memcpy(buf, ssid, n + 1);
        }
        M5.Display.print(buf);
    } else {
        M5.Display.print("No WiFi");
    }

    // 右：电量百分比（右对齐）+ 充电符号
    char pct[8];
    if (batteryPct < 0) {
        snprintf(pct, sizeof(pct), "--%%");
    } else {
        snprintf(pct, sizeof(pct), "%d%%", batteryPct);
    }
    int pctW = (int)strlen(pct) * 6;
    int pctX = 320 - 4 - pctW;
    // 电量颜色：>30 白 / 10-30 黄 / <10 红
    uint16_t batColor = TFT_WHITE;
    if (batteryPct >= 0 && batteryPct < 10)      batColor = TFT_RED;
    else if (batteryPct >= 0 && batteryPct < 30) batColor = TFT_YELLOW;
    M5.Display.setTextColor(batColor, TFT_BLACK);
    M5.Display.setCursor(pctX, 7);
    M5.Display.print(pct);

    if (charging) {
        // 充电闪电符号 — 在百分比左边
        int lx = pctX - 10, ly = 6;
        M5.Display.fillTriangle(lx + 4, ly,     lx,     ly + 7, lx + 5, ly + 7, TFT_CYAN);
        M5.Display.fillTriangle(lx + 5, ly + 7, lx + 9, ly + 7, lx + 5, ly + 14, TFT_CYAN);
    }

    // 复位文字样式
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

// 顶栏在屏幕中的可点击范围 — main.cpp 里检测点击进设置页用
static const int STATUS_BAR_H = 22;

// 待机画面：顶栏 + 中段表情 + 下方"按住说话"
// pressing=true 时按钮变红色（录音中）；false 是青色待机色
void displayIdle(const char* topLine = "Pixel AI", const char* subLine = "", bool pressing = false,
                 const char* wifiSsid = nullptr, int batteryPct = -1, bool charging = false) {
    M5.Display.fillScreen(TFT_BLACK);

    // 顶部状态栏 — WiFi + 电量
    drawStatusBar(wifiSsid, batteryPct, charging);

    // 副标题（可选）— 顶栏下面一行
    if (subLine && strlen(subLine) > 0) {
        M5.Display.setTextSize(2);
        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.setCursor(4, 28);
        M5.Display.print(subLine);
    }
    // 注：旧 topLine 在这个布局里被 SSID 顶替了 — 只在没传 wifiSsid 时显示，
    // 保持调用方向后兼容（main.cpp 旧 displayIdle("Pixel AI", ...) 不会失效）
    if ((!wifiSsid || strlen(wifiSsid) == 0) && topLine && strlen(topLine) > 0
        && !subLine) {
        // 完全没传 wifi 也没 sub：把 topLine 显示在副位置
        M5.Display.setTextSize(2);
        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5.Display.setCursor(4, 28);
        M5.Display.print(topLine);
    }

    // 屏幕中段 (y≈80-130) 画 Pixel 表情
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

// ── 设置页面 ────────────────────────────────────────
// 全屏：WiFi 信息（SSID/IP/RSSI）、电量、设备 token 短串
// 任何位置点击 → 退出回 idle
void displaySettings(const char* ssid, const char* ip, int rssi,
                     int batteryPct, bool charging,
                     const char* tokenShort) {
    M5.Display.fillScreen(TFT_BLACK);

    // 顶栏依然显示 — 设置页里也能扫一眼电量
    drawStatusBar(ssid, batteryPct, charging);

    // 标题
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.setCursor(4, 30);
    M5.Display.print("Settings");

    // 内容用 textsize 1（6×8 字体），密度高、放得下
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    int y = 60;
    const int LH = 14;

    auto labelLine = [&](const char* label, const char* value, uint16_t valColor) {
        M5.Display.setTextColor(0x7BEF, TFT_BLACK);  // 灰
        M5.Display.setCursor(4, y);
        M5.Display.print(label);
        M5.Display.setTextColor(valColor, TFT_BLACK);
        M5.Display.setCursor(80, y);
        M5.Display.print(value);
        y += LH;
    };

    bool connected = (ssid && strlen(ssid) > 0);
    labelLine("WiFi:", connected ? ssid : "Not connected",
              connected ? TFT_GREEN : TFT_RED);
    if (connected) {
        labelLine("IP:", ip ? ip : "-", TFT_WHITE);
        char rssiBuf[16];
        if (rssi == 0) snprintf(rssiBuf, sizeof(rssiBuf), "-");
        else           snprintf(rssiBuf, sizeof(rssiBuf), "%d dBm", rssi);
        labelLine("Signal:", rssiBuf, TFT_WHITE);
    }

    char batBuf[16];
    if (batteryPct < 0) snprintf(batBuf, sizeof(batBuf), "--");
    else                snprintf(batBuf, sizeof(batBuf),
                                  "%d%%%s", batteryPct, charging ? " (chg)" : "");
    labelLine("Battery:", batBuf, TFT_WHITE);

    labelLine("Device:", (tokenShort && strlen(tokenShort) > 0) ? tokenShort : "Not paired",
              (tokenShort && strlen(tokenShort) > 0) ? TFT_WHITE : TFT_YELLOW);

    // 底部提示 — 全宽框，"tap to close"
    const int bx = 20, by = 180, bw = 280, bh = 40;
    M5.Display.fillRoundRect(bx, by, bw, bh, 10, 0x2104);  // 暗灰
    M5.Display.drawRoundRect(bx, by, bw, bh, 10, TFT_WHITE);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_WHITE, 0x2104);
    const char* msg = "Tap to close";
    int tw = (int)strlen(msg) * 12;
    M5.Display.setCursor(bx + (bw - tw) / 2, by + (bh - 16) / 2);
    M5.Display.print(msg);

    // 复位
    M5.Display.setTextSize(2);
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
