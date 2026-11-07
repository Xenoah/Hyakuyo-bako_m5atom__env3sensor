#pragma once

#include <Arduino.h>

#include <vector>

// Web のグラフ用に、全ての値を一定間隔で RAM に貯めるリングバッファ。
// 列の数は起動時に登録されたセンサーの値の数で決まる。
class History {
 public:
  void begin(size_t metricCount, size_t capacity) {
    metricCount_ = metricCount;
    capacity_ = capacity;
    times_.assign(capacity, 0);
    values_.assign(capacity * metricCount, NAN);
  }

  void record(uint32_t uptimeSec, const float* values) {
    times_[head_] = uptimeSec;
    for (size_t m = 0; m < metricCount_; ++m) values_[head_ * metricCount_ + m] = values[m];
    head_ = (head_ + 1) % capacity_;
    if (size_ < capacity_) ++size_;
  }

  size_t size() const { return size_; }
  // i = 0 が一番古い
  uint32_t time(size_t i) const { return times_[slot(i)]; }
  float value(size_t i, size_t metric) const { return values_[slot(i) * metricCount_ + metric]; }

 private:
  size_t slot(size_t i) const { return (head_ + capacity_ - size_ + i) % capacity_; }

  size_t metricCount_ = 0;
  size_t capacity_ = 0;
  size_t head_ = 0;
  size_t size_ = 0;
  std::vector<uint32_t> times_;
  std::vector<float> values_;
};
