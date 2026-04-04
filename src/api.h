#pragma once
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "config.h"

// ---- HTTP 工具 ----

// 发送 multipart/form-data（上传音频）
// 返回响应 body，失败返回空
String httpPostMultipart(const char* path, const uint8_t* fileData, size_t fileSize,
                         const char* fieldName, const char* fileName, const char* mimeType) {
    WiFiClientSecure client;
    client.setInsecure();   // 跳过 SSL 证书验证（生产可换成证书指纹）
    client.setTimeout(20000);

    if (!client.connect(API_HOST, API_PORT)) {
        Serial.println("[API] Connection failed");
        return "";
    }

    const String boundary = "----PixelBoundary7MA4YWxkTrZu0gW";
    String partHeader = "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"" + fieldName + "\"; filename=\"" + fileName + "\"\r\n"
        "Content-Type: " + mimeType + "\r\n\r\n";
    String partFooter = "\r\n--" + boundary + "--\r\n";

    size_t contentLength = partHeader.length() + fileSize + partFooter.length();

    // Request headers
    client.printf("POST %s HTTP/1.1\r\n", path);
    client.printf("Host: %s\r\n", API_HOST);
    client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary.c_str());
    client.printf("Content-Length: %d\r\n", (int)contentLength);
    client.printf("Connection: close\r\n\r\n");

    // Body
    client.print(partHeader);
    const size_t CHUNK = 1024;
    size_t sent = 0;
    while (sent < fileSize) {
        size_t toSend = min(CHUNK, fileSize - sent);
        client.write(fileData + sent, toSend);
        sent += toSend;
    }
    client.print(partFooter);

    // Read response
    String response = "";
    unsigned long timeout = millis() + 15000;
    bool headersEnded = false;
    while (client.connected() && millis() < timeout) {
        while (client.available()) {
            String line = client.readStringUntil('\n');
            if (!headersEnded) {
                if (line == "\r") headersEnded = true;
            } else {
                response += line + "\n";
            }
        }
        if (headersEnded && response.length() > 0) break;
        delay(10);
    }
    client.stop();
    return response;
}

// 发送 JSON POST
String httpPostJson(const char* path, const String& jsonBody) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(20000);

    if (!client.connect(API_HOST, API_PORT)) {
        Serial.println("[API] Connection failed");
        return "";
    }

    client.printf("POST %s HTTP/1.1\r\n", path);
    client.printf("Host: %s\r\n", API_HOST);
    client.printf("Content-Type: application/json\r\n");
    client.printf("Content-Length: %d\r\n", (int)jsonBody.length());
    client.printf("Connection: close\r\n\r\n");
    client.print(jsonBody);

    String response = "";
    unsigned long timeout = millis() + 15000;
    bool headersEnded = false;
    while (client.connected() && millis() < timeout) {
        while (client.available()) {
            String line = client.readStringUntil('\n');
            if (!headersEnded) {
                if (line == "\r") headersEnded = true;
            } else {
                response += line + "\n";
            }
        }
        if (headersEnded && response.length() > 0) break;
        delay(10);
    }
    client.stop();
    return response;
}

// 下载二进制数据到缓冲区，返回实际下载字节数
size_t httpGetBinary(const char* path, uint8_t* outBuf, size_t outBufSize) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(20000);

    if (!client.connect(API_HOST, API_PORT)) return 0;

    client.printf("GET %s HTTP/1.1\r\n", API_HOST);
    client.printf("Host: %s\r\n", API_HOST);
    client.printf("Connection: close\r\n\r\n");

    // 跳过 headers
    unsigned long timeout = millis() + 10000;
    while (client.connected() && millis() < timeout) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }

    size_t total = 0;
    timeout = millis() + 15000;
    while (client.connected() && total < outBufSize && millis() < timeout) {
        if (client.available()) {
            int b = client.read(outBuf + total, outBufSize - total);
            if (b > 0) total += b;
        }
        delay(1);
    }
    client.stop();
    return total;
}

// ---- Pixel API 封装 ----

// 上传 WAV → 返回转写文字
String transcribeAudio(const uint8_t* wavData, size_t wavSize) {
    Serial.println("[API] Transcribing...");
    String resp = httpPostMultipart(
        "/transcribe", wavData, wavSize,
        "file", "recording.wav", "audio/wav"
    );
    if (resp.isEmpty()) return "";

    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return "";
    return doc["text"].as<String>();
}

// 文字 → Pixel 回复
String chatWithPixel(const String& message, const String& userId = "esp32") {
    Serial.printf("[API] Chat: %s\n", message.c_str());
    JsonDocument req;
    req["message"]  = message;
    req["user_id"]  = userId;
    String body;
    serializeJson(req, body);

    String resp = httpPostJson("/chat", body);
    if (resp.isEmpty()) return "";

    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return "";
    return doc["reply"].as<String>();
}

// 文字 → MP3 音频，下载到 outBuf，返回字节数
size_t speakText(const String& text, const String& lang,
                 uint8_t* outBuf, size_t outBufSize) {
    Serial.printf("[API] TTS: %s\n", text.c_str());
    JsonDocument req;
    req["text"] = text;
    req["lang"] = lang;
    String body;
    serializeJson(req, body);

    // 先发 POST，然后直接读取 binary response
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(20000);
    if (!client.connect(API_HOST, API_PORT)) return 0;

    client.printf("POST /speak HTTP/1.1\r\n");
    client.printf("Host: %s\r\n", API_HOST);
    client.printf("Content-Type: application/json\r\n");
    client.printf("Content-Length: %d\r\n", (int)body.length());
    client.printf("Connection: close\r\n\r\n");
    client.print(body);

    // 跳过 headers
    unsigned long timeout = millis() + 10000;
    while (client.connected() && millis() < timeout) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }

    size_t total = 0;
    timeout = millis() + 15000;
    while (client.connected() && total < outBufSize && millis() < timeout) {
        if (client.available()) {
            int b = client.read(outBuf + total, outBufSize - total);
            if (b > 0) total += b;
        }
        delay(1);
    }
    client.stop();
    Serial.printf("[API] TTS downloaded %d bytes\n", (int)total);
    return total;
}

// 自动检测语言（简单规则，云端会更准）
String detectLang(const String& text) {
    for (int i = 0; i < (int)text.length() - 1; i++) {
        uint8_t c = (uint8_t)text[i];
        if (c >= 0xE4 && c <= 0xE9) return "zh";  // 中文 UTF-8 范围
    }
    return "en";  // 默认英文（挪威语也用 en 的 TTS 效果可接受）
}

// 翻译文字，返回翻译结果和目标语言
struct TranslateResult { String text; String targetLang; };

TranslateResult translateText(const String& text, const String& sourceLang) {
    JsonDocument req;
    req["text"]        = text;
    req["source_lang"] = sourceLang;
    req["target_lang"] = "auto";
    String body;
    serializeJson(req, body);

    String resp = httpPostJson("/translate", body);
    if (resp.isEmpty()) return {"", ""};

    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return {"", ""};
    return { doc["translation"].as<String>(), doc["target_lang"].as<String>() };
}
