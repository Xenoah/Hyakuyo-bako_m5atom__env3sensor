#include <Arduino.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <time.h>

#include "config.h"
#include "history.h"
#include "json_util.h"
#include "matrix_display.h"
#include "sensors/env3_sensor.h"
#include "settings.h"
#include "uploader.h"
#include "web_page.h"

namespace {

class Button {
 public:
  enum Event { kNone, kClick, kLongPress };

  explicit Button(uint8_t pin) : pin_(pin) {}
  void begin() { pinMode(pin_, INPUT); }

  Event update(uint32_t nowMs) {
    bool raw = digitalRead(pin_) == LOW;
    if (raw != lastRaw_) {
      lastRaw_ = raw;
      changedAtMs_ = nowMs;
    }
    if (raw != stable_ && nowMs - changedAtMs_ >= kDebounceMs) {
      stable_ = raw;
      if (stable_) {
        pressedAtMs_ = nowMs;
        longFired_ = false;
      } else if (!longFired_) {
        return kClick;
      }
    }
    if (stable_ && !longFired_ && nowMs - pressedAtMs_ >= kLongPressMs) {
      longFired_ = true;
      return kLongPress;
    }
    return kNone;
  }

 private:
  static constexpr uint32_t kDebounceMs = 30;
  static constexpr uint32_t kLongPressMs = 1000;
  uint8_t pin_;
  bool lastRaw_ = false;
  bool stable_ = false;
  bool longFired_ = false;
  uint32_t changedAtMs_ = 0;
  uint32_t pressedAtMs_ = 0;
};

MatrixDisplay display(PIN_LED);
Button button(PIN_BUTTON);
WebServer server(80);
Preferences prefs;

// センサー。ここに追加して setup() で sensors.add() すれば、
// マトリクス・Web・履歴・CSV・GAS 送信に自動で載る (README「センサーの追加」参照)。
SensorHub sensors;
Env3Sensor env3(Wire, PIN_SDA, PIN_SCL);

uint32_t lastSensorReadMs = 0;
History history;
bool historyStarted = false;
uint32_t lastHistorySec = 0;

bool binaryMode = false;
size_t displayItem = 0;            // 次にマトリクスに流す値 (sensors.metric の番号)
size_t lastShownItem = SIZE_MAX;   // 直前に流した値。番号が戻ったら 1 周したとみなす

Settings settings;
Uploader uploader;
bool networkStarted = false;
DNSServer dnsServer;
bool apMode = false;        // セットアップ用アクセスポイントを出している
bool staRetryInAp = false;  // AP 中も保存済みの Wi-Fi への再接続を試みる
uint32_t lastStaRetryMs = 0;
char apSsid[32] = "";
uint32_t restartAtMs = 0;  // 0 以外ならこの時刻に再起動

bool uploadedOnce = false;
uint32_t lastUploadMs = 0;
long lastUploadSlot = -1;

uint32_t uptimeSec() { return millis() / 1000; }

bool timeSynced(time_t now) { return now > 1700000000; }

String colorHex(uint32_t color) {
  char hex[8];
  snprintf(hex, sizeof(hex), "#%06lx", (unsigned long)color);
  return hex;
}

// ---------------------------------------------------------------- sensor

void recordHistory() {
  if (!sensors.anyValid()) return;
  uint32_t now = uptimeSec();
  if (historyStarted && now - lastHistorySec < HISTORY_INTERVAL_SEC) return;
  historyStarted = true;
  lastHistorySec = now;
  float values[SensorHub::kMaxMetrics];
  for (size_t i = 0; i < sensors.metricCount(); ++i) values[i] = sensors.metric(i).value;
  history.record(now, values);
}

// ---------------------------------------------------------------- display

void formatBinary(char* out, size_t size, long value, const char* unit) {
  char bits[40];
  size_t n = 0;
  unsigned long v = value < 0 ? -value : value;
  do {
    bits[n++] = '0' + (v & 1);
    v >>= 1;
  } while (v && n < sizeof(bits));
  size_t pos = 0;
  if (value < 0 && pos + 1 < size) out[pos++] = '-';
  while (n > 0 && pos + 1 < size) out[pos++] = bits[--n];
  out[pos] = '\0';
  strlcat(out, unit, size);
}

void formatValue(char* out, size_t size, const Metric& m) {
  if (binaryMode) {
    formatBinary(out, size, lroundf(m.value), m.matrixUnit);
  } else {
    snprintf(out, size, "%.*f%s", m.decimals, m.value, m.matrixUnit);
  }
}

void refillDisplay(MatrixDisplay& d) {
  if (!sensors.anyValid()) {
    bool anyOk = false;
    for (Sensor* s : sensors.sensors()) anyOk |= s->ok();
    if (anyOk) {
      d.appendText("...", COLOR_INFO);  // 見つかったが、まだ最初の値が読めていない
    } else {
      d.appendText("ERR", COLOR_ERROR);
      d.appendGap(4);
    }
    return;
  }

  // 読めていない値・マトリクスに出さない値は飛ばして、次の値を流す
  const size_t n = sensors.metricCount();
  for (size_t tries = 0; tries < n; ++tries) {
    size_t i = displayItem;
    displayItem = (displayItem + 1) % n;
    const Metric& m = sensors.metric(i);
    if (!m.onMatrix || isnan(m.value)) continue;

    if (apMode && (lastShownItem == SIZE_MAX || i <= lastShownItem)) {
      d.appendText("AP", COLOR_INFO);
      d.appendGap(3);
    }
    lastShownItem = i;
    char text[48];
    formatValue(text, sizeof(text), m);
    d.appendText(text, m.color);
    d.appendGap(4);
    return;
  }
}

void restartScroll() {
  displayItem = 0;
  lastShownItem = SIZE_MAX;
  display.clear();
}

void setBinaryMode(bool binary) {
  binaryMode = binary;
  prefs.putBool("binary", binaryMode);
  restartScroll();
}

void setBrightness(int value) {
  uint8_t b = constrain(value, 1, (int)MAX_BRIGHTNESS);
  display.setBrightness(b);
  prefs.putUChar("bright", b);
}

void showIp() {
  display.clear();
  if (apMode) {
    display.appendText("AP ", COLOR_INFO);
    display.appendText(WiFi.softAPIP().toString().c_str(), COLOR_INFO);
  } else if (WiFi.isConnected()) {
    display.appendText("IP ", COLOR_INFO);
    display.appendText(WiFi.localIP().toString().c_str(), COLOR_INFO);
  } else {
    display.appendText("NO IP", COLOR_ERROR);
  }
  display.appendGap(4);
}

// ---------------------------------------------------------------- upload

void requestUpload() {
  if (!settings.hasGasTarget() || !sensors.anyValid()) return;
  UploadSnapshot snapshot;
  snapshot.count = sensors.metricCount();
  for (size_t i = 0; i < snapshot.count; ++i) snapshot.values[i] = sensors.metric(i).value;
  snapshot.rssi = WiFi.RSSI();
  snapshot.uptimeSec = uptimeSec();
  uploader.request(snapshot);
}

// NTP 同期後は間隔ちょうど (10分なら xx:00, xx:10, ...) に送る。同期前は起動からの経過で送る。
void scheduleUpload() {
  if (!sensors.anyValid() || !WiFi.isConnected()) return;
  const uint32_t interval = settings.uploadIntervalSec();

  time_t now = time(nullptr);
  if (timeSynced(now)) {
    long slot = now / interval;
    if (lastUploadSlot < 0 && uploadedOnce && millis() - lastUploadMs < interval * 1000UL) {
      lastUploadSlot = slot;  // 同期前に送ったばかりなら重複させない
    }
    if (slot == lastUploadSlot) return;
    lastUploadSlot = slot;
  } else if (uploadedOnce && millis() - lastUploadMs < interval * 1000UL) {
    return;
  }
  uploadedOnce = true;
  lastUploadMs = millis();
  requestUpload();
}

long secondsUntilNextUpload() {
  const long interval = settings.uploadIntervalSec();
  time_t now = time(nullptr);
  if (timeSynced(now)) return (now / interval + 1) * interval - now;
  if (!uploadedOnce) return 0;
  long elapsed = (millis() - lastUploadMs) / 1000;
  return max(0L, interval - elapsed);
}

// ---------------------------------------------------------------- web

void sendJson(const String& json) { server.send(200, "application/json", json); }

void sendError(const char* message) {
  server.send(400, "application/json", "{\"ok\":false,\"error\":" + jsonString(message) + "}");
}

void handleStatus() {
  String json;
  json.reserve(2048);
  json = "{\"device\":" + jsonString(DEVICE_NAME) + ",\"mode\":\"" + (binaryMode ? "binary" : "decimal") +
         "\",\"brightness\":" + display.brightness() + ",\"maxBrightness\":" + MAX_BRIGHTNESS;

  json += ",\"sensors\":[";
  for (size_t i = 0; i < sensors.sensors().size(); ++i) {
    Sensor* s = sensors.sensors()[i];
    json += String(i ? "," : "") + "{\"name\":" + jsonString(s->name()) + ",\"ok\":" + (s->ok() ? "true" : "false") + "}";
  }
  json += "],\"metrics\":[";
  for (size_t i = 0; i < sensors.metricCount(); ++i) {
    const Metric& m = sensors.metric(i);
    json += String(i ? "," : "") + "{\"key\":" + jsonString(m.key) + ",\"label\":" + jsonString(m.label) +
            ",\"unit\":" + jsonString(m.unit) + ",\"decimals\":" + m.decimals + ",\"color\":\"" + colorHex(m.color) +
            "\",\"value\":" + jsonNumber(m.value, m.decimals + 1) + "}";
  }

  const time_t now = time(nullptr);
  json += "],\"apMode\":" + String(apMode ? "true" : "false") + ",\"apSsid\":" + jsonString(apSsid) + ",\"ip\":\"" +
          (apMode ? WiFi.softAPIP() : WiFi.localIP()).toString() + "\",\"ssid\":" + jsonString(WiFi.SSID()) +
          ",\"rssi\":" + WiFi.RSSI() + ",\"mac\":\"" + WiFi.macAddress() + "\",\"uptime\":" + uptimeSec() +
          ",\"epoch\":" + String((long)now) + ",\"timeSynced\":" + (timeSynced(now) ? "true" : "false") +
          ",\"freeHeap\":" + ESP.getFreeHeap();

  json += ",\"upload\":{\"intervalSec\":" + String(settings.uploadIntervalSec()) +
          ",\"nextInSec\":" + secondsUntilNextUpload() + ",\"targets\":[";
  std::vector<TargetStatus> statuses = uploader.statuses();
  for (size_t i = 0; i < settings.gasTargets.size(); ++i) {
    const GasTarget& t = settings.gasTargets[i];
    TargetStatus s = i < statuses.size() ? statuses[i] : TargetStatus{};
    json += String(i ? "," : "") + "{\"name\":" + jsonString(t.name) +
            ",\"configured\":" + (t.url.isEmpty() ? "false" : "true") + ",\"lastAttempt\":" + s.lastAttemptSec +
            ",\"lastSuccess\":" + s.lastSuccessSec + ",\"lastCode\":" + s.lastCode +
            ",\"message\":" + jsonString(s.message) + ",\"successCount\":" + s.successCount +
            ",\"failureCount\":" + s.failureCount + "}";
  }
  json += "]}}";
  sendJson(json);
}

// 履歴は大きいのでチャンクに分けて送る。row(i) が i 行目の文字列を返す。
template <typename RowWriter>
void streamHistory(const char* contentType, const String& header, const char* footer, RowWriter row) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, contentType, "");
  String chunk = header;
  for (size_t i = 0; i < history.size(); ++i) {
    chunk += row(i);
    if (chunk.length() > 1200) {
      server.sendContent(chunk);
      chunk = "";
    }
  }
  chunk += footer;
  server.sendContent(chunk);
  server.sendContent("");
}

