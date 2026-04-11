#pragma once
/**
 * Pixel AI — 录音层
 * 目标：M5Stack CoreS3 内置 PDM 麦克风（SPM1423）
 *   CLK = GPIO0 (MIC_SCK_PIN)
 *   DAT = GPIO14 (MIC_SD_PIN)
 *
 * ESP-IDF I2S 驱动以 PDM_RX 模式读取，
 * 输出标准 16-bit mono WAV（含文件头）。
 */

#include <driver/i2s.h>
#include "config.h"

#define I2S_MIC_PORT    I2S_NUM_0

// ---- WAV 文件头（44 字节）----
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

void recorderInit() {
    // SPM1423 是 PDM 麦克风，使用 I2S_MODE_PDM | I2S_MODE_RX
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
        .sample_rate          = SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_PCM_SHORT,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 512,
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0,
    };

    // PDM 模式下只使用 CLK (bck) 和 DATA (data_in)；ws 不需要
    i2s_pin_config_t pins = {
        .bck_io_num   = MIC_SCK_PIN,       // PDM CLK = GPIO0
        .ws_io_num    = I2S_PIN_NO_CHANGE,  // PDM 不用 WS
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = MIC_SD_PIN,         // PDM DATA = GPIO14
    };

    esp_err_t err = i2s_driver_install(I2S_MIC_PORT, &cfg, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[MIC] i2s_driver_install error: %d\n", (int)err);
        return;
    }
    i2s_set_pin(I2S_MIC_PORT, &pins);
    i2s_zero_dma_buffer(I2S_MIC_PORT);
    Serial.println("[MIC] PDM mic initialized (SPM1423)");
}

/**
 * 录音并将结果写入 outBuf（WAV header + PCM data）
 * maxMs：最长录音时长（毫秒）
 * 返回：写入 outBuf 的总字节数（含 44 字节 header）
 */
size_t recordToBuffer(uint8_t* outBuf, size_t outBufSize, uint32_t maxMs) {
    const size_t headerSize  = sizeof(WavHeader);
    uint8_t*     audioBuf    = outBuf + headerSize;
    size_t       audioMaxBytes = outBufSize - headerSize;
    size_t       audioBytes  = 0;

    uint32_t startMs = millis();
    int16_t  raw16[256];  // PDM 直接输出 16-bit
    size_t   bytesRead = 0;

    while (millis() - startMs < maxMs && audioBytes < audioMaxBytes) {
        i2s_read(I2S_MIC_PORT, raw16, sizeof(raw16), &bytesRead, portMAX_DELAY);
        size_t samples = bytesRead / 2;
        size_t toCopy  = min(samples * 2, audioMaxBytes - audioBytes);
        memcpy(audioBuf + audioBytes, raw16, toCopy);
        audioBytes += toCopy;
    }

    // 写 WAV header
    WavHeader hdr;
    hdr.dataSize  = (uint32_t)audioBytes;
    hdr.chunkSize = 36 + (uint32_t)audioBytes;
    memcpy(outBuf, &hdr, headerSize);

    Serial.printf("[MIC] Recorded %d bytes audio (%d ms)\n",
                  (int)audioBytes, (int)(millis() - startMs));
    return headerSize + audioBytes;
}
