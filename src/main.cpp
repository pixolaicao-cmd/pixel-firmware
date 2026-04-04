/**
 * Pixel AI 挂件 — ESP32-S3 固件
 *
 * 工作流程：
 *   待机 → 按下按键 → 录音 → 松开按键 → 上传 → 转写 → AI 回复 → TTS → 播放
 *
 * 引脚配置见 config.h
 */

#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "wifi_mgr.h"
#include "recorder.h"
#include "player.h"
#include "api.h"

// ---- 内存分配（PSRAM）----
// WAV 录音缓冲区（30秒 × 16000Hz × 2bytes ≈ 960KB）
static uint8_t* wavBuf  = nullptr;
// MP3 回复缓冲区（最大 200KB）
static uint8_t* mp3Buf  = nullptr;
static const size_t WAV_BUF_SIZE = RECORD_BUF_SIZE + sizeof(WavHeader);
static const size_t MP3_BUF_SIZE = 200 * 1024;

// ---- 状态机 ----
enum class State {
    IDLE,
    RECORDING,
    UPLOADING,
    THINKING,
    SPEAKING,
    ERROR
};

State currentState = State::IDLE;

// ---- 翻译模式 ----
bool translateMode = false;

// 检测唤醒指令（支持中文、挪威语、英语）
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

// ---- LED 工具 ----
void ledOn()  { digitalWrite(LED_PIN, HIGH); }
void ledOff() { digitalWrite(LED_PIN, LOW); }
void ledBlink(int times, int ms = 100) {
    for (int i = 0; i < times; i++) {
        ledOn(); delay(ms); ledOff(); delay(ms);
    }
}

// ---- Setup ----
void setup() {
    Serial.begin(115200);
    Serial.println("\n[Pixel] Booting...");

    // GPIO
    pinMode(BTN_PIN, INPUT_PULLUP);
    pinMode(LED_PIN, OUTPUT);
    ledOff();

    // 显示
    displayInit();
    displayShow("Pixel", "Booting...");

    // 分配 PSRAM 缓冲区
    wavBuf = (uint8_t*)ps_malloc(WAV_BUF_SIZE);
    mp3Buf = (uint8_t*)ps_malloc(MP3_BUF_SIZE);
    if (!wavBuf || !mp3Buf) {
        displayShow("ERROR", "PSRAM alloc", "failed");
        Serial.println("[Pixel] PSRAM allocation failed!");
        while (true) { ledBlink(3, 200); delay(1000); }
    }
    Serial.println("[Pixel] PSRAM buffers allocated");

    // 硬件初始化
    recorderInit();
    playerInit();

    // WiFi
    if (connectWiFi()) {
        displayShow("Pixel", "Ready!", "Hold btn to talk");
        ledBlink(2);
    } else {
        displayShow("Pixel", "No WiFi", "Check setup");
    }

    Serial.println("[Pixel] Ready");
}

