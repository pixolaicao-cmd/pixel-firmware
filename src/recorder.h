#pragma once
/**
 * Pixel AI — 录音层（M5Stack CoreS3）
 *
 * 硬件：CoreS3 内置 ES7210 4 通道 I2S codec + 双 MEMS mic
 *   - codec 必须先经 I2C 配置寄存器才会出声 — 这部分由 M5Unified 接管
 *   - 我们只调 M5.Mic 高层 API，不直接碰 I2S 驱动
 *
 * 输出：标准 16-bit mono WAV（含 44 字节 header），16kHz，PCM
 * 主流程在 main.cpp 中按 ≤ 500ms 增量循环调用本函数 —— 每次覆写 outBuf。
 */

#include <M5Unified.h>
#include "config.h"

// ---- WAV 文件头（44 字节）---------------------------------
struct WavHeader {
    char     riff[4]         = {'R','I','F','F'};
    uint32_t chunkSize;
    char     wave[4]         = {'W','A','V','E'};
    char     fmt[4]          = {'f','m','t',' '};
    uint32_t subchunk1Size   = 16;
    uint16_t audioFormat     = 1;          // PCM
    uint16_t numChannels     = 1;          // Mono
    uint32_t sampleRate      = SAMPLE_RATE;
    uint32_t byteRate        = SAMPLE_RATE * 2;
    uint16_t blockAlign      = 2;
    uint16_t bitsPerSample   = 16;
    char     data[4]         = {'d','a','t','a'};
    uint32_t dataSize;
};

// 一次 DMA 抓多少 sample。256 = 16ms @ 16kHz；够小让按键释放响应快，
// 够大让 DMA 不在循环里疯狂被叫醒。
static const size_t MIC_CHUNK_SAMPLES = 256;

void recorderInit() {
    // CoreS3 的 mic/speaker 共享 I2S0 — 不能同时 begin。
    // 这里只把 config 挂上去，真正 begin 在 recordToBuffer 入口完成
    // （并且会先 M5.Speaker.end() 让出总线）。
    auto cfg = M5.Mic.config();
    cfg.sample_rate     = SAMPLE_RATE;     // 16000
    cfg.stereo          = false;
    cfg.over_sampling   = 2;               // 1~6, 越高失真越低
    cfg.dma_buf_len     = 256;
    cfg.dma_buf_count   = 6;
    cfg.task_priority   = 2;
    cfg.task_pinned_core = -1;
    M5.Mic.config(cfg);
    Serial.printf("[MIC] config staged (ES7210, %d Hz mono — begin on demand)\n",
                  SAMPLE_RATE);
}

/**
 * 录音并将结果写入 outBuf（WAV header + PCM data）
 * outBuf       指向 PSRAM 缓冲（main.cpp 已分配）
 * outBufSize   缓冲总字节数（含 44 字节 header 余量）
 * maxMs        最长录音时长
 * shouldStop   每 chunk 调一次的回调；返回 true 立即停止录音（如按键释放）
 * onTick       每 chunk 调一次的回调，参数是已录音时长（ms），用于更新 UI
 *
 * 返回         写入 outBuf 的总字节数（含 header）；录音失败返回 0
 *
 * 注意：本函数从头到尾只 begin / end 一次 mic_task，避免在 chunk 之间
 * 反复进入 i2s_driver_install / uninstall 导致 mic_task 在 i2s_read 时
 * 拿到已被释放的 rx queue（EXCVADDR=0x1c LoadProhibited）。
 */
size_t recordToBuffer(uint8_t* outBuf, size_t outBufSize, uint32_t maxMs,
                      bool (*shouldStop)() = nullptr,
                      void (*onTick)(uint32_t elapsedMs) = nullptr) {
    if (!outBuf || outBufSize <= sizeof(WavHeader)) return 0;

    // I2S 切换：让出 Speaker（如果之前在用），再拿 Mic
    if (M5.Speaker.isEnabled()) M5.Speaker.end();
    if (!M5.Mic.isEnabled()) {
        if (!M5.Mic.begin()) {
            Serial.println("[MIC] M5.Mic.begin() failed — ES7210 not responding");
            return 0;
        }
    }

    const size_t headerSize    = sizeof(WavHeader);
    int16_t*     pcmStart      = reinterpret_cast<int16_t*>(outBuf + headerSize);
    const size_t maxSamples    = (outBufSize - headerSize) / sizeof(int16_t);

    uint32_t startMs        = millis();
    size_t   samplesWritten = 0;
    uint32_t lastTickMs     = 0;

    while ((millis() - startMs) < maxMs &&
           samplesWritten + MIC_CHUNK_SAMPLES <= maxSamples) {
        // 队列一次 DMA 录制；如 mic 仍在忙就轮询等
        bool queued = false;
        for (int retry = 0; retry < 50 && !queued; retry++) {
            queued = M5.Mic.record(pcmStart + samplesWritten,
                                   MIC_CHUNK_SAMPLES,
                                   SAMPLE_RATE,
                                   false /* mono */);
            if (!queued) delay(1);
        }
        if (!queued) {
            Serial.println("[MIC] record() queue timeout");
            break;
        }
        // 等 DMA 把这一块填满（典型 16ms）
        while (M5.Mic.isRecording()) { delay(1); }
        samplesWritten += MIC_CHUNK_SAMPLES;

        uint32_t elapsed = millis() - startMs;
        // ~250ms 调一次 onTick，避免每 16ms 重绘 LCD 抢 SPI
        if (onTick && elapsed - lastTickMs >= 250) {
            onTick(elapsed);
            lastTickMs = elapsed;
        }
        // 用户松手 / 业务层要求停 → 立刻退出
        if (shouldStop && shouldStop()) break;
    }

    // 关键：在函数返回前 end() mic，让 mic_task 安全退出后再让出 I2S。
    // 否则上层在我们退出后跑 voicePipeline / Speaker.begin 时，
    // mic_task 仍持有 rx queue → i2s_driver_uninstall 后 mic_task 下一轮
    // i2s_read 拿空指针 → LoadProhibited @ Mic_Class.cpp:232
    M5.Mic.end();

    // 写 WAV 文件头
    const uint32_t audioBytes = static_cast<uint32_t>(samplesWritten * sizeof(int16_t));
    WavHeader hdr;
    hdr.dataSize  = audioBytes;
    hdr.chunkSize = 36 + audioBytes;
    memcpy(outBuf, &hdr, headerSize);

    Serial.printf("[MIC] Recorded %u bytes (%u samples, %u ms)\n",
                  (unsigned)audioBytes,
                  (unsigned)samplesWritten,
                  (unsigned)(millis() - startMs));
    return headerSize + audioBytes;
}

// 兼容旧调用点 — 这里 mic 已经在 recordToBuffer 内部 end 过，再调一次安全。
inline void recorderStopMic() {
    if (M5.Mic.isEnabled()) M5.Mic.end();
}
