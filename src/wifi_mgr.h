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

// ── 多网络记忆（最多 5 条）─────────────────────────────────
// 设计：环形列表，按"最后一次连上"顺序排（slot 0 = 最近用过）
// 每个 slot 用键 ssid0..ssid4 / pass0..pass4 存
// 连接时：扫描周围 → 保存的网络里挑 RSSI 最强的那个 → 连
// 老的单 slot 凭据（"ssid"/"pass"）首次启动自动迁移到 slot 0
#define WIFI_MAX_SLOTS 5

struct WifiSlot {
    String ssid;
    String password;
};

// 读全部 slot；空 ssid 的位置跳过。返回有效条数。
int loadWiFiSlots(WifiSlot out[WIFI_MAX_SLOTS]) {
    prefs.begin("wifi", true);
    int n = 0;
    char k[8];
    for (int i = 0; i < WIFI_MAX_SLOTS; i++) {
        snprintf(k, sizeof(k), "ssid%d", i);
        String s = prefs.getString(k, "");
        if (s.length() == 0) continue;
        snprintf(k, sizeof(k), "pass%d", i);
        String p = prefs.getString(k, "");
        out[n].ssid = s;
        out[n].password = p;
        n++;
    }

    // 一次性迁移：老格式（"ssid"/"pass"）→ 新 slot 0
    // 只在新格式里完全没有这个 SSID 时才迁，避免重复
    if (n < WIFI_MAX_SLOTS) {
        String legacySsid = prefs.getString("ssid", "");
        if (legacySsid.length() > 0) {
            String legacyPass = prefs.getString("pass", "");
            bool dup = false;
            for (int i = 0; i < n; i++) if (out[i].ssid == legacySsid) { dup = true; break; }
            if (!dup) {
                out[n].ssid = legacySsid;
                out[n].password = legacyPass;
                n++;
            }
        }
    }
    prefs.end();
    return n;
}

// 持久化整个 slot 数组（slot 0 永远是最近用过的）
static void _writeAllSlots(const WifiSlot slots[WIFI_MAX_SLOTS], int count) {
    prefs.begin("wifi", false);
    char k[8];
    for (int i = 0; i < WIFI_MAX_SLOTS; i++) {
        snprintf(k, sizeof(k), "ssid%d", i);
        if (i < count) prefs.putString(k, slots[i].ssid);
        else           prefs.remove(k);
        snprintf(k, sizeof(k), "pass%d", i);
        if (i < count) prefs.putString(k, slots[i].password);
        else           prefs.remove(k);
    }
    // 老 key 清掉，防止下次启动又被迁回来
    prefs.remove("ssid");
    prefs.remove("pass");
    prefs.end();
}

// 把 (ssid, password) 提升到 slot 0；其它向后挤；超过 5 个挤掉最旧的
void saveWiFiCreds(const String& ssid, const String& password) {
    if (ssid.length() == 0) return;
    WifiSlot slots[WIFI_MAX_SLOTS];
    int n = loadWiFiSlots(slots);

    // 找已有同名 SSID 的位置（更新密码用）
    int existing = -1;
    for (int i = 0; i < n; i++) if (slots[i].ssid == ssid) { existing = i; break; }

    WifiSlot fresh;
    fresh.ssid = ssid;
    fresh.password = password;

    // 把现有列表整体往后挪一格，前面腾出 slot 0
    WifiSlot reordered[WIFI_MAX_SLOTS];
    int outN = 0;
    reordered[outN++] = fresh;
    for (int i = 0; i < n && outN < WIFI_MAX_SLOTS; i++) {
        if (i == existing) continue;            // 跳过同名（已经放 slot 0 了）
        reordered[outN++] = slots[i];
    }
    _writeAllSlots(reordered, outN);
    Serial.printf("[WiFi] saved '%s' (slot 0); total %d remembered\n",
                  ssid.c_str(), outN);
}

