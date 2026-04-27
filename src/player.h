#pragma once
/**
 * Pixel AI — 播放层（M5Stack CoreS3）
 *
 * 硬件：CoreS3 内置 AW88298 I2S 功放 + 内置喇叭
 *   - AW88298 必须先经 I2C 解除静音才会出声 — 这部分由 M5Unified 接管
 *   - 我们只调 M5.Speaker 高层 API
 *
 * 流程：MP3 字节 → ESP8266Audio 解码到 PCM → M5.Speaker.playRaw 一次性播放
 *   - 不直接对接 I2S（避免和 M5.Mic 共享的 I2S 总线打架）
 *   - 解码缓冲分配在 PSRAM，可吃下 ~30 秒 24kHz mono 音频
 */

#include <M5Unified.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutput.h>
#include <AudioFileSource.h>
#include <AudioFileSourceBuffer.h>
#include "config.h"

// ─── 解码 sink：把 ESP8266Audio 的输出捕获成 int16 PCM ─────
//   这样我们就有完整 PCM 缓冲可以一把丢给 M5.Speaker.playRaw。
class PCMCaptureSink : public AudioOutput {
public:
    PCMCaptureSink(int16_t* buf, size_t maxSamples)
        : _buf(buf), _capacity(maxSamples) {}

    bool SetRate(int hz) override {
        _hertz = hz;
        // ESP8266Audio 内部会调 SetBitsPerSample 等，hertz 可能多次更新；
        // 用最后一次（解析完 MP3 头之后）就是真实采样率
        return true;
    }
    bool SetBitsPerSample(int bits) override {
        return bits == 16;
    }
    bool SetChannels(int ch) override {
        _channels = ch;
        return true;
    }
    bool ConsumeSample(int16_t sample[2]) override {
        if (_pos >= _capacity) return false;
        // mp3 双声道 → 取均值降为 mono；mono 直接拿 sample[0]
        int16_t s = (_channels == 2)
                    ? static_cast<int16_t>(((int32_t)sample[0] + sample[1]) / 2)
                    : sample[0];
        _buf[_pos++] = s;
        return true;
    }
    bool stop() override { return true; }
    bool begin() override { return true; }

    size_t   samples()    const { return _pos; }
    uint32_t sampleRate() const { return _hertz; }

private:
    int16_t* _buf;
    size_t   _capacity;
    size_t   _pos     = 0;
    int      _hertz   = 22050;
    int      _channels = 1;
};

// ─── 内存源：把已下载的 MP3 字节当 AudioFileSource 喂给解码器 ──
class AudioFileSourceMemory : public AudioFileSource {
public:
    AudioFileSourceMemory(const uint8_t* data, size_t len)
        : _data(data), _len(len), _pos(0) {}
    bool open(const char*) override { _pos = 0; return true; }
    uint32_t read(void* buf, uint32_t len) override {
        uint32_t toRead = (len < (uint32_t)(_len - _pos)) ? len : (uint32_t)(_len - _pos);
        memcpy(buf, _data + _pos, toRead);
        _pos += toRead;
        return toRead;
    }
    bool seek(int32_t pos, int how) override {
        if      (how == SEEK_SET) _pos = pos;
        else if (how == SEEK_CUR) _pos += pos;
        else                       _pos = _len + pos;
        return true;
    }
    bool close()  override { return true; }
    bool isOpen() override { return true; }
    uint32_t getSize() override { return _len; }
    uint32_t getPos()  override { return _pos; }
private:
    const uint8_t* _data;
    size_t _len, _pos;
};

// ─── PSRAM 缓冲：解码出来的 PCM 暂存于此 ───────────────────
// 30 秒 × 24kHz × 1ch × 2bytes ≈ 1.4MB，放 PSRAM 不挤
static const size_t PCM_MAX_SAMPLES = 30UL * 24000UL;
static int16_t* g_pcmBuf = nullptr;

