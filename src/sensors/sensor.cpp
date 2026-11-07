#include "sensor.h"

void Sensor::poll(uint32_t nowMs) {
  if (!ok_) {
    if (started_ && nowMs - lastBeginMs_ < 5000) return;
    started_ = true;
    lastBeginMs_ = nowMs;
    ok_ = begin();
    Serial.printf("[sensor] %s: %s\n", name_, ok_ ? "ok" : "not found");
    failures_ = 0;
    return;  // 最初の変換が終わるのを待って、次の呼び出しから読む
  }

  if (read()) {
    failures_ = 0;
    return;
  }
  Serial.printf("[sensor] %s: read failed\n", name_);
  if (++failures_ >= 3) {
    ok_ = false;
    invalidate();
  }
}

void Sensor::invalidate() {
  for (Metric& m : metrics_) m.value = NAN;
}

void SensorHub::add(Sensor* sensor) {
  sensors_.push_back(sensor);
  for (Metric& m : sensor->metrics()) {
    if (metrics_.size() >= kMaxMetrics) {
      Serial.printf("[sensor] too many metrics, %s is ignored\n", m.key);
      continue;
    }
    metrics_.push_back(&m);
  }
}

void SensorHub::poll(uint32_t nowMs) {
  for (Sensor* s : sensors_) s->poll(nowMs);
}

bool SensorHub::anyValid() const {
  for (const Metric* m : metrics_) {
    if (!isnan(m->value)) return true;
  }
  return false;
}
