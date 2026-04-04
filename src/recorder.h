#pragma once
#include <driver/i2s.h>
#include "config.h"

#define I2S_MIC_PORT    I2S_NUM_0

// WAV 文件头结构
struct WavHeader {
    char    riff[4]        = {'R','I','F','F'};
    uint32_t chunkSize;
    char    wave[4]        = {'W','A','V','E'};
    char    fmt[4]         = {'f','m','t',' '};
    uint32_t subchunk1Size = 16;
    uint16_t audioFormat   = 1;   // PCM
    uint16_t numChannels   = 1;   // Mono
    uint32_t sampleRate    = SAMPLE_RATE;
    uint32_t byteRate      = SAMPLE_RATE * 2;
    uint16_t blockAlign    = 2;
    uint16_t bitsPerSample = 16;
    char    data[4]        = {'d','a','t','a'};
    uint32_t dataSize;
};

void recorderInit() {
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,   // INMP441 输出32bit
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 512,
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0,
    };

    i2s_pin_config_t pins = {
        .bck_io_num   = MIC_SCK_PIN,
        .ws_io_num    = MIC_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = MIC_SD_PIN,
    };

    i2s_driver_install(I2S_MIC_PORT, &cfg, 0, NULL);
    i2s_set_pin(I2S_MIC_PORT, &pins);
    i2s_zero_dma_buffer(I2S_MIC_PORT);
    Serial.println("[MIC] I2S mic initialized");
}

// 录音并返回 WAV 字节数组（含 header）
// 返回实际录音的总字节数（header + 音频数据）
size_t recordToBuffer(uint8_t* outBuf, size_t outBufSize, uint32_t maxMs) {
    const size_t headerSize = sizeof(WavHeader);
    uint8_t* audioBuf = outBuf + headerSize;
    size_t audioMaxBytes = outBufSize - headerSize;
    size_t audioBytes = 0;

    uint32_t startMs = millis();
    int32_t raw32[128];
    size_t bytesRead = 0;

    while (millis() - startMs < maxMs && audioBytes < audioMaxBytes) {
        i2s_read(I2S_MIC_PORT, raw32, sizeof(raw32), &bytesRead, portMAX_DELAY);
        size_t samples = bytesRead / 4;

        for (size_t i = 0; i < samples && audioBytes < audioMaxBytes; i++) {
            // INMP441 输出 18bit，高位对齐在 32bit 中，取高 16bit
            int16_t s = (int16_t)(raw32[i] >> 14);
            memcpy(audioBuf + audioBytes, &s, 2);
            audioBytes += 2;
        }
    }

    // 写 WAV header
    WavHeader hdr;
    hdr.dataSize  = audioBytes;
    hdr.chunkSize = 36 + audioBytes;
    memcpy(outBuf, &hdr, headerSize);

    Serial.printf("[MIC] Recorded %d bytes of audio\n", (int)audioBytes);
    return headerSize + audioBytes;
}
