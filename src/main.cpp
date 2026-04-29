/**
 * Pixel AI 挂件 — ESP32-S3 固件 (M5Stack CoreS3)
 *
 * 工作流程：
 *   启动 → WiFi → 配对（首次）→ 待机
 *   待机 → 按下按键 → 录音 → 松开按键 → voicePipeline() → 播放
 *
 * ⚠️  硬件未到货期间：用 Serial 'r' 模拟按键，用 Serial.println 代替屏幕
 * 引脚配置见 config.h
 */

#include <Arduino.h>
#include <M5Unified.h>
#include "config.h"
#include "display.h"
#include "wifi_mgr.h"
#include "recorder.h"
#include "player.h"
#include "api.h"
#include "pairing.h"   // 必须在 api.h 之后，因为 api.h 先定义实现

// ---- 全局：设备 token（配对后填充）----
static String g_deviceToken = "";

// ---- 内存分配（PSRAM）----
// WAV 录音缓冲区（30秒 × 16000Hz × 2bytes ≈ 960KB）
static uint8_t* wavBuf = nullptr;
// MP3 回复缓冲区（最大 200KB）
static uint8_t* mp3Buf = nullptr;
static const size_t WAV_BUF_SIZE = RECORD_BUF_SIZE + sizeof(WavHeader);
static const size_t MP3_BUF_SIZE = 200 * 1024;

// ---- 状态机 ----
enum class State {
    IDLE,
    RECORDING,
    PROCESSING,   // 合并 UPLOADING + THINKING（voicePipeline 一次搞定）
    SPEAKING,
    PAIRING,
    ERROR
};

State currentState = State::IDLE;
// 上次录音实际写入 wavBuf 的字节数（含 WAV header）— PROCESSING 用这个值
// 而不是 WAV_BUF_SIZE，否则会把整 960KB 上传给 /api/voice 然后 HTTP 0 超时
static size_t g_wavBytes = 0;

// ---- 翻译模式 ----
bool translateMode = false;

bool isTranslateOn(const String& text) {
    return text.indexOf("翻译模式开启") >= 0 ||
           text.indexOf("翻译模式") >= 0 ||
           text.indexOf("oversettelse") >= 0 ||
           text.indexOf("translation mode") >= 0 ||
           text.indexOf("translate mode") >= 0;
}

bool isTranslateOff(const String& text) {
    return text.indexOf("翻译模式关闭") >= 0 ||
           text.indexOf("退出翻译") >= 0 ||
           text.indexOf("avslutt oversettelse") >= 0 ||
           text.indexOf("stop translation") >= 0 ||
           text.indexOf("exit translation") >= 0;
}

// ---- LED 工具（CoreS3 无独立 LED，函数保留以免编译报错）----
void ledOn()  { if (LED_PIN >= 0) digitalWrite(LED_PIN, HIGH); }
void ledOff() { if (LED_PIN >= 0) digitalWrite(LED_PIN, LOW); }
void ledBlink(int times, int ms = 100) {
    for (int i = 0; i < times; i++) {
        ledOn(); delay(ms); ledOff(); delay(ms);
    }
}

// ---- 按键检测（CoreS3：物理 BtnA/B/C 不存在，只有触摸屏 FT6336U）----
// 读 M5.Touch — 屏幕任何位置被按住即视为 push-to-talk。
// 留 Serial 'r' 作为开发期备用通道（异步：'r' 切换 hold 状态，再发一次释放）。
static bool g_btnSimPressed = false;

// HOLD-TO-TALK 按钮在屏幕的实际矩形（与 displayIdle() 里画的位置一致）
static const int BTN_RECT_X1 = 20,  BTN_RECT_Y1 = 140;
static const int BTN_RECT_X2 = 300, BTN_RECT_Y2 = 220;

bool isBtnPressed() {
#if USE_TOUCH_BTN
    // M5.update() 在 loop() 里每轮调一次，刷新触摸状态
    if (g_btnSimPressed) return true;
    if (M5.Touch.getCount() == 0) return false;
    auto t = M5.Touch.getDetail(0);
    // 只把落在 HOLD-TO-TALK 矩形内的触摸算成「按键」
    return (t.x >= BTN_RECT_X1 && t.x <= BTN_RECT_X2 &&
            t.y >= BTN_RECT_Y1 && t.y <= BTN_RECT_Y2);
#else
    return (digitalRead(BTN_PIN) == LOW);
#endif
}