void handleHistory() {
  String header = "{\"uptime\":" + String(uptimeSec()) + ",\"epoch\":" + String((long)time(nullptr)) +
                  ",\"intervalSec\":" + String(HISTORY_INTERVAL_SEC) + ",\"keys\":[";
  for (size_t m = 0; m < sensors.metricCount(); ++m) header += String(m ? "," : "") + jsonString(sensors.metric(m).key);
  header += "],\"rows\":[";
  streamHistory("application/json", header, "]}", [](size_t i) {
    String line = String(i ? ",[" : "[") + history.time(i);
    for (size_t m = 0; m < sensors.metricCount(); ++m) {
      line += "," + jsonNumber(history.value(i, m), sensors.metric(m).decimals + 1);
    }
    return line + "]";
  });
}

void handleCsv() {
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + String(DEVICE_NAME) + "_history.csv\"");
  const uint32_t nowUp = uptimeSec();
  const time_t now = time(nullptr);
  const bool synced = timeSynced(now);
  String header = synced ? "timestamp" : "uptime_sec";
  for (size_t m = 0; m < sensors.metricCount(); ++m) header += String(",") + sensors.metric(m).key;
  header += "\n";
  streamHistory("text/csv", header, "", [=](size_t i) {
    char when[24];
    if (synced) {
      time_t at = now - (nowUp - history.time(i));
      struct tm tm;
      localtime_r(&at, &tm);
      strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &tm);
    } else {
      snprintf(when, sizeof(when), "%lu", (unsigned long)history.time(i));
    }
    String line = when;
    for (size_t m = 0; m < sensors.metricCount(); ++m) {
      float v = history.value(i, m);
      line += "," + (isnan(v) ? String("") : String(v, (unsigned int)sensors.metric(m).decimals + 1));
    }
    return line + "\n";
  });
}

