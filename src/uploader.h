#pragma once

#include <Arduino.h>

#include <vector>

#include "sensors/sensor.h"
#include "settings.h"

// 送信する 1 回分の値。FreeRTOS のキューに入れるので固定長にしている。
struct UploadSnapshot {
  uint8_t count = 0;
  float values[SensorHub::kMaxMetrics];  // SensorHub の metric(i) と同じ並び
  int32_t rssi = 0;
  uint32_t uptimeSec = 0;
};

// 送信先ごとの状態。時刻は起動からの秒数 (0 = まだ)
struct TargetStatus {
  uint32_t lastAttemptSec = 0;
  uint32_t lastSuccessSec = 0;
  int lastCode = 0;
  char message[96] = "waiting";
  uint32_t successCount = 0;
  uint32_t failureCount = 0;
};

// 登録された全ての GAS へ、別タスクで値を送る。
// 本文は {"token":..,"device":..,"values":{"<metric key>":値, ..., "rssi_dbm":..,"uptime_sec":..}}
class Uploader {
 public:
  void begin(SensorHub& sensors, const char* device);
  // 送信先を差し替える。送信先ごとの状態はリセットされる
  void setTargets(const std::vector<GasTarget>& targets);
  // 送信を予約する。送信中に次が来たら、最新の 1 件だけを残す
  void request(const UploadSnapshot& snapshot);
  std::vector<TargetStatus> statuses();

 private:
  enum class Result {
    kOk,
    kRejected,  // GAS には届いたが拒否された (合言葉違いなど)。再送しても変わらない
    kFailed,    // 通信エラーなど。再送する
  };

  static void taskEntry(void* self);
  void run();
  String buildBody(const UploadSnapshot& snapshot, const String& token) const;
  Result post(const String& url, const String& body, int& code, String& message);
  void setStatus(size_t index, uint32_t generation, Result result, int code, const String& message);

  SensorHub* sensors_ = nullptr;
  const char* device_ = "";
  QueueHandle_t queue_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;  // targets_ / statuses_ / generation_ を守る
  std::vector<GasTarget> targets_;
  std::vector<TargetStatus> statuses_;
  uint32_t generation_ = 0;  // setTargets のたびに増やし、送信中に差し替わったら結果を捨てる
};
