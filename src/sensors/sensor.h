#pragma once

#include <Arduino.h>

#include <vector>

// センサーが出す 1 つの値 (気温、湿度など)。
// key はスプレッドシートの列名・API のキーになるので、単位まで含めた英小文字で付ける (例: temperature_c)。
struct Metric {
  const char* key;
  const char* label;       // Web に出す名前 (例: 気温)
  const char* unit;        // Web に出す単位 (例: °C)
  const char* matrixUnit;  // マトリクスに出す単位。3x5 フォントにある文字だけ使える ('*' は °)
  uint8_t decimals;        // 表示する小数点以下の桁数
  uint32_t color;          // 表示色 0xRRGGBB
  bool onMatrix = true;    // マトリクスのスクロールに出すか
  float value = NAN;       // 最新値。読めていなければ NAN
};

// センサーの基底クラス。新しいセンサーはこれを継承して begin() と read() を書き、
// main.cpp で sensors.add() するだけで、マトリクス・Web・履歴・CSV・GAS 送信に自動で載る。
class Sensor {
 public:
  explicit Sensor(const char* name) : name_(name) {}
  virtual ~Sensor() = default;

  const char* name() const { return name_; }
  bool ok() const { return ok_; }
  std::vector<Metric>& metrics() { return metrics_; }

  // SensorHub から一定間隔で呼ばれる。見つからなければ 5 秒おきに begin() をやり直す。
  void poll(uint32_t nowMs);

 protected:
  // 見つかって使える状態になったら true
  virtual bool begin() = 0;
  // 成功したら metrics_[i].value を更新して true。
  // 3 回続けて失敗すると ok() が false になり、値は NAN になる。
  virtual bool read() = 0;

  // コンストラクタで値の定義を登録する
  void addMetric(const Metric& metric) { metrics_.push_back(metric); }

 private:
  void invalidate();

  const char* name_;
  std::vector<Metric> metrics_;
  bool ok_ = false;
  bool started_ = false;
  uint8_t failures_ = 0;
  uint32_t lastBeginMs_ = 0;
};

// 登録されたセンサーをまとめて扱う。値は登録順に並べた一覧 (metric(i)) でも参照できる。
class SensorHub {
 public:
  static constexpr size_t kMaxMetrics = 24;

  void add(Sensor* sensor);
  void poll(uint32_t nowMs);

  const std::vector<Sensor*>& sensors() const { return sensors_; }
  size_t metricCount() const { return metrics_.size(); }
  Metric& metric(size_t i) { return *metrics_[i]; }
  bool anyValid() const;

 private:
  std::vector<Sensor*> sensors_;
  std::vector<Metric*> metrics_;
};
