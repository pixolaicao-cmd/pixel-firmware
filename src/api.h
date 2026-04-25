#pragma once
/**
 * Pixel AI — 云端 API 封装
 * 主接口：voicePipeline()   音频 → 全流程 → MP3（单次调用）
 * 备用接口：transcribeAudio / chatWithPixel / speakText（分步调试用）
 */

#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "config.h"
#include "cert.h"

// ── TLS 配置（统一入口，便于一处切换证书验证策略）────────────
static inline void configureTls(WiFiClientSecure& client) {
#if USE_CERT_PINNING
    // 同时信任 ISRG X1（RSA）和 X2（ECDSA）
    client.setCACert(ISRG_ROOT_X1_PEM);
    // 注：ESP32 的 setCACert 通常只接受一个证书链
    // 若 Vercel 切换到 X2，需改为 client.setCACert(ISRG_ROOT_X2_PEM)
    // 更稳妥做法：ESP-IDF 5.x 的 setCACertBundle 支持多根，后续可升级
#else
    client.setInsecure();  // ⚠️ 开发阶段：跳过证书校验
#endif
}

// ── 内部：HTTP 读响应 body（跳过 headers）────────────────────

static String _readBody(WiFiClientSecure& client, int timeoutMs) {
    // 跳过 headers
    unsigned long t = millis() + timeoutMs;
    while (client.connected() && millis() < t) {
        String line = client.readStringUntil('\n');
        if (line == "\r" || line == "\r\n" || line.length() == 0) break;
    }
    String body = "";
    t = millis() + timeoutMs;
    while (client.connected() && millis() < t) {
        while (client.available()) {
            body += (char)client.read();
            t = millis() + timeoutMs;  // 收到数据就续期
        }
        delay(5);
    }
    return body;
}

// ── 内部：读二进制响应 body（跳过 headers）──────────────────

static size_t _readBinaryBody(WiFiClientSecure& client, uint8_t* buf,
                               size_t bufSize, int timeoutMs) {
    // 跳过 headers，顺便读 X-Transcript / X-Reply header
    unsigned long t = millis() + timeoutMs;
    while (client.connected() && millis() < t) {
        String line = client.readStringUntil('\n');
        if (line == "\r" || line == "\r\n" || line.length() == 0) break;
    }
    size_t total = 0;
    t = millis() + timeoutMs;
    while (client.connected() && total < bufSize && millis() < t) {
        if (client.available()) {
            int n = client.read(buf + total, bufSize - total);
            if (n > 0) { total += n; t = millis() + timeoutMs; }
        }
        delay(1);
    }
    return total;
}

// ── 公开：JSON POST ──────────────────────────────────────────

String httpPostJson(const char* path, const String& body,
                    int timeoutMs = HTTP_TIMEOUT_MS,
                    const String& deviceToken = "") {
    WiFiClientSecure client;
    configureTls(client);
    client.setTimeout(timeoutMs / 1000 + 5);

    if (!client.connect(API_HOST, API_PORT)) {
        Serial.printf("[API] Connect failed: %s\n", path);
        return "";
    }
    client.printf("POST %s HTTP/1.1\r\nHost: %s\r\n", path, API_HOST);
    client.printf("Content-Type: application/json\r\n");
    client.printf("Content-Length: %d\r\n", (int)body.length());
    if (deviceToken.length() > 0)
        client.printf("Authorization: Bearer %s\r\n", deviceToken.c_str());
    client.printf("Connection: close\r\n\r\n");
    client.print(body);

    String resp = _readBody(client, timeoutMs);
    client.stop();
    return resp;
}

// ── 公开：JSON GET ────────────────────────────────────────────

String httpGetJson(const char* path, const String& deviceToken = "") {
    WiFiClientSecure client;
    configureTls(client);
    client.setTimeout(HTTP_TIMEOUT_MS / 1000 + 5);

    if (!client.connect(API_HOST, API_PORT)) return "";
    client.printf("GET %s HTTP/1.1\r\nHost: %s\r\n", path, API_HOST);
    if (deviceToken.length() > 0)
        client.printf("Authorization: Bearer %s\r\n", deviceToken.c_str());
    client.printf("Connection: close\r\n\r\n");

    String resp = _readBody(client, HTTP_TIMEOUT_MS);
    client.stop();
    return resp;
}

