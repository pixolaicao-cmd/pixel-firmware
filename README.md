# Pixel AI 固件

ESP32-S3 固件，PlatformIO 项目。

## 引脚接线

| 模块 | 信号 | ESP32-S3 GPIO |
|------|------|---------------|
| INMP441 麦克风 | SCK (BCLK) | GPIO 12 |
| INMP441 麦克风 | WS (LRCLK) | GPIO 11 |
| INMP441 麦克风 | SD (DATA) | GPIO 10 |
| INMP441 麦克风 | VDD | 3.3V |
| INMP441 麦克风 | GND | GND |
| INMP441 麦克风 | L/R | GND (左声道) |
| MAX98357A 扬声器 | BCLK | GPIO 14 |
| MAX98357A 扬声器 | LRC | GPIO 15 |
| MAX98357A 扬声器 | DIN | GPIO 13 |
| OLED 1.3" | SDA | GPIO 8 |
| OLED 1.3" | SCL | GPIO 9 |
| SD 卡 | MOSI | GPIO 35 |
| SD 卡 | MISO | GPIO 37 |
| SD 卡 | SCK | GPIO 36 |
| SD 卡 | CS | GPIO 34 |
| 按键 | — | GPIO 0 (BOOT) |
| LED | — | GPIO 2 |

## 烧录步骤

1. 安装 VSCode + PlatformIO 插件
2. 打开 `pixel-firmware` 文件夹
3. 修改 `src/config.h`（后端地址已填好）
4. 点 Upload 烧录
5. 第一次启动：连接 WiFi 热点 `Pixel-Setup`，打开浏览器访问 `192.168.4.1` 配网

## 使用方法

- **按住** 按键 → 录音
- **松开** → 自动上传、AI 回复、语音播报
- 最长录音 30 秒

## 如果用 0.96" OLED

在 `config.h` 把 `USE_SH1106` 改为 `false`。
