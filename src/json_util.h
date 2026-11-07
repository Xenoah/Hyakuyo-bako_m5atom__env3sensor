#pragma once

#include <Arduino.h>

// 文字列を JSON の文字列リテラルにする (前後の " 付き)
inline String jsonString(const String& value) {
  String out = "\"";
  for (const char* p = value.c_str(); *p; ++p) {
    if (*p == '"' || *p == '\\') {
      out += '\\';
      out += *p;
    } else if ((uint8_t)*p < 0x20) {
      char esc[8];
      snprintf(esc, sizeof(esc), "\\u%04x", *p);
      out += esc;
    } else {
      out += *p;
    }
  }
  return out + "\"";
}

// 数値を JSON にする。NAN は null
inline String jsonNumber(float value, uint8_t decimals) {
  if (isnan(value)) return "null";
  return String(value, (unsigned int)decimals);
}
