#include "settings.h"

#include <Preferences.h>

#include "config.h"

namespace {
constexpr char kNamespace[] = "env3cfg";

// 未保存のキーを getString で読むとエラーログが出るので、先に有無を確かめる
String getString(Preferences& prefs, const char* key, const char* fallback) {
  return prefs.isKey(key) ? prefs.getString(key) : String(fallback);
}
}  // namespace

void loadSettings(Settings& settings) {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  settings.wifiSsid = getString(prefs, "ssid", WIFI_SSID);
  settings.wifiPassword = getString(prefs, "pass", WIFI_PASSWORD);
  settings.gasUrl = getString(prefs, "gasUrl", GAS_URL);
  settings.gasToken = getString(prefs, "gasToken", GAS_TOKEN);
  settings.adminPassword = getString(prefs, "adminPass", OTA_PASSWORD);
  settings.uploadIntervalMin = prefs.getUShort("uploadMin", DEFAULT_UPLOAD_INTERVAL_MIN);
  prefs.end();
}

void saveSettings(const Settings& settings) {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putString("ssid", settings.wifiSsid);
  prefs.putString("pass", settings.wifiPassword);
  prefs.putString("gasUrl", settings.gasUrl);
  prefs.putString("gasToken", settings.gasToken);
  prefs.putString("adminPass", settings.adminPassword);
  prefs.putUShort("uploadMin", settings.uploadIntervalMin);
  prefs.end();
}

void resetSettings() {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.clear();
  prefs.end();
}