// ── 公开：multipart/form-data POST ───────────────────────────

String httpPostMultipart(const char* path,
                         const uint8_t* fileData, size_t fileSize,
                         const char* fieldName, const char* fileName,
                         const char* mimeType,
                         const String& deviceToken = "") {
    WiFiClientSecure client;
    configureTls(client);
    client.setTimeout(HTTP_TIMEOUT_MS / 1000 + 5);

    if (!client.connect(API_HOST, API_PORT)) return "";

    const String bnd = "----PixelBnd7MA4YWxk";
    String head = "--" + bnd + "\r\nContent-Disposition: form-data; name=\"" +
                  fieldName + "\"; filename=\"" + fileName + "\"\r\n" +
                  "Content-Type: " + mimeType + "\r\n\r\n";
    String tail = "\r\n--" + bnd + "--\r\n";
    int contentLen = head.length() + fileSize + tail.length();

    client.printf("POST %s HTTP/1.1\r\nHost: %s\r\n", path, API_HOST);
    client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", bnd.c_str());
    client.printf("Content-Length: %d\r\n", contentLen);
    if (deviceToken.length() > 0)
        client.printf("Authorization: Bearer %s\r\n", deviceToken.c_str());
    client.printf("Connection: close\r\n\r\n");
    client.print(head);

    const size_t CHUNK = 2048;
    size_t sent = 0;
    while (sent < fileSize) {
        size_t n = min(CHUNK, fileSize - sent);
        client.write(fileData + sent, n);
        sent += n;
    }
    client.print(tail);

    String resp = _readBody(client, HTTP_TIMEOUT_MS);
    client.stop();
    return resp;
}

// ============================================================
// 🎯 主接口：音频 → AI → MP3（/api/voice 单次调用）
// 服务端完成 STT + Chat + TTS，设备只做录音和播放
// ============================================================

struct VoiceResult {
    bool    success;
    size_t  mp3Size;       // mp3Buf 里的有效字节数
    String  transcript;   // 转写原文（调试用）
    String  reply;        // AI 回复文字（调试用）
    String  errorMsg;
};

VoiceResult voicePipeline(const uint8_t* wavData, size_t wavSize,
                           uint8_t* mp3Buf, size_t mp3BufSize,
                           const String& deviceToken = "") {
    VoiceResult result = {false, 0, "", "", ""};

    WiFiClientSecure client;
    configureTls(client);
    client.setTimeout(VOICE_TIMEOUT_MS / 1000 + 10);

    if (!client.connect(API_HOST, API_PORT)) {
        result.errorMsg = "connect failed";
        return result;
    }

    const String bnd = "----PixelVoiceBnd";
    String head = "--" + bnd + "\r\nContent-Disposition: form-data; name=\"file\";"
                  " filename=\"rec.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
    String tail = "\r\n--" + bnd + "--\r\n";
    int contentLen = head.length() + wavSize + tail.length();

    client.printf("POST /api/voice HTTP/1.1\r\nHost: %s\r\n", API_HOST);
    client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", bnd.c_str());
    client.printf("Content-Length: %d\r\n", contentLen);
    if (deviceToken.length() > 0)
        client.printf("Authorization: Bearer %s\r\n", deviceToken.c_str());
    client.printf("Connection: close\r\n\r\n");
    client.print(head);

    // 分块上传音频
    const size_t CHUNK = 2048;
    size_t sent = 0;
    while (sent < wavSize) {
        size_t n = min(CHUNK, wavSize - sent);
        client.write(wavData + sent, n);
        sent += n;
    }
    client.print(tail);

    Serial.printf("[API] Voice sent %d bytes, waiting response...\n", (int)wavSize);

    // 读响应 headers（提取 X-Transcript / X-Reply）
    unsigned long t = millis() + VOICE_TIMEOUT_MS;
    int statusCode = 0;
    while (client.connected() && millis() < t) {
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.startsWith("HTTP/")) {
            // "HTTP/1.1 200 OK"
            int sp = line.indexOf(' ');
            if (sp > 0) statusCode = line.substring(sp + 1, sp + 4).toInt();
        }
        if (line.startsWith("X-Transcript:"))
            result.transcript = line.substring(13).trim();
        if (line.startsWith("X-Reply:"))
            result.reply = line.substring(8).trim();
        if (line.length() == 0) break;  // headers 结束
    }

    if (statusCode != 200) {
        // 读错误 body
        String errBody = "";
        t = millis() + 5000;
        while (client.connected() && millis() < t && errBody.length() < 200) {
            if (client.available()) errBody += (char)client.read();
            delay(1);
        }
        client.stop();
        result.errorMsg = String("HTTP ") + statusCode + ": " + errBody;
        return result;
    }

    // 读 MP3 body
    size_t total = 0;
    t = millis() + VOICE_TIMEOUT_MS;
    while (client.connected() && total < mp3BufSize && millis() < t) {
        if (client.available()) {
            int n = client.read(mp3Buf + total, mp3BufSize - total);
            if (n > 0) { total += n; t = millis() + VOICE_TIMEOUT_MS; }
        }
        delay(1);
    }
    client.stop();

    Serial.printf("[API] Voice MP3: %d bytes\n", (int)total);
    if (total < 100) {
        result.errorMsg = "MP3 too small";
        return result;
    }

    result.success = true;
    result.mp3Size  = total;
    return result;
}

