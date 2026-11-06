#pragma once

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#include <functional>
#include <vector>

// 5x5 の LED マトリクスに 3x5 フォントの文字列を横スクロール表示する。
// 表示待ちの列 (テープ) が尽きそうになると refill コールバックで次の文字列を要求する。
class MatrixDisplay {
 public:
  static constexpr uint8_t kSize = 5;
  using Refill = std::function<void(MatrixDisplay&)>;

  explicit MatrixDisplay(uint8_t pin);

  void begin(uint8_t brightness, uint16_t scrollIntervalMs);
  void setBrightness(uint8_t brightness);
  uint8_t brightness() const { return brightness_; }
  void setRefill(Refill refill) { refill_ = std::move(refill); }

  // 表示中のテープを捨てて、右端からスクロールし直す。
  void clear();
  void appendText(const char* text, uint32_t color);
  void appendGap(uint8_t columns);

  // スクロールを進める。loop() から毎回呼ぶ。
  void update(uint32_t nowMs);
  // Wi-Fi 接続待ちなどで使う外周のくるくる表示。
  void drawSpinner(uint32_t nowMs, uint32_t color);

  // 現在の 25 ピクセルの色 (輝度補正前)。Web のミラー表示用。
  uint32_t pixel(uint8_t index) const { return frame_[index]; }

 private:
  struct Column {
    uint8_t bits;  // bit0 = 最上段
    uint32_t color;
  };

  void render();
  void show();

  Adafruit_NeoPixel strip_;
  uint8_t brightness_ = 20;
  uint16_t scrollIntervalMs_ = 90;
  uint32_t lastStepMs_ = 0;
  std::vector<Column> tape_;
  size_t pos_ = 0;
  Refill refill_;
  uint32_t frame_[kSize * kSize] = {};
};