void handleMatrix() {
  char hex[MatrixDisplay::kSize * MatrixDisplay::kSize * 6 + 1];
  for (uint8_t i = 0; i < MatrixDisplay::kSize * MatrixDisplay::kSize; ++i) {
    snprintf(hex + i * 6, 7, "%06lx", (unsigned long)display.pixel(i));
  }
  server.send(200, "text/plain", hex);
}

void handleMode() {
  String value = server.arg("value");
  if (value == "binary") {
    setBinaryMode(true);
  } else if (value == "decimal") {
    setBinaryMode(false);
  } else {
    setBinaryMode(!binaryMode);
  }
  sendJson("{\"ok\":true}");
}

void handleBrightness() {
  if (!server.hasArg("value")) return sendError("value is required");
  setBrightness(server.arg("value").toInt());
  sendJson("{\"ok\":true}");
}

// 管理パスワードが設定されていれば Basic 認証 (ユーザー名 admin) を求める
bool authorized() {
  if (settings.adminPassword.isEmpty() || server.authenticate("admin", settings.adminPassword.c_str())) return true;
  server.requestAuthentication(BASIC_AUTH, DEVICE_NAME);
  return false;
}

void handleSettingsGet() {
  if (!authorized()) return;
  String json = "{\"wifiSsid\":" + jsonString(settings.wifiSsid) +
                ",\"hasWifiPassword\":" + (settings.wifiPassword.isEmpty() ? "false" : "true") + ",\"gasTargets\":[";
  for (size_t i = 0; i < settings.gasTargets.size(); ++i) {
    const GasTarget& t = settings.gasTargets[i];
    json += String(i ? "," : "") + "{\"name\":" + jsonString(t.name) + ",\"url\":" + jsonString(t.url) +
            ",\"token\":" + jsonString(t.token) + "}";
  }
  json += "],\"maxGasTargets\":" + String(Settings::kMaxGasTargets) +
          ",\"hasAdminPassword\":" + (settings.adminPassword.isEmpty() ? "false" : "true") +
          ",\"uploadIntervalMin\":" + settings.uploadIntervalMin + ",\"apMode\":" + (apMode ? "true" : "false") +
          ",\"apSsid\":" + jsonString(apSsid) + ",\"device\":" + jsonString(DEVICE_NAME) + "}";
  sendJson(json);
}

