#pragma once

#include <Arduino.h>

// Web の設定画面から変更でき、NVS (フラッシュ) に保存される設定。
// 保存されていない項目は secrets.h / config.h の値が初期値になる。
struct Settings {
  String wifiSsid;
  String wifiPassword;
  String gasUrl;
  String gasToken;
  String adminPassword;  // 設定画面 (Basic 認証、ユーザー名 admin) と OTA のパスワード
  uint16_t uploadIntervalMin = 10;

  uint32_t uploadIntervalSec() const { return uploadIntervalMin * 60UL; }
};

void loadSettings(Settings& settings);
void saveSettings(const Settings& settings);
// 保存した設定を消して secrets.h の値に戻す
void resetSettings();