// 注：顶部触摸调音量已移除 — IDLE 状态 M5.Speaker 没 begin，setVolume 不生效，
// 用户按了没反馈很迷惑。改成中段画 Pixel 表情。音量调节保留：Serial 发 +/- 仍可调，
// 后续可用语音指令"大声点 / 小声点"控制。

bool isBtnReleased() {
    return !isBtnPressed();
}

// 读取 Serial 模拟指令（异步切换：第一次 'r' 按下，第二次 'r' 释放；
// 或发 'R'/换行 立即释放）。不再用 delay() 阻塞主循环。
void pollSerialSim() {
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'r') {
            g_btnSimPressed = !g_btnSimPressed;
            Serial.printf("[SIM] Button %s\n",
                          g_btnSimPressed ? "PRESSED (send 'r' again to release)" : "released");
        } else if (c == 'R' || c == '\n' || c == ' ') {
            if (g_btnSimPressed) {
                g_btnSimPressed = false;
                Serial.println("[SIM] Button released");
            }
        } else if (c == '+' || c == '=') {
            playerVolumeUp();
        } else if (c == '-' || c == '_') {
            playerVolumeDown();
        }
    }
}

// ============================================================
// 配对屏幕回调（保持 pairing.h 与显示解耦）
// ============================================================

// 把配对码暂存为全局，等待界面也要显示同一个码
static String s_pairingCode = "";

void showPairingCode(const String& code, int secondsLeft) {
    s_pairingCode = code;
    char buf[32];
    snprintf(buf, sizeof(buf), "Code: %s", code.c_str());
    displayShow("Pair Pixel", buf, "Open Pixel app");
    Serial.printf("[Pair] Code: %s  (%ds left)\n", code.c_str(), secondsLeft);
}

void showPairingWaiting(int secondsLeft) {
    // 保持配对码可见 —— 不覆盖成 "Waiting..."
    char codeLine[32];
    snprintf(codeLine, sizeof(codeLine), "Code: %s", s_pairingCode.c_str());
    char timeLine[32];
    snprintf(timeLine, sizeof(timeLine), "Waiting... %ds", secondsLeft);
    displayShow("Pair Pixel", codeLine, timeLine);
    if (secondsLeft % 30 == 0)
        Serial.printf("[Pair] Still waiting... %ds left\n", secondsLeft);
}

void showPairingError(const String& msg) {
    displayShow("Pair Error", msg.c_str(), "Restart device");
    Serial.printf("[Pair] Error: %s\n", msg.c_str());
}

// ============================================================
// Setup
// ============================================================

void setup() {
    Serial.begin(115200);
    Serial.println("\n[Pixel] Booting...");

    // M5Unified 初始化（LCD、触摸、IMU、AXP2101 电源）
    auto cfg = M5.config();
    M5.begin(cfg);

    // GPIO（CoreS3 上 LED_PIN = -1，跳过 pinMode）
    if (LED_PIN >= 0) {
        pinMode(LED_PIN, OUTPUT);
        ledOff();
    }

    // 显示初始化
    displayInit();
    displayShow("Pixel AI", "Booting...");

    // 分配 PSRAM 缓冲区
    wavBuf = (uint8_t*)ps_malloc(WAV_BUF_SIZE);
    mp3Buf = (uint8_t*)ps_malloc(MP3_BUF_SIZE);
    if (!wavBuf || !mp3Buf) {
        displayShow("ERROR", "PSRAM alloc failed");
        Serial.println("[Pixel] PSRAM allocation failed!");
        while (true) { delay(1000); }
    }
    Serial.printf("[Pixel] PSRAM: wavBuf=%dKB mp3Buf=%dKB\n",
                  (int)(WAV_BUF_SIZE / 1024), (int)(MP3_BUF_SIZE / 1024));

    // 硬件初始化
    recorderInit();
    playerInit();

    // WiFi
    displayShow("Pixel AI", "Connecting WiFi...");
    if (!connectWiFi()) {
        displayShow("No WiFi", "Check setup");
        Serial.println("[Pixel] WiFi failed — skipping pairing");
        return;
    }
    Serial.println("[Pixel] WiFi OK");
    // 关 WiFi power save —— 默认开启会把 TCP/TLS 上传从 100+KB/s
    // 限速到 ~5KB/s。5 秒录音 (160KB) 上传时间从 30s+ 掉到 ~2s。
    WiFi.setSleep(false);
    // 让 WiFi/TCP 栈稳一下，避免首发 TLS 握手偶发失败
    delay(1500);

    // 配对流程（若 NVS 已有 token 则立即返回）
    displayShow("Pixel AI", "Checking pairing...");
    g_deviceToken = runPairingFlow(showPairingCode, showPairingWaiting, showPairingError);

    if (g_deviceToken.isEmpty()) {
        displayShow("Not paired", "Restart to retry");
        Serial.println("[Pixel] Pairing failed or timed out");
        // 不 block — 仍允许匿名使用（token 为空时 API 不带 Authorization header）
    } else {
        Serial.printf("[Pixel] Token: %s...%s\n",
                      g_deviceToken.substring(0, 8).c_str(),
                      g_deviceToken.substring(g_deviceToken.length() - 4).c_str());
    }

    displayIdle("Pixel AI", "Ready!", false);
    Serial.println("[Pixel] Ready. Tap & hold screen to talk (or 'r' in Serial).");
}