// ---- Main Loop ----
void loop() {
    bool btnPressed = (digitalRead(BTN_PIN) == LOW);

    switch (currentState) {

        case State::IDLE:
            if (btnPressed) {
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
                Serial.println("[Pixel] Recording started");
            }
            break;

        case State::RECORDING: {
            // 录音：持续到按键松开或超过最大时长
            uint32_t maxMs = RECORD_MAX_SEC * 1000UL;
            uint32_t startMs = millis();
            size_t totalWav = 0;

            // 在录音期间轮询按键
            while (digitalRead(BTN_PIN) == LOW && (millis() - startMs) < maxMs) {
                // 每帧录 500ms
                size_t sz = recordToBuffer(wavBuf, WAV_BUF_SIZE, 500);
                if (sz > sizeof(WavHeader)) totalWav = sz;
                // 更新显示（倒计时）
                int elapsed = (millis() - startMs) / 1000;
                char timeBuf[16];
                snprintf(timeBuf, sizeof(timeBuf), "%ds / %ds", elapsed, RECORD_MAX_SEC);
                displayShow("Recording...", timeBuf);
            }

            // 最终录取完整音频
            totalWav = recordToBuffer(wavBuf, WAV_BUF_SIZE,
                       max(0UL, maxMs - (millis() - startMs) + 100));

            ledOff();
            Serial.printf("[Pixel] Recording done: %d bytes\n", (int)totalWav);

            if (totalWav <= sizeof(WavHeader) + 100) {
                // 太短，忽略
                displayShow("Too short", "Hold longer");
                delay(1500);
                currentState = State::IDLE;
            } else {
                currentState = State::UPLOADING;
            }
            break;
        }

        case State::UPLOADING: {
            displayShow("Uploading...", "Please wait");
            ledBlink(1, 50);

            String transcript = transcribeAudio(wavBuf, WAV_BUF_SIZE);
            if (transcript.isEmpty()) {
                displayShow("ERROR", "Transcribe", "failed");
                delay(2000);
                currentState = State::IDLE;
                break;
            }

            Serial.printf("[Pixel] Transcript: %s\n", transcript.c_str());

            // ---- 检测翻译模式指令 ----
            String transcriptLower = transcript;
            transcriptLower.toLowerCase();

            if (isTranslateOff(transcriptLower)) {
                translateMode = false;
                displayShow("Pixel", "Chat mode", "Ready!");
                size_t sz = speakText("好的，退出翻译模式。", "zh", mp3Buf, MP3_BUF_SIZE);
                if (sz > 0) playMp3Buffer(mp3Buf, sz);
                currentState = State::IDLE;
                break;
            }

            if (isTranslateOn(transcriptLower)) {
                translateMode = true;
                displayShow("Pixel", "Translate ON", "Say anything!");
                size_t sz = speakText("翻译模式已开启，请说话。", "zh", mp3Buf, MP3_BUF_SIZE);
                if (sz > 0) playMp3Buffer(mp3Buf, sz);
                currentState = State::IDLE;
                break;
            }

            // ---- 翻译模式 ----
            if (translateMode) {
                displayShow("[Translate]", transcript.substring(0, 20).c_str());
                String srcLang = detectLang(transcript);
                TranslateResult result = translateText(transcript, srcLang);

                if (result.text.isEmpty()) {
                    displayShow("ERROR", "Translate", "failed");
                    delay(2000);
                    currentState = State::IDLE;
                    break;
                }

                Serial.printf("[Pixel] Translation: %s\n", result.text.c_str());
                displayShow("->", result.text.substring(0, 20).c_str(),
                            result.text.length() > 20 ? result.text.substring(20, 40).c_str() : "");

                size_t mp3Size = speakText(result.text, result.targetLang, mp3Buf, MP3_BUF_SIZE);
                if (mp3Size > 0) {
                    ledOn();
                    playMp3Buffer(mp3Buf, mp3Size);
                    ledOff();
                }
                displayShow("[Translate]", "Ready!", "Say anything");
                currentState = State::IDLE;
                break;
            }

            // ---- 普通对话模式 ----
            displayShow("Got it:", transcript.substring(0, 20).c_str());
            delay(500);
            displayShow("Thinking...");

            String reply = chatWithPixel(transcript);
            if (reply.isEmpty()) {
                displayShow("ERROR", "Chat failed");
                delay(2000);
                currentState = State::IDLE;
                break;
            }

            Serial.printf("[Pixel] Reply: %s\n", reply.c_str());
            displayShow("Pixel:", reply.substring(0, 20).c_str(),
                        reply.length() > 20 ? reply.substring(20, 40).c_str() : "");

            String lang = detectLang(reply);
            size_t mp3Size = speakText(reply, lang, mp3Buf, MP3_BUF_SIZE);
            if (mp3Size == 0) {
                displayShow("ERROR", "TTS failed");
                delay(2000);
                currentState = State::IDLE;
                break;
            }

            currentState = State::SPEAKING;
            ledOn();
            displayShow("Speaking...", reply.substring(0, 20).c_str());
            playMp3Buffer(mp3Buf, mp3Size);
            ledOff();

            displayShow("Pixel", "Ready!", "Hold btn to talk");
            currentState = State::IDLE;
            break;
        }

        case State::THINKING:
        case State::SPEAKING:
            // 由 UPLOADING 状态内联处理，这里不应到达
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
