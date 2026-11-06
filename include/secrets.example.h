#pragma once

// このファイルを include/secrets.h にコピーして値を書き換えてください。
// secrets.h は .gitignore 済みなのでコミットされません。

#define WIFI_SSID "your-ssid"
#define WIFI_PASSWORD "your-password"

// GAS をウェブアプリとしてデプロイした URL (https://script.google.com/macros/s/XXXX/exec)
// 空のままならアップロードはしません。
#define GAS_URL ""
// GAS 側のスクリプトプロパティ TOKEN と同じ値。GAS 側で未設定なら空でOK。
#define GAS_TOKEN ""

// ArduinoOTA のパスワード。空ならパスワードなし。
#define OTA_PASSWORD ""