bool sameTargets(const std::vector<GasTarget>& a, const std::vector<GasTarget>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].name != b[i].name || a[i].url != b[i].url || a[i].token != b[i].token) return false;
  }
  return true;
}

void handleSettingsPost() {
  if (!authorized()) return;

  Settings next = settings;
  next.wifiSsid = server.arg("wifiSsid");
  next.wifiSsid.trim();
  // SSID を変えたときはパスワードも入れ直し (空ならオープンなネットワーク)。同じ SSID で空欄なら据え置き。
  String wifiPassword = server.arg("wifiPassword");
  if (next.wifiSsid != settings.wifiSsid || wifiPassword.length() > 0) next.wifiPassword = wifiPassword;
  String adminPassword = server.arg("adminPassword");
  if (adminPassword.length() > 0) next.adminPassword = adminPassword;
  if (server.hasArg("clearAdminPassword")) next.adminPassword = "";
  long interval = server.arg("uploadIntervalMin").toInt();

  // 送信先は gasName<i> / gasUrl<i> / gasToken<i> (i = 0 .. gasCount-1)
  long count = server.arg("gasCount").toInt();
  if (count < 1 || count > (long)Settings::kMaxGasTargets) return sendError("送信先は 1〜10 件にしてください");
  next.gasTargets.clear();
  for (long i = 0; i < count; ++i) {
    GasTarget t{server.arg("gasName" + String(i)), server.arg("gasUrl" + String(i)), server.arg("gasToken" + String(i))};
    t.name.trim();
    t.url.trim();
    t.token.trim();
    String label = "送信先 " + String(i + 1) + ": ";
    if (t.name.length() > 32) return sendError((label + "名前は 32 文字以内にしてください").c_str());
    if (t.url.length() > 0 && !t.url.startsWith("https://"))
      return sendError((label + "URL は https:// で始まる必要があります").c_str());
    if (t.url.length() > 256) return sendError((label + "URL が長すぎます").c_str());
    if (t.token.length() > 64) return sendError((label + "合言葉は 64 文字以内にしてください").c_str());
    next.gasTargets.push_back(t);
  }

  if (next.wifiSsid.length() > 32) return sendError("SSID は 32 文字以内にしてください");
  if (next.wifiPassword.length() > 63 || (next.wifiPassword.length() > 0 && next.wifiPassword.length() < 8))
    return sendError("Wi-Fi パスワードは 8〜63 文字にしてください");
  if (next.adminPassword.length() > 64) return sendError("管理パスワードは 64 文字以内にしてください");
  if (interval < 1 || interval > 60) return sendError("送信間隔は 1〜60 分にしてください");
  next.uploadIntervalMin = interval;

  const bool wifiChanged = next.wifiSsid != settings.wifiSsid || next.wifiPassword != settings.wifiPassword;
  const bool targetsChanged = !sameTargets(next.gasTargets, settings.gasTargets);
  const bool intervalChanged = next.uploadIntervalMin != settings.uploadIntervalMin;
  saveSettings(next);
  settings = next;
  if (targetsChanged) uploader.setTargets(settings.gasTargets);
  ArduinoOTA.setPassword(settings.adminPassword.c_str());
  if (intervalChanged) lastUploadSlot = -1;  // 新しい間隔の区切りで送り直す

  // Wi-Fi を変えたとき、またはセットアップモード中は再起動してつなぎ直す
  const bool restart = wifiChanged || apMode;
  if (restart) restartAtMs = millis() + 1500;
  sendJson(restart ? "{\"ok\":true,\"restart\":true}" : "{\"ok\":true,\"restart\":false}");
}