// ============================================================
// Main Loop
// ============================================================

void loop() {
    // 必须每轮调一次：刷新 M5.BtnA / 触摸 / IMU 等内部状态
    M5.update();
    // 读 Serial 模拟按键（调试用备用通道）
    pollSerialSim();

    switch (currentState) {

        // ── 待机 ──────────────────────────────────────────────
        case State::IDLE:
            if (isBtnPressed()) {
                // 没 token 就不让进录音 — 否则 voicePipeline 会 NULL deref 崩
                if (g_deviceToken.isEmpty()) {
                    displayShow("Not paired", "Restart device", "Pair in app");
                    Serial.println("[Pixel] Tap ignored — no device token (pairing required)");
                    delay(2000);
                    displayIdle("Pixel AI", "Not paired", false);
                    return;
                }
                if (!isWiFiConnected()) {
                    displayShow("No WiFi", "Reconnecting...");
                    connectWiFi();
                    return;
                }
                currentState = State::RECORDING;
                ledOn();
                if (translateMode)
                    displayShow("[Translate]", "Recording...", "Release to send");
                else
                    displayIdle("Recording", "Release to send", true);
                Serial.println("[Pixel] Recording...");
            }
            break;

        // ── 录音 ──────────────────────────────────────────────
        case State::RECORDING: {
            // 单次 recordToBuffer 一直录，按键释放（shouldStop=true）就停。
            // 旧版本是 500ms chunk 循环 + final cut，每次 chunk 覆写 wavBuf，
            // 最终只剩一小段尾音；并且 chunk 之间 mic_task 会撞上失效 rx queue。
            size_t totalWav = recordToBuffer(
                wavBuf, WAV_BUF_SIZE,
                RECORD_MAX_SEC * 1000UL,
                /* shouldStop */ []() {
                    // recordToBuffer 内部不会自己调 M5.update / pollSerialSim,
                    // 必须在这里手动刷新触摸状态 + serial 'r' 模拟切换
                    M5.update();
                    pollSerialSim();
                    return !isBtnPressed();
                },
                /* onTick     */ [](uint32_t elapsedMs) {
                    char timeBuf[24];
                    snprintf(timeBuf, sizeof(timeBuf), "%us / %ds",
                             (unsigned)(elapsedMs / 1000), RECORD_MAX_SEC);
                    displayShow("Recording...", timeBuf);
                });

            ledOff();
            g_wavBytes = totalWav;
            Serial.printf("[Pixel] Recorded: %d bytes\n", (int)totalWav);

            if (totalWav <= (size_t)(sizeof(WavHeader) + 100)) {
                displayShow("Too short", "Hold longer");
                delay(1500);
                currentState = State::IDLE;
            } else {
                currentState = State::PROCESSING;
            }
            break;
        }

        // ── 处理（翻译 or 主管线）────────────────────────────
        case State::PROCESSING: {

            // ---- 翻译模式：用分步接口（translate 不走 /api/voice）----
            if (translateMode) {
                displayShow("[Translate]", "Transcribing...");
                String transcript = transcribeAudio(wavBuf, g_wavBytes, g_deviceToken);
                if (transcript.isEmpty()) {
                    displayShow("ERROR", "Transcribe failed");
                    delay(2000);
                    currentState = State::IDLE;
                    break;
                }

                String transcriptLower = transcript;
                transcriptLower.toLowerCase();

                if (isTranslateOff(transcriptLower)) {
                    translateMode = false;
                    displayShow("Pixel AI", "Chat mode", "Ready!");
                    size_t sz = speakText("好的，退出翻译模式。", "zh",
                                          mp3Buf, MP3_BUF_SIZE, g_deviceToken);
                    if (sz > 0) {
                        displaySpeakingInit();
                        ledOn();
                        playMp3Buffer(mp3Buf, sz, displaySpeakingTick);
                        ledOff();
                    }
                    currentState = State::IDLE;
                    break;
                }

                // 不显示转写正文（同样是中文乱码风险），只显示状态
                displayShow("[Translate]", "Translating...");
                String srcLang = detectLang(transcript);
                TranslateResult tr = translateText(transcript, srcLang, g_deviceToken);

                if (tr.text.isEmpty()) {
                    displayShow("ERROR", "Translate failed");
                    delay(2000);
                    currentState = State::IDLE;
                    break;
                }

                Serial.printf("[Pixel] Translation: %s\n", tr.text.c_str());
                displayShow("->", tr.text.substring(0, 20).c_str(),
                            tr.text.length() > 20 ? tr.text.substring(20, 40).c_str() : "");

                size_t mp3Sz = speakText(tr.text, tr.targetLang,
                                          mp3Buf, MP3_BUF_SIZE, g_deviceToken);
                if (mp3Sz > 0) {
                    displaySpeakingInit();
                    ledOn();
                    playMp3Buffer(mp3Buf, mp3Sz, displaySpeakingTick);
                    ledOff();
                }

                displayShow("[Translate]", "Ready!", "Say anything");
                currentState = State::IDLE;
                break;
            }

            // ---- 普通对话：/api/voice 单次调用 ----
            displayShow("Thinking...", "Please wait");
            Serial.println("[Pixel] Calling voicePipeline...");

            VoiceResult vr = voicePipeline(wavBuf, g_wavBytes,
                                           mp3Buf, MP3_BUF_SIZE,
                                           g_deviceToken);

            if (!vr.success) {
                Serial.printf("[Pixel] voicePipeline error: %s\n", vr.errorMsg.c_str());
                displayShow("ERROR", vr.errorMsg.substring(0, 20).c_str());
                delay(2500);
                currentState = State::IDLE;
                break;
            }

            Serial.printf("[Pixel] STT: %s\n", vr.transcript.c_str());
            Serial.printf("[Pixel] AI:  %s\n", vr.reply.c_str());

            // 检查翻译模式开启指令（在 STT 文本里）
            String tLow = vr.transcript;
            tLow.toLowerCase();
            if (isTranslateOn(tLow)) {
                translateMode = true;
                displayShow("Pixel AI", "Translate ON", "Say anything!");
                // 仍然播放 AI 的回复（通常会确认模式切换）
            }

            // 不显示回复正文（reply 是 URL-encoded UTF-8 中文，
            // M5 默认字库也不渲染汉字，硬画出来就是一串乱码）。
            // 改成动画 Pixel 脸 + 嘴巴跟着说话节奏开合。
            displaySpeakingInit();

            // 播放 MP3，每帧动画嘴巴
            currentState = State::SPEAKING;
            ledOn();
            playMp3Buffer(mp3Buf, vr.mp3Size, displaySpeakingTick);
            ledOff();

            displayIdle("Pixel AI", "Ready!", false);
            currentState = State::IDLE;
            break;
        }

        case State::SPEAKING:
            // 由 PROCESSING 内联处理，不应到达此分支
            currentState = State::IDLE;
            break;

        case State::PAIRING:
            // 预留：可在运行时重新发起配对（长按触发）
            currentState = State::IDLE;
            break;

        case State::ERROR:
            ledBlink(3, 300);
            displayShow("ERROR", "Restarting...");
            delay(3000);
            ESP.restart();
            break;
    }

    delay(20);
}
