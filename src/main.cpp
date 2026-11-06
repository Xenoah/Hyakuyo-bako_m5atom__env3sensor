#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <M5UnitENV.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "config.h"
#include "matrix_display.h"
#include "web_page.h"

namespace {

struct Reading {
  float temperature = NAN;  // °C
  float humidity = NAN;     // %RH
  float pressure = NAN;     // hPa
  bool valid = false;
};

struct HistoryEntry {
  uint32_t uptimeSec;
  float temperature;
  float humidity;
  float pressure;
};

// 時刻は NTP 同期前でも扱えるよう起動からの秒数で持ち、表示時に実時刻へ換算する。
struct UploadStatus {
  uint32_t lastAttemptSec = 0;  // 0 = まだ
  uint32_t lastSuccessSec = 0;
  int lastCode = 0;
  char message[96] = "waiting";
  uint32_t successCount = 0;
  uint32_t failureCount = 0;
};

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
SHT3X sht3x;
QMP6988 qmp;

bool sensorOk = false;
uint32_t lastSensorInitMs = 0;
uint32_t lastSensorReadMs = 0;
uint8_t sensorFailures = 0;
Reading reading;

HistoryEntry history[HISTORY_SIZE];
size_t historyHead = 0;  // 次に書き込む位置
size_t historyCount = 0;
uint32_t lastHistorySec = 0;

bool binaryMode = false;
uint8_t displayItem = 0;  // 0:気温 1:湿度 2:気圧

bool networkStarted = false;
QueueHandle_t uploadQueue;
SemaphoreHandle_t uploadMutex;
UploadStatus uploadStatus;
bool uploadedOnce = false;
uint32_t lastUploadMs = 0;
long lastUploadSlot = -1;

uint32_t uptimeSec() { return millis() / 1000; }

bool timeSynced(time_t now) { return now > 1700000000; }

// ---------------------------------------------------------------- sensor

bool initSensors() {
  bool shtOk = sht3x.begin(&Wire, SHT3X_I2C_ADDR, PIN_SDA, PIN_SCL, 400000U);
  bool qmpOk = qmp.begin(&Wire, QMP6988_SLAVE_ADDRESS_L, PIN_SDA, PIN_SCL, 400000U);
  Serial.printf("[sensor] SHT30:%s QMP6988:%s\n", shtOk ? "ok" : "NG", qmpOk ? "ok" : "NG");
  return shtOk && qmpOk;
}

void readSensors() {
  if (!sensorOk) {
    if (millis() - lastSensorInitMs < 5000) return;
    lastSensorInitMs = millis();
    sensorOk = initSensors();
    if (!sensorOk) return;
    sensorFailures = 0;
  }

  bool shtOk = sht3x.update();
  qmp.update();
  float hPa = qmp.pressure / 100.0f;
  if (!shtOk || hPa < 300 || hPa > 1100) {
    Serial.println("[sensor] read failed");
    if (++sensorFailures >= 3) {
      sensorOk = false;
      reading.valid = false;
    }
    return;
  }
  sensorFailures = 0;
  reading.temperature = sht3x.cTemp;
  reading.humidity = sht3x.humidity;
  reading.pressure = hPa;
  reading.valid = true;
}

void recordHistory() {
  if (!reading.valid) return;
  uint32_t now = uptimeSec();
  if (historyCount > 0 && now - lastHistorySec < HISTORY_INTERVAL_SEC) return;
  lastHistorySec = now;
  history[historyHead] = {now, reading.temperature, reading.humidity, reading.pressure};
  historyHead = (historyHead + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) ++historyCount;
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

void formatValue(char* out, size_t size, float value, const char* unit) {
  if (binaryMode) {
    formatBinary(out, size, lroundf(value), unit);
  } else {
    snprintf(out, size, "%.1f%s", value, unit);
  }
}

void refillDisplay(MatrixDisplay& d) {
  if (!sensorOk) {
    d.appendText("ERR", COLOR_ERROR);
    d.appendGap(4);
    return;
  }
  if (!reading.valid) {
    d.appendText("...", COLOR_INFO);
    return;
  }

  char text[48];
  uint32_t color;
  switch (displayItem) {
    case 0:
      formatValue(text, sizeof(text), reading.temperature, "*C");
      color = COLOR_TEMPERATURE;
      break;
    case 1:
      formatValue(text, sizeof(text), reading.humidity, "%");
      color = COLOR_HUMIDITY;
      break;
    default:
      formatValue(text, sizeof(text), reading.pressure, "hPa");
      color = COLOR_PRESSURE;
      break;
  }
  displayItem = (displayItem + 1) % 3;
  d.appendText(text, color);
  d.appendGap(4);
}

void restartScroll() {
  displayItem = 0;
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
  if (WiFi.isConnected()) {
    display.appendText("IP ", COLOR_INFO);
    display.appendText(WiFi.localIP().toString().c_str(), COLOR_INFO);
  } else {
    display.appendText("NO IP", COLOR_ERROR);
  }
  display.appendGap(4);
}

// ---------------------------------------------------------------- upload

void setUploadStatus(bool ok, int code, const char* message) {
  xSemaphoreTake(uploadMutex, portMAX_DELAY);
  uploadStatus.lastAttemptSec = uptimeSec();
  uploadStatus.lastCode = code;
  if (ok) {
    uploadStatus.lastSuccessSec = uploadStatus.lastAttemptSec;
    ++uploadStatus.successCount;
  } else {
    ++uploadStatus.failureCount;
  }
  // JSON にそのまま埋め込むので、引用符や制御文字は落としておく
  size_t n = 0;
  for (const char* p = message; *p && n + 1 < sizeof(uploadStatus.message); ++p) {
    if (*p == '"' || *p == '\\' || (uint8_t)*p < 0x20) continue;
    uploadStatus.message[n++] = *p;
  }
  uploadStatus.message[n] = '\0';
  xSemaphoreGive(uploadMutex);
}

bool postToGas(const Reading& r) {
  if (!WiFi.isConnected()) {
    setUploadStatus(false, 0, "wifi disconnected");
    return false;
  }

  char body[256];
  snprintf(body, sizeof(body),
           "{\"token\":\"%s\",\"device\":\"%s\",\"temperature\":%.2f,\"humidity\":%.2f,"
           "\"pressure\":%.2f,\"rssi\":%d,\"uptime\":%lu}",
           GAS_TOKEN, DEVICE_NAME, r.temperature, r.humidity, r.pressure, WiFi.RSSI(),
           (unsigned long)uptimeSec());

  WiFiClientSecure client;
  client.setInsecure();  // 証明書の検証は省略 (送るのは測定値のみ)
  HTTPClient http;
  // GAS は 302 でリダイレクトして結果を返すので GET で追いかける
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(20000);
  if (!http.begin(client, GAS_URL)) {
    setUploadStatus(false, 0, "invalid GAS_URL");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  int code = http.POST((uint8_t*)body, strlen(body));
  String response = code > 0 ? http.getString() : HTTPClient::errorToString(code);
  http.end();

  bool ok = code == 200 && response.indexOf("\"ok\":true") >= 0;
  Serial.printf("[upload] code=%d %s\n", code, response.substring(0, 120).c_str());
  setUploadStatus(ok, code, ok ? "ok" : response.substring(0, 90).c_str());
  return ok;
}

void uploadTask(void*) {
  Reading r;
  for (;;) {
    if (xQueueReceive(uploadQueue, &r, portMAX_DELAY) != pdTRUE) continue;
    for (int attempt = 0; attempt < 3; ++attempt) {
      if (postToGas(r)) break;
      // 30秒待って再送。新しい送信要求が来たらそちらを優先する。
      Reading newer;
      if (xQueuePeek(uploadQueue, &newer, pdMS_TO_TICKS(30000)) == pdTRUE) break;
    }
  }
}

void requestUpload() {
  if (strlen(GAS_URL) == 0) {
    setUploadStatus(false, 0, "GAS_URL is not set");
    return;
  }
  if (!reading.valid) {
    setUploadStatus(false, 0, "no sensor reading");
    return;
  }
  xQueueOverwrite(uploadQueue, &reading);
}

// NTP 同期後は 10 分ちょうど (xx:00, xx:10, ...) に送る。同期前は起動からの経過で送る。
void scheduleUpload() {
  if (!reading.valid || !WiFi.isConnected()) return;

  time_t now = time(nullptr);
  if (timeSynced(now)) {
    long slot = now / UPLOAD_INTERVAL_SEC;
    if (lastUploadSlot < 0 && uploadedOnce && millis() - lastUploadMs < UPLOAD_INTERVAL_SEC * 1000UL) {
      lastUploadSlot = slot;  // 同期前に送ったばかりなら重複させない
    }
    if (slot == lastUploadSlot) return;
    lastUploadSlot = slot;
  } else if (uploadedOnce && millis() - lastUploadMs < UPLOAD_INTERVAL_SEC * 1000UL) {
    return;
  }
  uploadedOnce = true;
  lastUploadMs = millis();
  requestUpload();
}

long secondsUntilNextUpload() {
  time_t now = time(nullptr);
  if (timeSynced(now)) return (now / UPLOAD_INTERVAL_SEC + 1) * UPLOAD_INTERVAL_SEC - now;
  if (!uploadedOnce) return 0;
  long elapsed = (millis() - lastUploadMs) / 1000;
  return max(0L, (long)UPLOAD_INTERVAL_SEC - elapsed);
}

// ---------------------------------------------------------------- web

// NAN は JSON の null にする
const char* num(char* buf, size_t size, float value, int decimals) {
  if (isnan(value)) return "null";
  snprintf(buf, size, "%.*f", decimals, value);
  return buf;
}

void sendJson(const char* json) { server.send(200, "application/json", json); }

void handleStatus() {
  UploadStatus up;
  xSemaphoreTake(uploadMutex, portMAX_DELAY);
  up = uploadStatus;
  xSemaphoreGive(uploadMutex);

  char t[16], h[16], p[16];
  const bool v = reading.valid;
  char json[1024];
  snprintf(json, sizeof(json),
           "{\"device\":\"%s\",\"sensorOk\":%s,\"valid\":%s,"
           "\"temperature\":%s,\"humidity\":%s,\"pressure\":%s,"
           "\"mode\":\"%s\",\"brightness\":%u,\"maxBrightness\":%u,"
           "\"ip\":\"%s\",\"ssid\":\"%s\",\"rssi\":%d,\"mac\":\"%s\","
           "\"uptime\":%lu,\"epoch\":%ld,\"timeSynced\":%s,\"freeHeap\":%u,"
           "\"upload\":{\"configured\":%s,\"intervalSec\":%lu,\"nextInSec\":%ld,"
           "\"lastAttempt\":%lu,\"lastSuccess\":%lu,\"lastCode\":%d,\"message\":\"%s\","
           "\"successCount\":%lu,\"failureCount\":%lu}}",
           DEVICE_NAME, sensorOk ? "true" : "false", v ? "true" : "false",
           num(t, sizeof(t), v ? reading.temperature : NAN, 2), num(h, sizeof(h), v ? reading.humidity : NAN, 2),
           num(p, sizeof(p), v ? reading.pressure : NAN, 2), binaryMode ? "binary" : "decimal",
           display.brightness(), MAX_BRIGHTNESS, WiFi.localIP().toString().c_str(), WiFi.SSID().c_str(),
           WiFi.RSSI(), WiFi.macAddress().c_str(), (unsigned long)uptimeSec(), (long)time(nullptr),
           timeSynced(time(nullptr)) ? "true" : "false", ESP.getFreeHeap(), strlen(GAS_URL) ? "true" : "false",
           (unsigned long)UPLOAD_INTERVAL_SEC, secondsUntilNextUpload(), (unsigned long)up.lastAttemptSec,
           (unsigned long)up.lastSuccessSec, up.lastCode, up.message, (unsigned long)up.successCount,
           (unsigned long)up.failureCount);
  sendJson(json);
}

// 履歴は大きいのでチャンクに分けて送る。each(entry, buf, size) が 1 行分を書く。
template <typename RowWriter>
void streamHistory(const char* contentType, const char* header, const char* footer, RowWriter each) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, contentType, "");
  String chunk = header;
  size_t start = (historyHead + HISTORY_SIZE - historyCount) % HISTORY_SIZE;
  char line[96];
  for (size_t i = 0; i < historyCount; ++i) {
    each(history[(start + i) % HISTORY_SIZE], i == 0, line, sizeof(line));
    chunk += line;
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
  char header[128];
  snprintf(header, sizeof(header), "{\"uptime\":%lu,\"epoch\":%ld,\"intervalSec\":%lu,\"rows\":[",
           (unsigned long)uptimeSec(), (long)time(nullptr), (unsigned long)HISTORY_INTERVAL_SEC);
  streamHistory("application/json", header, "]}", [](const HistoryEntry& e, bool first, char* buf, size_t size) {
    snprintf(buf, size, "%s[%lu,%.2f,%.2f,%.2f]", first ? "" : ",", (unsigned long)e.uptimeSec, e.temperature,
             e.humidity, e.pressure);
  });
}

void handleCsv() {
  server.sendHeader("Content-Disposition", "attachment; filename=\"env3_history.csv\"");
  const uint32_t nowUp = uptimeSec();
  const time_t now = time(nullptr);
  const bool synced = timeSynced(now);
  streamHistory("text/csv", synced ? "time,temperature,humidity,pressure\n" : "uptime_sec,temperature,humidity,pressure\n",
                "", [=](const HistoryEntry& e, bool, char* buf, size_t size) {
                  char when[24];
                  if (synced) {
                    time_t at = now - (nowUp - e.uptimeSec);
                    struct tm tm;
                    localtime_r(&at, &tm);
                    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &tm);
                  } else {
                    snprintf(when, sizeof(when), "%lu", (unsigned long)e.uptimeSec);
                  }
                  snprintf(buf, size, "%s,%.2f,%.2f,%.2f\n", when, e.temperature, e.humidity, e.pressure);
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
  if (!server.hasArg("value")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"value is required\"}");
    return;
  }
  setBrightness(server.arg("value").toInt());
  sendJson("{\"ok\":true}");
}

void setupWebServer() {
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); });
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
  server.onNotFound([] { server.send(404, "text/plain", "not found"); });
  server.begin();
}