void handleScan() {
  if (!authorized()) return;
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    // 同じ SSID が複数見えたら一番強いものだけ返す
    bool duplicate = false;
    for (int j = 0; j < n && !duplicate; ++j) {
      duplicate = j != i && WiFi.SSID(j) == ssid && (WiFi.RSSI(j) > WiFi.RSSI(i) || (WiFi.RSSI(j) == WiFi.RSSI(i) && j < i));
    }
    if (duplicate) continue;
    if (json.length() > 1) json += ',';
    json += "{\"ssid\":" + jsonString(ssid) + ",\"rssi\":" + WiFi.RSSI(i) +
            ",\"secure\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
  }
  WiFi.scanDelete();
  json += "]";
  sendJson(json);
}

void handleReset() {
  if (!authorized()) return;
  resetSettings();
  restartAtMs = millis() + 1500;
  sendJson("{\"ok\":true,\"restart\":true}");
}

void setupWebServer() {
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); });
  server.on("/style.css", HTTP_GET, [] { server.send_P(200, "text/css", STYLE_CSS); });
  server.on("/settings", HTTP_GET, [] {
    if (authorized()) server.send_P(200, "text/html; charset=utf-8", SETTINGS_HTML);
  });
  server.on("/api/settings", HTTP_GET, handleSettingsGet);
  server.on("/api/settings", HTTP_POST, handleSettingsPost);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/reset", HTTP_POST, handleReset);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/matrix", HTTP_GET, handleMatrix);
  server.on("/history.csv", HTTP_GET, handleCsv);
  server.on("/api/mode", HTTP_POST, handleMode);
  server.on("/api/brightness", HTTP_POST, handleBrightness);
  server.on("/api/upload", HTTP_POST, [] {
    requestUpload();
    sendJson("{\"ok\":true}");
  });
  server.on("/api/showip", HTTP_POST, [] {
    showIp();
    sendJson("{\"ok\":true}");
  });
  server.onNotFound([] {
    // セットアップモードではどの URL も設定画面へ (スマホのキャプティブポータル表示用)
    if (apMode) {
      server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/settings");
      server.send(302, "text/plain", "");
      return;
    }
    server.send(404, "text/plain", "not found");
  });
  server.begin();
}