// ============================================================
// 分步接口（调试 / 降级备用）
// ============================================================

String transcribeAudio(const uint8_t* wavData, size_t wavSize,
                       const String& deviceToken = "") {
    String resp = httpPostMultipart(
        "/api/transcribe", wavData, wavSize,
        "file", "rec.wav", "audio/wav", deviceToken);
    if (resp.isEmpty()) return "";
    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return "";
    return doc["text"].as<String>();
}

String chatWithPixel(const String& msg, const String& deviceToken = "") {
    JsonDocument req;
    req["message"] = msg;
    req["user_id"] = "esp32";
    String body; serializeJson(req, body);
    String resp = httpPostJson("/api/chat", body, HTTP_TIMEOUT_MS, deviceToken);
    if (resp.isEmpty()) return "";
    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return "";
    return doc["reply"].as<String>();
}

size_t speakText(const String& text, const String& lang,
                 uint8_t* mp3Buf, size_t mp3BufSize,
                 const String& deviceToken = "") {
    WiFiClientSecure client;
    configureTls(client);
    client.setTimeout(HTTP_TIMEOUT_MS / 1000 + 5);
    if (!client.connect(API_HOST, API_PORT)) return 0;

    JsonDocument req;
    req["text"] = text; req["lang"] = lang;
    String body; serializeJson(req, body);

    client.printf("POST /api/speak HTTP/1.1\r\nHost: %s\r\n", API_HOST);
    client.printf("Content-Type: application/json\r\nContent-Length: %d\r\n", (int)body.length());
    if (deviceToken.length() > 0)
        client.printf("Authorization: Bearer %s\r\n", deviceToken.c_str());
    client.printf("Connection: close\r\n\r\n");
    client.print(body);

    return _readBinaryBody(client, mp3Buf, mp3BufSize, HTTP_TIMEOUT_MS);
}

// 简单语言检测
String detectLang(const String& text) {
    for (int i = 0; i < (int)text.length() - 1; i++) {
        if ((uint8_t)text[i] >= 0xE4 && (uint8_t)text[i] <= 0xE9) return "zh";
    }
    return "en";
}

struct TranslateResult { String text; String targetLang; };

TranslateResult translateText(const String& text, const String& srcLang,
                               const String& deviceToken = "") {
    JsonDocument req;
    req["text"] = text; req["source_lang"] = srcLang; req["target_lang"] = "auto";
    String body; serializeJson(req, body);
    String resp = httpPostJson("/api/translate", body, HTTP_TIMEOUT_MS, deviceToken);
    if (resp.isEmpty()) return {"", ""};
    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return {"", ""};
    return {doc["translation"].as<String>(), doc["target_lang"].as<String>()};
}
