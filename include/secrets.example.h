#pragma once

// 設定はブラウザの設定画面 (/settings) から変更でき、本体に保存されます。
// ここに書いた値は「まだ何も保存されていないとき」の初期値です。
// 使う場合はこのファイルを include/secrets.h にコピーしてください (.gitignore 済み)。
// WIFI_SSID が空なら、起動時にセットアップ用アクセスポイントが立ち上がります。

#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// GAS をウェブアプリとしてデプロイした URL (https://script.google.com/macros/s/XXXX/exec)
// 空のままならアップロードはしません。
#define GAS_URL ""
// GAS 側のスクリプトプロパティ TOKEN と同じ値。GAS 側で未設定なら空でOK。
#define GAS_TOKEN ""

// 設定画面 (ユーザー名 admin) と ArduinoOTA のパスワード。空ならパスワードなし。
#define OTA_PASSWORD ""
