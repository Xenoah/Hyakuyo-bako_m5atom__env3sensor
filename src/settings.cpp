#include "settings.h"

#include <Preferences.h>

#include "config.h"

namespace {
constexpr char kNamespace[] = "env3cfg";

// 未保存のキーを getString で読むとエラーログが出るので、先に有無を確かめる
String getString(Preferences& prefs, const char* key, const char* fallback) {
  return prefs.isKey(key) ? prefs.getString(key) : String(fallback);
}

// NVS のキーは 15 文字まで。送信先 i は g<i>n / g<i>u / g<i>t に保存する
String targetKey(size_t i, char field) { return String("g") + i + field; }
}  // namespace

bool Settings::hasGasTarget() const {
  for (const GasTarget& t : gasTargets) {
    if (!t.url.isEmpty()) return true;
  }
  return false;
}

void loadSettings(Settings& settings) {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  settings.wifiSsid = getString(prefs, "ssid", WIFI_SSID);
  settings.wifiPassword = getString(prefs, "pass", WIFI_PASSWORD);
  settings.adminPassword = getString(prefs, "adminPass", OTA_PASSWORD);
  settings.uploadIntervalMin = prefs.getUShort("uploadMin", DEFAULT_UPLOAD_INTERVAL_MIN);

  settings.gasTargets.clear();
  if (prefs.isKey("gasN")) {
    size_t n = min<size_t>(prefs.getUChar("gasN", 1), Settings::kMaxGasTargets);
    for (size_t i = 0; i < n; ++i) {
      settings.gasTargets.push_back({getString(prefs, targetKey(i, 'n').c_str(), ""),
                                     getString(prefs, targetKey(i, 'u').c_str(), ""),
                                     getString(prefs, targetKey(i, 't').c_str(), "")});
    }
  } else {
    // v1.1 までは送信先が 1 件だけで gasUrl / gasToken に保存していた
    settings.gasTargets.push_back(
        {"", getString(prefs, "gasUrl", GAS_URL), getString(prefs, "gasToken", GAS_TOKEN)});
  }
  if (settings.gasTargets.empty()) settings.gasTargets.push_back({});
  prefs.end();
}

void saveSettings(const Settings& settings) {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putString("ssid", settings.wifiSsid);
  prefs.putString("pass", settings.wifiPassword);
  prefs.putString("adminPass", settings.adminPassword);
  prefs.putUShort("uploadMin", settings.uploadIntervalMin);

  const size_t n = min(settings.gasTargets.size(), Settings::kMaxGasTargets);
  prefs.putUChar("gasN", n);
  for (size_t i = 0; i < Settings::kMaxGasTargets; ++i) {
    for (char field : {'n', 'u', 't'}) {
      String key = targetKey(i, field);
      if (i >= n) {
        if (prefs.isKey(key.c_str())) prefs.remove(key.c_str());
        continue;
      }
      const GasTarget& t = settings.gasTargets[i];
      prefs.putString(key.c_str(), field == 'n' ? t.name : field == 'u' ? t.url : t.token);
    }
  }
  if (prefs.isKey("gasUrl")) prefs.remove("gasUrl");
  if (prefs.isKey("gasToken")) prefs.remove("gasToken");
  prefs.end();
}

void resetSettings() {
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.clear();
  prefs.end();
}
