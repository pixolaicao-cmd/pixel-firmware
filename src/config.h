#pragma once

// ============================================================
// Pixel AI 挂件 — 固件配置
// 修改这里的参数，不需要改其他文件
// ============================================================

// --- 后端 API ---
#define API_HOST        "skillful-mercy-production-881f.up.railway.app"
#define API_PORT        443
#define API_USE_SSL     true
#define API_TOKEN       ""   // 留空（后端用 JWT，ESP32 匿名访问 /transcribe 即可）

// --- WiFi 配网热点 ---
#define AP_SSID         "Pixel-Setup"
#define AP_PASSWORD     "pixel123"
#define AP_IP           "192.168.4.1"

// --- 引脚定义 ---

// I2S 麦克风 INMP441
#define MIC_SCK_PIN     12   // BCLK
#define MIC_WS_PIN      11   // LRCLK / WS
#define MIC_SD_PIN      10   // DATA

// I2S 扬声器 (MAX98357A 或同类 I2S DAC)
#define SPK_BCLK_PIN    14
#define SPK_LRC_PIN     15
#define SPK_DIN_PIN     13

// OLED (I2C, 1.3" SH1106 / 0.96" SSD1306)
#define OLED_SDA_PIN    8
#define OLED_SCL_PIN    9
#define OLED_WIDTH      128
#define OLED_HEIGHT     64

// SD 卡 (SPI)
#define SD_MOSI_PIN     35
#define SD_MISO_PIN     37
#define SD_SCK_PIN      36
#define SD_CS_PIN       34

// 按键 (按住录音, 松开发送)
#define BTN_PIN         0    // BOOT 按键，低电平有效

// 状态 LED
#define LED_PIN         2

// --- 录音参数 ---
#define SAMPLE_RATE     16000   // Hz，Deepgram 最佳
#define SAMPLE_BITS     16
#define RECORD_MAX_SEC  30      // 最大录音秒数
#define RECORD_BUF_SIZE (SAMPLE_RATE * 2 * RECORD_MAX_SEC)  // 16bit = 2 bytes/sample

// --- 显示 ---
// 用 U8G2_SH1106_128X64 适配 1.3" OLED，如果是 0.96" 换成 U8G2_SSD1306_128X64
#define USE_SH1106      true    // true=1.3" SH1106, false=0.96" SSD1306