// 音量分 5 档（1–5），0 = 静音。NVS 存档位（uint8_t），映射到 M5.Speaker 的 0–255。
// 选档对应：0=mute  1=轻  2=中低  3=中  4=中高  5=最大（不爆音上限）
static const uint8_t SPK_LEVEL_MIN = 0;
static const uint8_t SPK_LEVEL_MAX = 5;
static const uint8_t SPK_LEVEL_DEFAULT = 3;
// 经验值 — 5 档对应 M5.Speaker setVolume 的输入。255 已实测会爆音，停在 220。
static const uint8_t SPK_LEVEL_TO_RAW[SPK_LEVEL_MAX + 1] = {
    0, 60, 100, 140, 180, 220
};

static uint8_t g_speakerLevel = SPK_LEVEL_DEFAULT;

inline uint8_t _levelToRaw(uint8_t level) {
    if (level > SPK_LEVEL_MAX) level = SPK_LEVEL_MAX;
    return SPK_LEVEL_TO_RAW[level];
}

inline void _persistVolume() {
    Preferences p;
    p.begin("audio", false);
    p.putUChar("level", g_speakerLevel);
    p.end();
}

inline void _loadVolume() {
    Preferences p;
    p.begin("audio", true);
    // 兼容老版本：以前存的是 "vol"（0–255）；首次读不到 "level" 时按旧 raw 反推一档
    if (p.isKey("level")) {
        g_speakerLevel = p.getUChar("level", SPK_LEVEL_DEFAULT);
    } else if (p.isKey("vol")) {
        uint8_t oldRaw = p.getUChar("vol", 100);
        // 找最接近的档位
        uint8_t best = SPK_LEVEL_DEFAULT;
        int bestDiff = 9999;
        for (uint8_t i = 0; i <= SPK_LEVEL_MAX; i++) {
            int d = (int)oldRaw - (int)SPK_LEVEL_TO_RAW[i];
            if (d < 0) d = -d;
            if (d < bestDiff) { bestDiff = d; best = i; }
        }
        g_speakerLevel = best;
    } else {
        g_speakerLevel = SPK_LEVEL_DEFAULT;
    }
    if (g_speakerLevel > SPK_LEVEL_MAX) g_speakerLevel = SPK_LEVEL_MAX;
    p.end();
}

inline void _applyVolume() {
    if (M5.Speaker.isEnabled()) {
        M5.Speaker.setVolume(_levelToRaw(g_speakerLevel));
    }
}

void playerVolumeUp() {
    if (g_speakerLevel < SPK_LEVEL_MAX) g_speakerLevel++;
    _persistVolume();
    _applyVolume();
    Serial.printf("[SPK] level = %u/%u (raw %u)\n",
                  g_speakerLevel, SPK_LEVEL_MAX, _levelToRaw(g_speakerLevel));
}

void playerVolumeDown() {
    if (g_speakerLevel > SPK_LEVEL_MIN) g_speakerLevel--;
    _persistVolume();
    _applyVolume();
    Serial.printf("[SPK] level = %u/%u (raw %u)\n",
                  g_speakerLevel, SPK_LEVEL_MAX, _levelToRaw(g_speakerLevel));
}

uint8_t playerGetVolume()      { return _levelToRaw(g_speakerLevel); }
uint8_t playerGetVolumeLevel() { return g_speakerLevel; }
uint8_t playerGetVolumeMax()   { return SPK_LEVEL_MAX; }

void playerInit() {
    // CoreS3 mic/speaker 共享 I2S0，这里只 config 不 begin —
    // 真正 begin 推迟到 playMp3Buffer 入口（先 M5.Mic.end() 让出总线）。
    auto cfg = M5.Speaker.config();
    cfg.sample_rate     = 24000;     // 仅作默认；playRaw 会按实际 MP3 速率覆盖
    cfg.stereo          = false;
    cfg.dma_buf_len     = 256;
    cfg.dma_buf_count   = 8;
    cfg.task_priority   = 2;
    cfg.task_pinned_core = -1;
    cfg.use_dac         = false;     // CoreS3 不走内置 DAC，走 AW88298 I2S
    M5.Speaker.config(cfg);

    g_pcmBuf = (int16_t*)ps_malloc(PCM_MAX_SAMPLES * sizeof(int16_t));
    if (!g_pcmBuf) {
        Serial.println("[SPK] PSRAM alloc for PCM buf FAILED");
        return;
    }
    _loadVolume();
    Serial.printf("[SPK] config staged (AW88298 — begin on demand), PCM buf %u KB, level=%u/%u\n",
                  (unsigned)(PCM_MAX_SAMPLES * sizeof(int16_t) / 1024),
                  g_speakerLevel, SPK_LEVEL_MAX);
}

