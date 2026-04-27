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
    // M5.begin() 已在 setup() 里调用，电源/I2C 都准备好了。
    // 这里只需要点亮 mic 子系统（ES7210 + 内部 I2S）。
    auto cfg = M5.Mic.config();
    cfg.sample_rate     = SAMPLE_RATE;     // 16000
    cfg.stereo          = false;
    cfg.over_sampling   = 2;               // 1~6, 越高失真越低
    cfg.dma_buf_len     = 256;
    cfg.dma_buf_count   = 6;
    cfg.task_priority   = 2;
    cfg.task_pinned_core = -1;
    M5.Mic.config(cfg);

    if (!M5.Mic.begin()) {
        Serial.println("[MIC] M5.Mic.begin() FAILED — ES7210 codec not responding");
        return;
    }
    Serial.printf("[MIC] M5.Mic ready (ES7210, %d Hz mono)\n", SAMPLE_RATE);
}

/**
 * 录音并将结果写入 outBuf（WAV header + PCM data）
 * outBuf      指向 PSRAM 缓冲（main.cpp 已分配）
 * outBufSize  缓冲总字节数（含 44 字节 header 余量）
 * maxMs       最长录音时长
 * 返回        写入 outBuf 的总字节数（含 header）；录音失败返回 0
 */
size_t recordToBuffer(uint8_t* outBuf, size_t outBufSize, uint32_t maxMs) {
    if (!outBuf || outBufSize <= sizeof(WavHeader)) return 0;
    if (!M5.Mic.isEnabled()) {
        // 罕见情况：之前被 Speaker 抢占（共享 I2S 总线），重新拿回
        if (!M5.Mic.begin()) {
            Serial.println("[MIC] re-enable failed");
            return 0;
        }
    }

    const size_t headerSize    = sizeof(WavHeader);
    int16_t*     pcmStart      = reinterpret_cast<int16_t*>(outBuf + headerSize);
    const size_t maxSamples    = (outBufSize - headerSize) / sizeof(int16_t);

    uint32_t startMs        = millis();
    size_t   samplesWritten = 0;

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
    }

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