// ---------------------------------------------------------------- network

// Wi-Fi に初めてつながったときに mDNS と OTA を立ち上げる
void startNetworkServices() {
  networkStarted = true;
  Serial.printf("[wifi] connected: %s\n", WiFi.localIP().toString().c_str());
  if (MDNS.begin(DEVICE_NAME)) MDNS.addService("http", "tcp", 80);
  ArduinoOTA.setHostname(DEVICE_NAME);
  ArduinoOTA.setPassword(settings.adminPassword.c_str());
  ArduinoOTA.setMdnsEnabled(false);  // mDNS は上で開始済み
  ArduinoOTA.begin();
  showIp();
}

// Wi-Fi 未設定・接続失敗時のセットアップ用アクセスポイント。
// retrySta が true なら、AP に誰もつないでいない間は保存済みの Wi-Fi への接続も試し続ける。
void startSetupAp(bool retrySta) {
  apMode = true;
  staRetryInAp = retrySta;
  lastStaRetryMs = millis();
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(apSsid, sizeof(apSsid), "%s-setup-%02X%02X", DEVICE_NAME, mac[4], mac[5]);
  WiFi.setAutoReconnect(false);  // 自動再接続のチャンネル切り替えで AP が不安定になるので手動で試す
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid, SETUP_AP_PASSWORD);
  dnsServer.start(53, "*", WiFi.softAPIP());
  Serial.printf("[wifi] setup AP: %s (%s)\n", apSsid, WiFi.softAPIP().toString().c_str());
  showIp();
}

void stopSetupAp() {
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  apMode = false;
  apSsid[0] = '\0';
}

void updateSetupAp(uint32_t nowMs) {
  dnsServer.processNextRequest();
  if (WiFi.isConnected()) {
    stopSetupAp();
    return;
  }
  if (staRetryInAp && WiFi.softAPgetStationNum() == 0 && nowMs - lastStaRetryMs >= 30000) {
    lastStaRetryMs = nowMs;
    WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPassword.c_str());
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  prefs.begin("env3", false);
  binaryMode = prefs.getBool("binary", false);

  display.begin(prefs.getUChar("bright", DEFAULT_BRIGHTNESS), SCROLL_INTERVAL_MS);
  display.setRefill(refillDisplay);
  button.begin();
  const bool forceSetup = digitalRead(PIN_BUTTON) == LOW;  // ボタンを押しながら起動でセットアップモード
  loadSettings(settings);

  sensors.add(&env3);
  history.begin(sensors.metricCount(), HISTORY_SIZE);
  sensors.poll(millis());
  lastSensorReadMs = millis();

  uploader.begin(sensors, DEVICE_NAME);
  uploader.setTargets(settings.gasTargets);

  WiFi.persistent(false);  // 接続情報は settings 側で保存する
  WiFi.setHostname(DEVICE_NAME);
  configTzTime(TZ_INFO, NTP_SERVER1, NTP_SERVER2);

  const bool hasWifi = !settings.wifiSsid.isEmpty();
  if (hasWifi && !forceSetup) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPassword.c_str());
    // 最大 15 秒は接続を待つ。つながらなければセットアップ用 AP を出しつつ再接続を試し続ける。
    uint32_t start = millis();
    while (!WiFi.isConnected() && millis() - start < 15000) {
      display.drawSpinner(millis(), COLOR_INFO);
      delay(20);
    }
  }
  if (!WiFi.isConnected()) startSetupAp(hasWifi && !forceSetup);
  setupWebServer();
}

void loop() {
  const uint32_t nowMs = millis();

  if (restartAtMs != 0 && (int32_t)(nowMs - restartAtMs) >= 0) ESP.restart();
  if (apMode) updateSetupAp(nowMs);
  if (!networkStarted && WiFi.isConnected()) startNetworkServices();
  if (networkStarted) ArduinoOTA.handle();
  server.handleClient();

  switch (button.update(nowMs)) {
    case Button::kClick:
      setBinaryMode(!binaryMode);
      break;
    case Button::kLongPress:
      showIp();
      break;
    case Button::kNone:
      break;
  }

  if (nowMs - lastSensorReadMs >= SENSOR_INTERVAL_MS) {
    lastSensorReadMs = nowMs;
    sensors.poll(nowMs);
    recordHistory();
  }
  scheduleUpload();

  display.update(nowMs);
  delay(2);
}
