#pragma once

#include <Arduino.h>

// secrets.h は設定の初期値。なければ空の値 (= 起動時にセットアップモード) になる。
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

// ホスト名。http://env3.local/ でもアクセスできます。
constexpr char DEVICE_NAME[] = "env3";

// M5Atom Matrix
constexpr uint8_t PIN_LED = 27;
constexpr uint8_t PIN_BUTTON = 39;
// Grove ポート (Unit ENV III)
constexpr uint8_t PIN_SDA = 26;
constexpr uint8_t PIN_SCL = 32;

// Wi-Fi 未設定・接続失敗時に立ち上げるセットアップ用アクセスポイントのパスワード (8文字以上)
constexpr char SETUP_AP_PASSWORD[] = "env3setup";

constexpr uint32_t SENSOR_INTERVAL_MS = 2000;
constexpr uint16_t DEFAULT_UPLOAD_INTERVAL_MIN = 10;  // GAS への送信間隔の初期値 (設定画面で変更可)
constexpr uint32_t HISTORY_INTERVAL_SEC = 60;         // Web 用履歴の記録間隔
constexpr size_t HISTORY_SIZE = 1440;           // 60秒 x 1440 = 24時間分

constexpr uint16_t SCROLL_INTERVAL_MS = 90;
constexpr uint8_t DEFAULT_BRIGHTNESS = 20;
constexpr uint8_t MAX_BRIGHTNESS = 80;  // マトリクスは発熱するので上限を抑える

// 表示色 (0xRRGGBB)
constexpr uint32_t COLOR_TEMPERATURE = 0xFF1493;  // 蛍光ピンク
constexpr uint32_t COLOR_HUMIDITY = 0x00FFFF;     // 蛍光シアン
constexpr uint32_t COLOR_PRESSURE = 0x39FF14;     // 蛍光グリーン
constexpr uint32_t COLOR_INFO = 0xFFFFFF;
constexpr uint32_t COLOR_ERROR = 0xFF0000;

#define TZ_INFO "JST-9"
#define NTP_SERVER1 "ntp.nict.jp"
#define NTP_SERVER2 "pool.ntp.org"