// 后向兼容：旧代码用 loadWiFiCreds 拿 slot 0
bool loadWiFiCreds(String& ssid, String& password) {
    WifiSlot slots[WIFI_MAX_SLOTS];
    int n = loadWiFiSlots(slots);
    if (n == 0) return false;
    ssid = slots[0].ssid;
    password = slots[0].password;
    return true;
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

// 单网络连接尝试 — 8 秒内连不上就放弃
// 比之前的 30s × 2 round 短得多 — 这是 #2 (失败检测加速) 的核心
// 多网络场景下宁可快速失败、试下一个，也不要在错的 SSID 上耗 60 秒
static bool _tryConnect(const String& ssid, const String& password, int timeoutSec = 8) {
    WiFi.disconnect(true, true);
    delay(150);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), password.c_str());

    int tries = 0;
    int maxTries = timeoutSec * 2;  // 500ms/次
    while (WiFi.status() != WL_CONNECTED && tries < maxTries) {
        delay(500);
        tries++;
    }
    return WiFi.status() == WL_CONNECTED;
}

bool connectWiFi() {
    WifiSlot slots[WIFI_MAX_SLOTS];
    int n = loadWiFiSlots(slots);
    if (n == 0) {
        Serial.println("[WiFi] No saved networks, starting captive portal");
        startCaptivePortal();
        return false;
    }

    // 1. 扫一圈周围 WiFi，按 RSSI 排序
    displayShow("Scanning WiFi...", "", "");
    Serial.println("[WiFi] Scanning...");
    int found = WiFi.scanNetworks(false /*async*/, true /*hidden*/);
    if (found < 0) found = 0;

    // 2. 在保存的网络里挑一个【出现在扫描结果里】+【RSSI 最强】的
    //    没扫到也没关系 — 后面会盲尝 slot 0（有的路由器对 hidden SSID 不响应扫描）
    int bestSlotIdx = -1;
    int bestRssi = -999;
    for (int s = 0; s < n; s++) {
        for (int f = 0; f < found; f++) {
            if (WiFi.SSID(f) == slots[s].ssid) {
                int rssi = WiFi.RSSI(f);
                if (rssi > bestRssi) {
                    bestRssi = rssi;
                    bestSlotIdx = s;
                }
                break;  // 同 SSID 多个 AP，挑第一个匹配（已经按强排过了）
            }
        }
    }
    WiFi.scanDelete();

    // 3. 优先连"扫到的最强匹配"；如果什么也没匹配上，按记忆顺序盲试
    int order[WIFI_MAX_SLOTS];
    int orderN = 0;
    if (bestSlotIdx >= 0) {
        order[orderN++] = bestSlotIdx;
        Serial.printf("[WiFi] Best match: '%s' rssi=%d (slot %d)\n",
                      slots[bestSlotIdx].ssid.c_str(), bestRssi, bestSlotIdx);
    }
    for (int s = 0; s < n; s++) {
        if (s == bestSlotIdx) continue;
        order[orderN++] = s;
    }

    // 4. 按顺序尝试，每个最多 8 秒
    for (int i = 0; i < orderN; i++) {
        const WifiSlot& slot = slots[order[i]];
        Serial.printf("[WiFi] Trying '%s' (%d/%d)\n", slot.ssid.c_str(), i + 1, orderN);
        displayShow("Connecting...", slot.ssid.c_str());
        if (_tryConnect(slot.ssid, slot.password, 8)) {
            Serial.printf("[WiFi] Connected to '%s'! IP: %s\n",
                          slot.ssid.c_str(), WiFi.localIP().toString().c_str());
            displayShow("Connected!", slot.ssid.c_str(),
                        WiFi.localIP().toString().c_str());
            // 这次连上的 SSID 提到 slot 0，下次开机直接命中
            if (order[i] != 0) {
                saveWiFiCreds(slot.ssid, slot.password);
            }
            return true;
        }
    }

    Serial.printf("[WiFi] All %d saved networks failed, starting captive portal\n", orderN);
    startCaptivePortal();
    return false;
}

bool isWiFiConnected() {
    return WiFi.status() == WL_CONNECTED;
}
