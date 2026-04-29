#pragma once

// ============================================================
// Pixel AI 挂件 — 固件配置
// 目标硬件：M5Stack CoreS3（ESP32-S3, 内置麦克风/扬声器/摄像头）
// ============================================================

// --- 后端 API ---
#define API_HOST    "pixel-web-three.vercel.app"
#define API_PORT    443
#define API_USE_SSL true

// TLS 证书验证（默认 false —— setInsecure()）
// ⚠️ 硬件到货后，先用默认 false 验证整条流程工作正常，再改成 true 启用证书校验
//    启用后若证书链问题导致连接失败，设备无法上线，务必先在稳定环境验证
// Vercel *.vercel.app 使用 Let's Encrypt 签发（ISRG Root X1 / X2），证书见 cert.h
#define USE_CERT_PINNING false

// 设备 token（首次启动为空，配对后写入 NVS Flash 持久化）
#define NVS_NAMESPACE   "pixel"
#define NVS_TOKEN_KEY   "device_token"
#define NVS_DEVICE_ID_KEY "device_id"

// --- WiFi 配网热点（设备未连过网时启动） ---
// SSID 也带 MAC 后缀 → 同一空间多台 Pixel 不会撞名
// 密码每台设备唯一（运行时由 MAC 后 3 字节生成，见 wifi_mgr.h:apPassword()）
// 旧的硬编码 "pixel123" 已下线 —— 公开仓库 + 全设备同密码 = 任何人都能蹭网
#define AP_SSID_PREFIX "Pixel-"
#define AP_IP          "192.168.4.1"

// ============================================================
// M5Stack CoreS3 硬件（内置，无需外接）
// 参考：https://docs.m5stack.com/en/core/CoreS3
//
// ⚠️ 引脚由 M5Unified 库统一管理，不要手写 i2s_driver_install。
//     - 麦克风 ES7210 4-ch I2S codec  → M5.Mic 接管（recorder.h）
//     - 扬声器 AW88298 I2S 功放        → M5.Speaker 接管（player.h）
//     - 麦/喇叭共享同一条 I2S 总线，由 M5Unified 切换 RX/TX 方向
// ============================================================

// --- 触摸按键（CoreS3 屏幕底部映射为 BtnA） ---
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
