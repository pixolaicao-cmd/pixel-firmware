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

// ---- 按键检测（CoreS3：BtnA 是屏幕下方左侧的电容触摸"屏幕底部"，
//      实际可用 M5.BtnA / M5.BtnPWR / 整块触摸屏 任一作为 Push-to-Talk）
// 这里用 BtnA：CoreS3 出厂时屏幕底部 1/3 区域被映射为触摸 BtnA。
// 留 Serial 'r' 作为开发期备用模拟通道（USB 调试时可不接触屏幕也能录音）。
static bool g_btnSimPressed = false;

bool isBtnPressed() {
#if USE_TOUCH_BTN
    // M5.update() 在 loop() 里每轮调一次，此处直接读最新状态
    return M5.BtnA.isPressed() || g_btnSimPressed;
#else
    return (digitalRead(BTN_PIN) == LOW);
#endif
}

bool isBtnReleased() {
#if USE_TOUCH_BTN
    return !(M5.BtnA.isPressed() || g_btnSimPressed);
#else
    return (digitalRead(BTN_PIN) == HIGH);
#endif
}

// 读取 Serial 模拟指令（'r' 模拟按住按键 2 秒；调试用，硬件按键不工作时备用）
void pollSerialSim() {
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'r') {
            g_btnSimPressed = true;
            Serial.println("[SIM] Button pressed");
            delay(2000);
            g_btnSimPressed = false;
            Serial.println("[SIM] Button released");
        }
    }
}

// ============================================================
// 配对屏幕回调（保持 pairing.h 与显示解耦）
// ============================================================

void showPairingCode(const String& code, int secondsLeft) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Code: %s", code.c_str());
    displayShow("Pairing...", buf, "Open Pixel App");
    Serial.printf("[Pair] Code: %s  (%ds left)\n", code.c_str(), secondsLeft);
}

void showPairingWaiting(int secondsLeft) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Waiting... %ds", secondsLeft);
    displayShow("Pairing...", buf, "Enter code in App");
    // 每隔约 30 秒打一次日志，避免刷屏
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

    displayShow("Pixel AI", "Ready!", "Press 'r' to talk");
    Serial.println("[Pixel] Ready. Press 'r' in Serial to simulate button.");
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
                    displayShow("Recording...", "Release to send");
                Serial.println("[Pixel] Recording...");
            }
            break;

        // ── 录音 ──────────────────────────────────────────────
        case State::RECORDING: {
            uint32_t maxMs = RECORD_MAX_SEC * 1000UL;
            uint32_t startMs = millis();
            size_t totalWav = 0;

            while (isBtnPressed() && (millis() - startMs) < maxMs) {
                size_t sz = recordToBuffer(wavBuf, WAV_BUF_SIZE, 500);
                if (sz > sizeof(WavHeader)) totalWav = sz;
                int elapsed = (millis() - startMs) / 1000;
                char timeBuf[24];
                snprintf(timeBuf, sizeof(timeBuf), "%ds / %ds", elapsed, RECORD_MAX_SEC);
                displayShow("Recording...", timeBuf);
            }

            // 最终截取完整帧
            totalWav = recordToBuffer(wavBuf, WAV_BUF_SIZE,
                       max(0UL, maxMs - (millis() - startMs) + 100));

            ledOff();
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
                String transcript = transcribeAudio(wavBuf, WAV_BUF_SIZE, g_deviceToken);
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
                    if (sz > 0) { ledOn(); playMp3Buffer(mp3Buf, sz); ledOff(); }
                    currentState = State::IDLE;
                    break;
                }

                displayShow("[Translate]", transcript.substring(0, 20).c_str());
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
                if (mp3Sz > 0) { ledOn(); playMp3Buffer(mp3Buf, mp3Sz); ledOff(); }

                displayShow("[Translate]", "Ready!", "Say anything");
                currentState = State::IDLE;
                break;
            }

            // ---- 普通对话：/api/voice 单次调用 ----
            displayShow("Thinking...", "Please wait");
            Serial.println("[Pixel] Calling voicePipeline...");

            VoiceResult vr = voicePipeline(wavBuf, WAV_BUF_SIZE,
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

            // 显示 AI 回复（最多两行，每行 20 字符）
            String replyLine1 = vr.reply.substring(0, 20);
            String replyLine2 = vr.reply.length() > 20 ? vr.reply.substring(20, 40) : "";
            displayShow("Pixel:", replyLine1.c_str(), replyLine2.c_str());

            // 播放 MP3
            currentState = State::SPEAKING;
            ledOn();
            playMp3Buffer(mp3Buf, vr.mp3Size);
            ledOff();

            displayShow("Pixel AI", "Ready!", "Press 'r' to talk");
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
