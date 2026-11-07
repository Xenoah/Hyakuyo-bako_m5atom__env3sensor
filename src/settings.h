#pragma once

#include <Arduino.h>

#include <vector>

// GAS の送信先 1 件
struct GasTarget {
  String name;   // 画面に出す名前 (空でもよい)
  String url;    // ウェブアプリの URL。空なら送らない
  String token;  // GAS のスクリプトプロパティ TOKEN と同じ値
};

// Web の設定画面から変更でき、NVS (フラッシュ) に保存される設定。
// 保存されていない項目は secrets.h / config.h の値が初期値になる。
struct Settings {
  static constexpr size_t kMaxGasTargets = 10;

  String wifiSsid;
  String wifiPassword;
  std::vector<GasTarget> gasTargets;  // 1〜kMaxGasTargets 件
  String adminPassword;               // 設定画面 (Basic 認証、ユーザー名 admin) と OTA のパスワード
  uint16_t uploadIntervalMin = 10;

  uint32_t uploadIntervalSec() const { return uploadIntervalMin * 60UL; }
  bool hasGasTarget() const;
};

void loadSettings(Settings& settings);
void saveSettings(const Settings& settings);
// 保存した設定を消して secrets.h の値に戻す
void resetSettings();