// Wi-Fi に初めてつながったときに mDNS と OTA を立ち上げる
void startNetworkServices() {
  networkStarted = true;
  Serial.printf("[wifi] connected: %s\n", WiFi.localIP().toString().c_str());
  if (MDNS.begin(DEVICE_NAME)) MDNS.addService("http", "tcp", 80);
  ArduinoOTA.setHostname(DEVICE_NAME);
  if (strlen(OTA_PASSWORD) > 0) ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.setMdnsEnabled(false);  // mDNS は上で開始済み
  ArduinoOTA.begin();
  showIp();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  prefs.begin("env3", false);
  binaryMode = prefs.getBool("binary", false);

  display.begin(prefs.getUChar("bright", DEFAULT_BRIGHTNESS), SCROLL_INTERVAL_MS);
  display.setRefill(refillDisplay);
  button.begin();

  uploadMutex = xSemaphoreCreateMutex();
  uploadQueue = xQueueCreate(1, sizeof(Reading));
  xTaskCreatePinnedToCore(uploadTask, "upload", 12288, nullptr, 1, nullptr, 0);

  lastSensorInitMs = millis();
  sensorOk = initSensors();
  lastSensorReadMs = millis();  // 最初の変換が終わるのを待ってから読む

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(DEVICE_NAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  configTzTime(TZ_INFO, NTP_SERVER1, NTP_SERVER2);

  // 最大 15 秒は接続を待つ。つながらなくても計測と表示は続け、裏で再接続させる。
  uint32_t start = millis();
  while (!WiFi.isConnected() && millis() - start < 15000) {
    display.drawSpinner(millis(), COLOR_INFO);
    delay(20);
  }
  setupWebServer();
}

void loop() {
  const uint32_t nowMs = millis();

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
    readSensors();
    recordHistory();
  }
  scheduleUpload();

  display.update(nowMs);
  delay(2);
}
