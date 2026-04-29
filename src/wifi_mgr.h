#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "display.h"

Preferences prefs;
WebServer  apServer(80);
DNSServer  dnsServer;

// 从 AP MAC 派生 SSID + 密码 — 每台设备独立
// MAC 取 AP 模式的，6 字节稳定（出厂烧死，不会随 STA 漂）
static void _macSuffix(char out[7]) {
    uint8_t mac[6];
    WiFi.softAPmacAddress(mac);
    snprintf(out, 7, "%02X%02X%02X", mac[3], mac[4], mac[5]);
}

String apSsid() {
    char suf[7]; _macSuffix(suf);
    return String(AP_SSID_PREFIX) + suf;   // e.g. "Pixel-A1B2C3"
}

// WPA2 密码至少 8 字符。"pixel-" + 6 hex = 12 字符，OK。
// 密码不是机密 — 它打印在 LCD 上给当面用户看；目的是防"路过的陌生人蹭网"
String apPassword() {
    char suf[7]; _macSuffix(suf);
    return String("pixel-") + suf;          // e.g. "pixel-A1B2C3"
}

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

// ── 配网页面 ────────────────────────────────────────────────────
// 进入页面立即调 /scan 拿附近 WiFi 列表，点击 SSID 就填进表单
const char* CONFIG_PAGE = R"HTML(
<!DOCTYPE html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pixel Setup</title>
<style>
  *{box-sizing:border-box}
  body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;max-width:420px;margin:0 auto;padding:20px;background:#fafafa;color:#222}
  h2{margin:0 0 4px;font-size:24px}
  .sub{color:#666;font-size:14px;margin-bottom:20px}
  .card{background:#fff;border-radius:12px;padding:16px;margin-bottom:16px;box-shadow:0 1px 3px rgba(0,0,0,0.06)}
  .card h3{margin:0 0 12px;font-size:15px;color:#444;font-weight:600;display:flex;justify-content:space-between;align-items:center}
  .net{display:flex;justify-content:space-between;align-items:center;padding:12px 8px;border-bottom:1px solid #eee;cursor:pointer;transition:background 0.1s}
  .net:last-child{border-bottom:none}
  .net:hover,.net.sel{background:#f0f7ff}
  .net.sel{font-weight:600}
  .ssid{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;margin-right:8px}
  .rssi{color:#888;font-size:13px}
  .lock{color:#888;font-size:13px;margin-left:6px}
  input,button{width:100%;padding:14px;margin:6px 0;border-radius:10px;border:1px solid #ddd;font-size:16px;background:#fff}
  input:focus{outline:none;border-color:#0a84ff}
  button{background:#0a84ff;color:#fff;border:none;cursor:pointer;font-weight:600;margin-top:12px}
  button:active{background:#0670d8}
  button.refresh{background:transparent;color:#0a84ff;border:1px solid #0a84ff;padding:6px 14px;width:auto;font-size:13px;margin:0}
  .empty{color:#999;text-align:center;padding:24px 0;font-size:14px}
  .spin{display:inline-block;width:14px;height:14px;border:2px solid #ddd;border-top-color:#0a84ff;border-radius:50%;animation:s 0.7s linear infinite;vertical-align:-2px;margin-right:6px}
  @keyframes s{to{transform:rotate(360deg)}}
</style>
</head><body>
<h2>🪄 Pixel Setup</h2>
<div class="sub">Pick your WiFi network</div>

<div class="card">
  <h3>
    <span>Networks nearby</span>
    <button class="refresh" id="refreshBtn" type="button" onclick="scan()">Refresh</button>
  </h3>
  <div id="list"><div class="empty"><span class="spin"></span>Scanning…</div></div>
</div>

<form action="/save" method="POST" id="form">
  <div class="card">
    <h3>Connect</h3>
    <input id="ssid" name="ssid" placeholder="WiFi name (or pick above)" required autocapitalize="off" autocorrect="off" spellcheck="false">
    <input id="pass" name="password" type="password" placeholder="Password (leave empty if open)">
    <button type="submit">Connect</button>
  </div>
</form>

<script>
function pickSsid(name, secured) {
  document.getElementById('ssid').value = name;
  document.querySelectorAll('.net').forEach(n => n.classList.remove('sel'));
  event.currentTarget.classList.add('sel');
  if (secured) document.getElementById('pass').focus();
}
function rssiBars(rssi) {
  if (rssi >= -55) return '▮▮▮▮';
  if (rssi >= -65) return '▮▮▮▯';
  if (rssi >= -75) return '▮▮▯▯';
  return '▮▯▯▯';
}
function render(nets) {
  var list = document.getElementById('list');
  if (!nets || !nets.length) {
    list.innerHTML = '<div class="empty">No networks found. Tap Refresh.</div>';
    return;
  }
  list.innerHTML = nets.map(function(n){
    var lock = n.s ? '🔒' : '';
    return '<div class="net" onclick="pickSsid(\'' +
      n.n.replace(/'/g, "\\'") + '\',' + (n.s?1:0) + ')">' +
      '<span class="ssid">' + n.n.replace(/</g,'&lt;') + '</span>' +
      '<span class="rssi">' + rssiBars(n.r) + '</span>' +
      '<span class="lock">' + lock + '</span></div>';
  }).join('');
}
async function scan() {
  var btn = document.getElementById('refreshBtn');
  btn.disabled = true; btn.textContent = 'Scanning…';
  document.getElementById('list').innerHTML = '<div class="empty"><span class="spin"></span>Scanning…</div>';
  try {
    var r = await fetch('/scan', {cache:'no-store'});
    var data = await r.json();
    render(data.networks || []);
  } catch(e) {
    document.getElementById('list').innerHTML = '<div class="empty">Scan failed. Try again.</div>';
  } finally {
    btn.disabled = false; btn.textContent = 'Refresh';
  }
}
scan();
</script>
</body></html>
)HTML";

const char* SUCCESS_PAGE = R"HTML(
<!DOCTYPE html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Saved</title>
<style>body{font-family:-apple-system,sans-serif;max-width:420px;margin:60px auto;padding:24px;text-align:center}
h2{font-size:32px;margin:0 0 12px}p{color:#555;line-height:1.5}</style>
</head><body>
<h2>✅ Saved!</h2>
<p>Pixel will restart and connect to your WiFi.<br>You can close this page.</p>
</body></html>
)HTML";

// ── /scan：回 JSON 附近 WiFi 列表（按信号强度排序、去重、limit 20）─
String _scanNetworksJson() {
    int n = WiFi.scanNetworks(false /*async*/, true /*hidden*/);
    if (n < 0) n = 0;

    // 按 RSSI 排序索引
    int order[40];
    int total = (n > 40) ? 40 : n;
    for (int i = 0; i < total; i++) order[i] = i;
    for (int i = 0; i < total - 1; i++) {
        for (int j = 0; j < total - 1 - i; j++) {
            if (WiFi.RSSI(order[j]) < WiFi.RSSI(order[j + 1])) {
                int t = order[j]; order[j] = order[j + 1]; order[j + 1] = t;
            }
        }
    }

    // 去重 SSID（同一 SSID 只留信号最强那个）+ 取前 20
    String json = "{\"networks\":[";
    String seen = "|";
    int kept = 0;
    for (int idx = 0; idx < total && kept < 20; idx++) {
        int i = order[idx];
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        String key = "|" + ssid + "|";
        if (seen.indexOf(key) >= 0) continue;
        seen += ssid + "|";

        // JSON 转义
        String esc;
        for (size_t k = 0; k < ssid.length(); k++) {
            char c = ssid[k];
            if (c == '"' || c == '\\') { esc += '\\'; esc += c; }
            else if (c == '\n') esc += "\\n";
            else if ((uint8_t)c < 0x20) ;  // skip control
            else esc += c;
        }

        bool secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        if (kept > 0) json += ",";
        json += "{\"n\":\"" + esc + "\",\"r\":" + String(WiFi.RSSI(i))
              + ",\"s\":" + (secured ? "1" : "0") + "}";
        kept++;
    }
    json += "]}";
    WiFi.scanDelete();
    return json;
}

void startCaptivePortal() {
    // 1. 启 AP — SSID 和密码都从 MAC 派生（每台设备唯一）
    WiFi.mode(WIFI_AP);
    String ssid = apSsid();
    String pass = apPassword();
    WiFi.softAP(ssid.c_str(), pass.c_str());
    delay(100);
    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("[WiFi] AP started: %s (pwd: %s)  IP: %s\n",
                  ssid.c_str(), pass.c_str(), apIP.toString().c_str());
    // 屏幕同时显示 SSID + 密码 — 用户当面看着抄
    displayShow(ssid.c_str(), "Password:", pass.c_str());

    // 2. DNS 全域名劫持 → 任何域名都解析回 192.168.4.1
    //    这是触发手机"自动弹出登录页"的关键 — 没有这一步只能让用户手动输 IP
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", apIP);

    // 3. 真正的配置页
    apServer.on("/", HTTP_GET, []() {
        apServer.sendHeader("Cache-Control", "no-store");
        apServer.send(200, "text/html", CONFIG_PAGE);
    });

    apServer.on("/scan", HTTP_GET, []() {
        String json = _scanNetworksJson();
        apServer.sendHeader("Cache-Control", "no-store");
        apServer.send(200, "application/json", json);
    });

    apServer.on("/save", HTTP_POST, []() {
        String ssid = apServer.arg("ssid");
        String pass = apServer.arg("password");
        if (ssid.length() == 0) {
            apServer.send(400, "text/plain", "SSID required");
            return;
        }
        saveWiFiCreds(ssid, pass);
        apServer.send(200, "text/html", SUCCESS_PAGE);
        Serial.printf("[WiFi] Saved creds for SSID: %s — restarting\n", ssid.c_str());
        delay(1500);
        ESP.restart();
    });

    // 4. iOS / macOS captive 探测路径 — 必须返回 200 + 非苹果 success 标记，
    //    OS 才认定"需要登录"并自动弹页面
    auto handleCaptive = []() {
        apServer.sendHeader("Location", "http://192.168.4.1/", true);
        apServer.send(302, "text/plain", "");
    };
    // iOS/macOS:
    apServer.on("/hotspot-detect.html", HTTP_GET, handleCaptive);
    apServer.on("/library/test/success.html", HTTP_GET, handleCaptive);
    // Android (Chromium):
    apServer.on("/generate_204",  HTTP_GET, handleCaptive);
    apServer.on("/gen_204",       HTTP_GET, handleCaptive);
    // Windows:
    apServer.on("/connecttest.txt", HTTP_GET, handleCaptive);
    apServer.on("/ncsi.txt",         HTTP_GET, handleCaptive);
    apServer.on("/redirect",         HTTP_GET, handleCaptive);

    // 5. 兜底：所有其他请求重定向到根
    apServer.onNotFound([]() {
        apServer.sendHeader("Location", "http://192.168.4.1/", true);
        apServer.send(302, "text/plain", "");
    });

    apServer.begin();
    // 配网状态保持 SSID/密码可见 — 用户连上前要看着抄
    // (captive portal 弹页面后用户也只需点选 → 不必再切屏)

    // 6. 服务循环（最多 5 分钟，期间 dnsServer + apServer 都得喂）
    unsigned long start = millis();
    while (millis() - start < 300000) {
        dnsServer.processNextRequest();
        apServer.handleClient();
        delay(2);
    }
    // 超时退出（dnsServer.stop 由 ESP.restart 之前的析构处理）
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
