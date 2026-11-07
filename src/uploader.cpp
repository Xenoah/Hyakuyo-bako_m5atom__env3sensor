#include "uploader.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "json_util.h"

namespace {

uint32_t uptimeSec() { return millis() / 1000; }

// GAS の doPost の結果ページ (302 の Location) を取りに行く
int fetchResult(const String& location, String& response) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(20000);
  if (!http.begin(client, location)) return HTTPC_ERROR_CONNECTION_REFUSED;
  int code = http.GET();
  response = code > 0 ? http.getString() : HTTPClient::errorToString(code);
  http.end();
  return code;
}

}  // namespace

void Uploader::begin(SensorHub& sensors, const char* device) {
  sensors_ = &sensors;
  device_ = device;
  mutex_ = xSemaphoreCreateMutex();
  queue_ = xQueueCreate(1, sizeof(UploadSnapshot));
  xTaskCreatePinnedToCore(taskEntry, "upload", 12288, this, 1, nullptr, 0);
}

void Uploader::setTargets(const std::vector<GasTarget>& targets) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  targets_ = targets;
  statuses_.assign(targets.size(), TargetStatus{});
  for (size_t i = 0; i < targets.size(); ++i) {
    if (targets[i].url.isEmpty()) strlcpy(statuses_[i].message, "URL is not set", sizeof(statuses_[i].message));
  }
  ++generation_;
  xSemaphoreGive(mutex_);
}

void Uploader::request(const UploadSnapshot& snapshot) { xQueueOverwrite(queue_, &snapshot); }

std::vector<TargetStatus> Uploader::statuses() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  std::vector<TargetStatus> copy = statuses_;
  xSemaphoreGive(mutex_);
  return copy;
}

void Uploader::taskEntry(void* self) { static_cast<Uploader*>(self)->run(); }

void Uploader::run() {
  UploadSnapshot snapshot;
  for (;;) {
    if (xQueueReceive(queue_, &snapshot, portMAX_DELAY) != pdTRUE) continue;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    std::vector<GasTarget> targets = targets_;
    uint32_t generation = generation_;
    xSemaphoreGive(mutex_);

    std::vector<bool> pending(targets.size());
    for (size_t i = 0; i < targets.size(); ++i) pending[i] = !targets[i].url.isEmpty();

    for (int attempt = 0; attempt < 3; ++attempt) {
      bool retry = false;
      for (size_t i = 0; i < targets.size(); ++i) {
        if (!pending[i]) continue;
        int code = 0;
        String message;
        Result result = post(targets[i].url, buildBody(snapshot, targets[i].token), code, message);
        Serial.printf("[upload] #%u %s code=%d %s\n", (unsigned)i + 1, targets[i].name.c_str(), code, message.c_str());
        setStatus(i, generation, result, code, message);
        pending[i] = result == Result::kFailed;
        retry |= pending[i];
      }
      if (!retry) break;
      // 30秒待って失敗した送信先だけ再送。新しい送信要求が来たらそちらを優先する。
      UploadSnapshot newer;
      if (xQueuePeek(queue_, &newer, pdMS_TO_TICKS(30000)) == pdTRUE) break;
    }
  }
}

String Uploader::buildBody(const UploadSnapshot& snapshot, const String& token) const {
  String body = "{\"token\":" + jsonString(token) + ",\"device\":" + jsonString(device_) + ",\"values\":{";
  for (size_t i = 0; i < snapshot.count && i < sensors_->metricCount(); ++i) {
    const Metric& m = sensors_->metric(i);
    body += jsonString(m.key) + ":" + jsonNumber(snapshot.values[i], m.decimals + 1) + ",";
  }
  body += "\"rssi_dbm\":" + String(snapshot.rssi) + ",\"uptime_sec\":" + String(snapshot.uptimeSec) + "}";

  // v1.1 以前の Code.gs は values を読まずトップレベルの値を見るので、更新前でも記録できるよう同じ値を載せる
  static const char* const kLegacy[][2] = {
      {"temperature_c", "temperature"}, {"humidity_pct", "humidity"}, {"pressure_hpa", "pressure"}};
  for (size_t i = 0; i < snapshot.count && i < sensors_->metricCount(); ++i) {
    for (const auto& legacy : kLegacy) {
      if (strcmp(sensors_->metric(i).key, legacy[0]) == 0) {
        body += String(",\"") + legacy[1] + "\":" + jsonNumber(snapshot.values[i], 2);
      }
    }
  }
  body += ",\"rssi\":" + String(snapshot.rssi) + ",\"uptime\":" + String(snapshot.uptimeSec) + "}";
  return body;
}

// GAS は doPost を実行してから 302 で結果の URL を返す。HTTPClient に自動で追わせると
// POST 用の Content-Type を付けたまま GET してしまい 400 になるので、結果は別リクエストで取る。
Uploader::Result Uploader::post(const String& url, const String& body, int& code, String& message) {
  if (!WiFi.isConnected()) {
    message = "wifi disconnected";
    return Result::kFailed;
  }

  String response;
  String location;
  {
    WiFiClientSecure client;
    client.setInsecure();  // 証明書の検証は省略 (送るのは測定値と合言葉のみ)
    HTTPClient http;
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setTimeout(20000);
    const char* headerKeys[] = {"Location"};
    http.collectHeaders(headerKeys, 1);
    if (!http.begin(client, url)) {
      message = "invalid URL";
      return Result::kRejected;
    }
    http.addHeader("Content-Type", "application/json");
    code = http.POST(body);
    if (code == HTTP_CODE_FOUND || code == HTTP_CODE_SEE_OTHER) {
      location = http.header("Location");
    } else {
      response = code > 0 ? http.getString() : HTTPClient::errorToString(code);
    }
    http.end();
  }

  if (location.length() > 0) {
    int resultCode = fetchResult(location, response);
    if (resultCode != HTTP_CODE_OK) {
      // 302 が返った時点で doPost は実行済み。再送すると行が重複するので成功扱いにする
      message = "ok (result not read)";
      return Result::kOk;
    }
    code = resultCode;
  }

  if (code == HTTP_CODE_OK && response.indexOf("\"ok\":true") >= 0) {
    message = "ok";
    return Result::kOk;
  }
  // HTML のエラーページはそのまま見せても読めないので要約する
  message = response.startsWith("<") ? "HTTP " + String(code) + " (html)" : response.substring(0, 90);
  return code == HTTP_CODE_OK ? Result::kRejected : Result::kFailed;
}

void Uploader::setStatus(size_t index, uint32_t generation, Result result, int code, const String& message) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (generation == generation_ && index < statuses_.size()) {
    TargetStatus& s = statuses_[index];
    s.lastAttemptSec = uptimeSec();
    s.lastCode = code;
    if (result == Result::kOk) {
      s.lastSuccessSec = s.lastAttemptSec;
      ++s.successCount;
    } else {
      ++s.failureCount;
    }
    strlcpy(s.message, message.c_str(), sizeof(s.message));
  }
  xSemaphoreGive(mutex_);
}
