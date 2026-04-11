#pragma once

// ============================================================
// Pixel AI 挂件 — 固件配置
// 目标硬件：M5Stack CoreS3（ESP32-S3, 内置麦克风/扬声器/摄像头）
// ============================================================

// --- 后端 API ---
#define API_HOST    "pixel-web-three.vercel.app"
#define API_PORT    443
#define API_USE_SSL true

// 设备 token（首次启动为空，配对后写入 NVS Flash 持久化）
#define NVS_NAMESPACE   "pixel"
#define NVS_TOKEN_KEY   "device_token"
#define NVS_DEVICE_ID_KEY "device_id"

// --- WiFi 配网热点（设备未连过网时启动） ---
#define AP_SSID     "Pixel-Setup"
#define AP_PASSWORD "pixel123"
#define AP_IP       "192.168.4.1"

// ============================================================
// M5Stack CoreS3 引脚（内置硬件，无需外接）
// 参考：https://docs.m5stack.com/en/core/CoreS3
// ⚠️  到货后用 M5.begin() 初始化，引脚由 M5Stack 库自动管理
// ============================================================

// --- 内置 I2S 麦克风 (SPM1423, PDM 模式) ---
#define MIC_SCK_PIN      0    // PDM CLK
#define MIC_SD_PIN       14   // PDM DATA
#define MIC_USE_PDM      true

// --- 内置扬声器（AW88298 I2S 功放） ---
#define SPK_BCLK_PIN     3
#define SPK_LRC_PIN      12
#define SPK_DIN_PIN      13
#define SPK_AMP_I2C_ADDR 0x36

// --- 内置触摸按键（替代物理 BTN） ---
// 使用 M5.BtnA / M5.BtnB / M5.BtnC，不占 GPIO
#define USE_TOUCH_BTN    true

// --- 状态 LED ---
// CoreS3 无独立 LED，用屏幕颜色/图标替代
#define LED_PIN          (-1)

// ============================================================
// 录音参数
// ============================================================
#define SAMPLE_RATE      16000
#define SAMPLE_BITS      16
#define RECORD_MAX_SEC   30
#define RECORD_BUF_SIZE  (SAMPLE_RATE * 2 * RECORD_MAX_SEC)  // ~960KB PSRAM

// ============================================================
// 网络超时 (ms)
// ============================================================
#define HTTP_TIMEOUT_MS      25000
#define VOICE_TIMEOUT_MS     45000   // /api/voice: STT + AI + TTS 全在服务端
#define PAIRING_POLL_MS      3000
#define PAIRING_TIMEOUT_MS   120000  // 等用户配对最多 2 分钟