// 从已下载的 MP3 字节缓冲解码并播放（阻塞直到播完）
void playMp3Buffer(const uint8_t* data, size_t len) {
    if (!data || len == 0 || !g_pcmBuf) return;
    Serial.printf("[SPK] Decoding MP3 %u bytes...\n", (unsigned)len);

    // I2S 切换：让出 Mic（如果之前在用），再拿 Speaker
    if (M5.Mic.isEnabled()) M5.Mic.end();
    if (!M5.Speaker.isEnabled()) {
        if (!M5.Speaker.begin()) {
            Serial.println("[SPK] M5.Speaker.begin() failed — AW88298 not responding");
            return;
        }
        M5.Speaker.setVolume(_levelToRaw(g_speakerLevel));
        // AW88298 上电后 ~80ms 才稳定。直接喂数据头部会有「啪」一声爆音。
        // 喂一段静音让功放进入稳态、自动调谐 DC 偏置，再播真实音频。
        static int16_t silenceBuf[1200] = {0};   // 50ms @ 24kHz
        M5.Speaker.playRaw(silenceBuf, 1200, 24000, false, 1, -1);
        while (M5.Speaker.isPlaying()) { delay(2); }
        delay(30);
    } else {
        // 之前已经在播，跨 MP3 也同步当前音量
        M5.Speaker.setVolume(_levelToRaw(g_speakerLevel));
    }

    // 1. 解码 MP3 → PCM 暂存到 PSRAM
    auto* src    = new AudioFileSourceMemory(data, len);
    auto* bufSrc = new AudioFileSourceBuffer(src, 2048);
    auto* sink   = new PCMCaptureSink(g_pcmBuf, PCM_MAX_SAMPLES);
    auto* mp3dec = new AudioGeneratorMP3();

    uint32_t t0 = millis();
    if (!mp3dec->begin(bufSrc, sink)) {
        Serial.println("[SPK] MP3 decoder begin failed");
        delete mp3dec; delete sink; delete bufSrc; delete src;
        return;
    }
    while (mp3dec->isRunning()) {
        if (!mp3dec->loop()) {
            mp3dec->stop();
            break;
        }
    }
    size_t   samples = sink->samples();
    uint32_t rate    = sink->sampleRate();
    uint32_t dt      = millis() - t0;

    delete mp3dec;
    delete sink;
    delete bufSrc;
    delete src;

    if (samples == 0 || rate == 0) {
        Serial.println("[SPK] decode produced 0 samples");
        return;
    }
    Serial.printf("[SPK] Decoded %u samples @ %u Hz in %u ms — playing\n",
                  (unsigned)samples, (unsigned)rate, (unsigned)dt);

    // 2. 一次性丢给 M5.Speaker 播放（mono），等待播完
    if (!M5.Speaker.playRaw(g_pcmBuf, samples, rate, false /* stereo */, 1, -1)) {
        Serial.println("[SPK] playRaw queue rejected");
        return;
    }
    while (M5.Speaker.isPlaying()) {
        delay(10);
    }
    Serial.println("[SPK] Playback done");
}

// 兼容旧接口：流式 URL 播放暂不实现（生产管道全用 buffer 版本）
inline void playMp3Url(const String& /*url*/) {
    Serial.println("[SPK] playMp3Url() not supported on CoreS3 build "
                   "(use playMp3Buffer with downloaded data)");
}
