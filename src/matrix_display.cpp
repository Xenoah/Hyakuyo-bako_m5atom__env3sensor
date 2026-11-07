#include "matrix_display.h"

namespace {

struct Glyph {
  char ch;
  uint8_t width;
  uint8_t rows[5];  // 上の行から。各行の上位ビットが左。
};

// '*' は度記号 (°) として使う。
constexpr Glyph kFont[] = {
    {'0', 3, {0b111, 0b101, 0b101, 0b101, 0b111}},
    {'1', 3, {0b010, 0b110, 0b010, 0b010, 0b111}},
    {'2', 3, {0b111, 0b001, 0b111, 0b100, 0b111}},
    {'3', 3, {0b111, 0b001, 0b111, 0b001, 0b111}},
    {'4', 3, {0b101, 0b101, 0b111, 0b001, 0b001}},
    {'5', 3, {0b111, 0b100, 0b111, 0b001, 0b111}},
    {'6', 3, {0b111, 0b100, 0b111, 0b101, 0b111}},
    {'7', 3, {0b111, 0b001, 0b001, 0b010, 0b010}},
    {'8', 3, {0b111, 0b101, 0b111, 0b101, 0b111}},
    {'9', 3, {0b111, 0b101, 0b111, 0b001, 0b111}},
    {'.', 1, {0b0, 0b0, 0b0, 0b0, 0b1}},
    {'-', 3, {0b000, 0b000, 0b111, 0b000, 0b000}},
    {'%', 3, {0b101, 0b001, 0b010, 0b100, 0b101}},
    {'*', 3, {0b010, 0b101, 0b010, 0b000, 0b000}},
    {' ', 2, {0b00, 0b00, 0b00, 0b00, 0b00}},
    {'A', 3, {0b010, 0b101, 0b111, 0b101, 0b101}},
    {'C', 3,{0b111, 0b100, 0b100, 0b100, 0b111}},
    {'E', 3, {0b111, 0b100, 0b111, 0b100, 0b111}},
    {'H', 3, {0b101, 0b101, 0b111, 0b101, 0b101}},
    {'I', 3, {0b111, 0b010, 0b010, 0b010, 0b111}},
    {'N', 3, {0b110, 0b101, 0b101, 0b101, 0b101}},
    {'O', 3, {0b111, 0b101, 0b101, 0b101, 0b111}},
    {'P', 3, {0b111, 0b101, 0b111, 0b100, 0b100}},
    {'R', 3, {0b110, 0b101, 0b110, 0b101, 0b101}},
    {'T', 3, {0b111, 0b010, 0b010, 0b010, 0b010}},
    {'a', 3, {0b000, 0b011, 0b101, 0b101, 0b011}},
    {'h', 3, {0b100, 0b100, 0b111, 0b101, 0b101}},
};

const Glyph* findGlyph(char ch) {
  for (const Glyph& g : kFont) {
    if (g.ch == ch) return &g;
  }
  return nullptr;
}

// 外周 16 ピクセルを時計回りに並べたインデックス
constexpr uint8_t kRing[] = {0, 1, 2, 3, 4, 9, 14, 19, 24, 23, 22, 21, 20, 15, 10, 5};

uint32_t scale(uint32_t color, uint8_t level) {
  uint8_t r = ((color >> 16) & 0xFF) * level / 255;
  uint8_t g = ((color >> 8) & 0xFF) * level / 255;
  uint8_t b = (color & 0xFF) * level / 255;
  return (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}

}  // namespace

MatrixDisplay::MatrixDisplay(uint8_t pin) : strip_(kSize * kSize, pin, NEO_GRB + NEO_KHZ800) {}

void MatrixDisplay::begin(uint8_t brightness, uint16_t scrollIntervalMs) {
  brightness_ = brightness;
  scrollIntervalMs_ = scrollIntervalMs;
  strip_.begin();
  clear();
  render();
}

void MatrixDisplay::setBrightness(uint8_t brightness) {
  brightness_ = brightness;
  show();
}

void MatrixDisplay::clear() {
  tape_.assign(kSize, Column{0, 0});
  pos_ = 0;
}

void MatrixDisplay::appendText(const char* text, uint32_t color) {
  for (const char* p = text; *p; ++p) {
    const Glyph* g = findGlyph(*p);
    if (!g) continue;
    for (uint8_t x = 0; x < g->width; ++x) {
      uint8_t bits = 0;
      for (uint8_t y = 0; y < kSize; ++y) {
        if (g->rows[y] & (1 << (g->width - 1 - x))) bits |= 1 << y;
      }
      tape_.push_back({bits, color});
    }
    tape_.push_back({0, 0});
  }
}

void MatrixDisplay::appendGap(uint8_t columns) { tape_.insert(tape_.end(), columns, Column{0, 0}); }

void MatrixDisplay::update(uint32_t nowMs) {
  if (nowMs - lastStepMs_ < scrollIntervalMs_) return;
  lastStepMs_ = nowMs;

  if (tape_.size() - pos_ <= kSize) {
    tape_.erase(tape_.begin(), tape_.begin() + pos_);
    pos_ = 0;
    if (refill_) refill_(*this);
  }
  if (tape_.size() - pos_ > kSize) ++pos_;
  render();
}

void MatrixDisplay::drawSpinner(uint32_t nowMs, uint32_t color) {
  constexpr uint8_t kRingSize = sizeof(kRing);
  uint8_t head = (nowMs / 60) % kRingSize;
  for (uint32_t& px : frame_) px = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    frame_[kRing[(head + kRingSize - i) % kRingSize]] = scale(color, 255 >> (i * 2));
  }
  show();
}

void MatrixDisplay::render() {
  for (uint8_t x = 0; x < kSize; ++x) {
    Column col = pos_ + x < tape_.size() ? tape_[pos_ + x] : Column{0, 0};
    for (uint8_t y = 0; y < kSize; ++y) {
      frame_[y * kSize + x] = (col.bits & (1 << y)) ? col.color : 0;
    }
  }
  show();
}

void MatrixDisplay::show() {
  for (uint8_t i = 0; i < kSize * kSize; ++i) {
    strip_.setPixelColor(i, scale(frame_[i], brightness_));
  }
  strip_.show();
}
