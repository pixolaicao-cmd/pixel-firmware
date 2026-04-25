#pragma once
/**
 * Pixel AI — 设备配对流程
 *
 * 启动时检查 NVS 是否有 device_token：
 *   有 → 跳过配对，直接用
 *   无 → 向后端注册，显示 8 位配对码（字母+数字），等用户在 App 输入
 */

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "config.h"

// ── NVS 持久化 ───────────────────────────────────────────────

static Preferences _prefs;

String nvsRead(const char* key) {
    _prefs.begin(NVS_NAMESPACE, true);
    String val = _prefs.getString(key, "");
    _prefs.end();
    return val;
}

void nvsWrite(const char* key, const String& value) {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.putString(key, value);
    _prefs.end();
}

void nvsClear(const char* key) {
    _prefs.begin(NVS_NAMESPACE, false);
    _prefs.remove(key);
    _prefs.end();
}

// 设备唯一 ID（用 MAC 地址）
String getDeviceId() {
    String cached = nvsRead(NVS_DEVICE_ID_KEY);
    if (cached.length() > 0) return cached;

    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    String id = String(macStr);
    nvsWrite(NVS_DEVICE_ID_KEY, id);
    return id;
}

// ── HTTP 工具（引用 api.h 中的 httpPostJson / httpGetJson） ──
// 在 api.h 中定义，这里直接调用

extern String httpPostJson(const char* path, const String& body, int timeoutMs = HTTP_TIMEOUT_MS);
extern String httpGetJson(const char* path, const String& deviceToken = "");

// ── 配对流程 ─────────────────────────────────────────────────

struct PairingResult {
    bool success;
    String deviceToken;
    String errorMsg;
};

/**
 * 向后端注册设备，返回 8 位配对码（字母+数字，已排除 0/O/1/I/L）。
 * 若已配对，返回 "PAIRED"（调用方跳过配对）。
 */
String registerDevice(const String& deviceId) {
    JsonDocument req;
    req["device_id"]        = deviceId;
    req["firmware_version"] = "0.2.0";
    req["model"]            = "CoreS3";
    String body;
    serializeJson(req, body);

    String resp = httpPostJson("/api/devices/register", body);
    if (resp.isEmpty()) return "";

    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return "";

    String status = doc["status"].as<String>();
    if (status == "already_paired") return "PAIRED";
    return doc["pairing_code"].as<String>();
}

/**
 * 轮询后端，等待用户在 App 输入配对码。
 * 返回 device_token 或空字符串（超时/失败）。
 */
String waitForPairing(const String& deviceId,
                      void (*onTick)(int secondsLeft) = nullptr) {
    unsigned long start = millis();
    while (millis() - start < PAIRING_TIMEOUT_MS) {
        String resp = httpGetJson(("/api/devices/status/" + deviceId).c_str());
        if (!resp.isEmpty()) {
            JsonDocument doc;
            if (deserializeJson(doc, resp) == DeserializationError::Ok) {
                String status = doc["status"].as<String>();
                if (status == "paired") {
                    return doc["device_token"].as<String>();
                }
            }
        }
        int remaining = (PAIRING_TIMEOUT_MS - (millis() - start)) / 1000;
        if (onTick) onTick(remaining);
        delay(PAIRING_POLL_MS);
    }
    return "";  // 超时
}

/**
 * 完整配对入口。
 * 返回最终的 device_token（已保存到 NVS）。
 * 屏幕回调由调用方注入，保持 pairing.h 与显示解耦。
 */
String runPairingFlow(
    void (*showCode)(const String& code, int secondsLeft),
    void (*showWaiting)(int secondsLeft),
    void (*showError)(const String& msg)
) {
    // 1. 先检查 NVS
    String saved = nvsRead(NVS_TOKEN_KEY);
    if (saved.length() > 0) return saved;

    String deviceId = getDeviceId();
    Serial.printf("[Pair] Device ID: %s\n", deviceId.c_str());

    // 2. 注册，拿配对码
    String code = registerDevice(deviceId);
    if (code.isEmpty()) {
        if (showError) showError("Server unreachable");
        return "";
    }
    if (code == "PAIRED") {
        // 已在服务端绑定但本地 NVS 丢失——重新拿 token（需要用户重新扫码）
        // 这种情况极少，清空 NVS 重新走流程
        code = registerDevice(deviceId);  // 刷新配对码
        if (code.isEmpty() || code == "PAIRED") {
            if (showError) showError("Re-pair needed");
            return "";
        }
    }

    Serial.printf("[Pair] Pairing code: %s\n", code.c_str());

    // 3. 显示配对码，等用户在 App 输入
    int remaining = PAIRING_TIMEOUT_MS / 1000;
    if (showCode) showCode(code, remaining);

    // 4. 轮询后端
    String token = waitForPairing(deviceId, [&](int sec) {
        if (showWaiting) showWaiting(sec);
    });

    if (token.isEmpty()) {
        if (showError) showError("Pairing timeout");
        return "";
    }

    // 5. 保存到 NVS Flash
    nvsWrite(NVS_TOKEN_KEY, token);
    Serial.printf("[Pair] Paired! Token saved.\n");
    return token;
}
