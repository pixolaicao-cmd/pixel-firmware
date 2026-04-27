#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "display.h"

Preferences prefs;
WebServer apServer(80);

// 从 flash 读取保存的 WiFi 凭据
bool loadWiFiCreds(String& ssid, String& password) {
    prefs.begin("wifi", true);
    ssid     = prefs.getString("ssid", "");
    password = prefs.getString("pass", "");
    prefs.end();
    return ssid.length() > 0;
}

void saveWiFiCreds(const String& ssid, const String& password) {
    prefs.begin("wifi", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    prefs.end();
}

// 配网热点页面
const char* CONFIG_PAGE = R"HTML(
<!DOCTYPE html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pixel Setup</title>
<style>
  body{font-family:sans-serif;max-width:400px;margin:40px auto;padding:20px}
  h2{color:#333}input,button{width:100%;padding:12px;margin:8px 0;box-sizing:border-box;border-radius:8px;border:1px solid #ccc;font-size:16px}
  button{background:#000;color:#fff;border:none;cursor:pointer}
  button:hover{background:#333}
</style>
</head><body>
<h2>🪄 Pixel Setup</h2>
<p>Connect Pixel to your WiFi network.</p>
<form action="/save" method="POST">
  <input name="ssid"     placeholder="WiFi Name (SSID)"     required>
  <input name="password" placeholder="WiFi Password" type="password">
  <button type="submit">Connect</button>
</form>
</body></html>
)HTML";

const char* SUCCESS_PAGE = R"HTML(
<!DOCTYPE html><html><body style="font-family:sans-serif;max-width:400px;margin:40px auto;padding:20px">
<h2>✅ Saved!</h2>
<p>Pixel will now connect to your WiFi. You can close this page.</p>
</body></html>
)HTML";

void startCaptivePortal() {
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    displayShow("WiFi Setup", "Connect to:", AP_SSID);
    Serial.printf("[WiFi] AP started: %s  IP: %s\n", AP_SSID, AP_IP);

    apServer.on("/", HTTP_GET, []() {
        apServer.send(200, "text/html", CONFIG_PAGE);
    });

    apServer.on("/save", HTTP_POST, []() {
        String ssid = apServer.arg("ssid");
        String pass = apServer.arg("password");
        saveWiFiCreds(ssid, pass);
        apServer.send(200, "text/html", SUCCESS_PAGE);
        delay(1000);
        ESP.restart();
    });

    // Captive portal：把所有请求重定向到配置页
    apServer.onNotFound([]() {
        apServer.sendHeader("Location", "http://192.168.4.1/");
        apServer.send(302, "text/plain", "");
    });

    apServer.begin();
    displayShow("WiFi Setup", "Open browser:", AP_IP);

    // 等待用户配置（最多 5 分钟）
    unsigned long start = millis();
    while (millis() - start < 300000) {
        apServer.handleClient();
        delay(10);
    }
}

bool connectWiFi() {
    String ssid, password;
    if (!loadWiFiCreds(ssid, password)) {
        Serial.println("[WiFi] No saved credentials, starting AP");
        startCaptivePortal();
        return false;
    }

    displayShow("Connecting...", ssid.c_str());
    Serial.printf("[WiFi] Connecting to %s\n", ssid.c_str());

    // 两轮尝试，每轮 30s。第一轮失败先 disconnect 再重 begin —— 路由器
    // 偶发握手卡死时这能拉回来。仍然失败才 fallback 到 captive portal。
    for (int round = 1; round <= 2; round++) {
        WiFi.disconnect(true, true);
        delay(200);
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), password.c_str());

        int tries = 0;
        while (WiFi.status() != WL_CONNECTED && tries < 60) {
            delay(500);
            tries++;
            if ((tries % 10) == 0) {
                Serial.printf("[WiFi] still trying... round %d, %ds\n", round, tries / 2);
            }
        }
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
            displayShow("Connected!", WiFi.localIP().toString().c_str());
            return true;
        }
        Serial.printf("[WiFi] round %d failed, retrying...\n", round);
    }

    Serial.println("[WiFi] All retries failed, starting AP");
    startCaptivePortal();
    return false;
}

bool isWiFiConnected() {
    return WiFi.status() == WL_CONNECTED;
}
