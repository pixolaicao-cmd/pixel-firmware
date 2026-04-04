#pragma once
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include <AudioFileSourceBuffer.h>
#include <AudioFileSourceHTTPStream.h>
#include "config.h"

#define I2S_SPK_PORT    I2S_NUM_1

AudioGeneratorMP3*        mp3    = nullptr;
AudioOutputI2S*           i2sOut = nullptr;
AudioFileSourceHTTPStream* http  = nullptr;
AudioFileSourceBuffer*    buf    = nullptr;

void playerInit() {
    i2sOut = new AudioOutputI2S(I2S_SPK_PORT);
    i2sOut->SetPinout(SPK_BCLK_PIN, SPK_LRC_PIN, SPK_DIN_PIN);
    i2sOut->SetGain(0.5);
    Serial.println("[SPK] I2S speaker initialized");
}

// 播放指定 URL 的 MP3（阻塞直到播完）
void playMp3Url(const String& url) {
    Serial.printf("[SPK] Playing: %s\n", url.c_str());

    if (mp3) { mp3->stop(); delete mp3; mp3 = nullptr; }
    if (buf) { delete buf; buf = nullptr; }
    if (http) { delete http; http = nullptr; }

    http = new AudioFileSourceHTTPStream(url.c_str());
    buf  = new AudioFileSourceBuffer(http, 4096);
    mp3  = new AudioGeneratorMP3();
    mp3->begin(buf, i2sOut);

    while (mp3->isRunning()) {
        if (!mp3->loop()) {
            mp3->stop();
            break;
        }
    }

    delete mp3;  mp3  = nullptr;
    delete buf;  buf  = nullptr;
    delete http; http = nullptr;
}

// 从内存缓冲区播放 MP3（已下载的数据）
class AudioFileSourceMemory : public AudioFileSource {
public:
    AudioFileSourceMemory(const uint8_t* data, size_t len) : _data(data), _len(len), _pos(0) {}
    bool open(const char*) override { _pos = 0; return true; }
    uint32_t read(void* buf, uint32_t len) override {
        uint32_t toRead = min(len, (uint32_t)(_len - _pos));
        memcpy(buf, _data + _pos, toRead);
        _pos += toRead;
        return toRead;
    }
    bool seek(int32_t pos, int how) override {
        if (how == SEEK_SET) _pos = pos;
        else if (how == SEEK_CUR) _pos += pos;
        else _pos = _len + pos;
        return true;
    }
    bool close() override { return true; }
    bool isOpen() override { return true; }
    uint32_t getSize() override { return _len; }
    uint32_t getPos() override { return _pos; }
private:
    const uint8_t* _data;
    size_t _len, _pos;
};

void playMp3Buffer(const uint8_t* data, size_t len) {
    if (!data || len == 0) return;
    Serial.printf("[SPK] Playing MP3 from buffer (%d bytes)\n", (int)len);

    if (mp3) { mp3->stop(); delete mp3; mp3 = nullptr; }

    auto* src = new AudioFileSourceMemory(data, len);
    auto* bufSrc = new AudioFileSourceBuffer(src, 2048);
    mp3 = new AudioGeneratorMP3();
    mp3->begin(bufSrc, i2sOut);

    while (mp3->isRunning()) {
        if (!mp3->loop()) { mp3->stop(); break; }
    }

    delete mp3;   mp3 = nullptr;
    delete bufSrc;
    delete src;
}
