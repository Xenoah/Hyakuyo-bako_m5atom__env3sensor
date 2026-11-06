# 百葉箱 M5Atom Matrix + ENV III

M5Atom Matrix に Unit ENV III (SHT30 + QMP6988) をつなぎ、

- 5x5 マトリクスに 気温 / 湿度 / 気圧 をスクロール表示
- 10 分ごとに Google Apps Script 経由でスプレッドシートへ記録
- 同じ LAN のブラウザから現在値・24 時間グラフ・送信状況を確認

するファームウェアです。PlatformIO でビルドします。

## 配線

ENV III を Atom Matrix の Grove ポートに挿すだけです (SDA=G26, SCL=G32)。

## マトリクス表示

| 項目 | 色 | 例 (10進数) | 例 (2進数) |
| --- | --- | --- | --- |
| 気温 | 蛍光ピンク | `23.4°C` | `10111°C` |
| 湿度 | 蛍光シアン | `45.6%` | `101110%` |
| 気圧 | 蛍光グリーン | `1013.2hPa` | `1111110101hPa` |

- **ボタンをクリック**: 10進数 / 2進数 を切り替え (2進数は整数に四捨五入した値)。設定は再起動後も保持されます。
- **ボタンを長押し (1秒)**: IP アドレスを白で流します。起動して Wi-Fi につながったときにも 1 回流れます。
- センサーが見つからないときは赤で `ERR` を流します。

## セットアップ

### 1. Google スプレッドシートと GAS

1. 新しいスプレッドシートを作り、「拡張機能 > Apps Script」を開く。
2. [gas/Code.gs](gas/Code.gs) の内容を貼り付けて保存。
3. (任意) 「プロジェクトの設定 > スクリプト プロパティ」に `TOKEN` を追加し、適当な合言葉を設定。
4. 「デプロイ > 新しいデプロイ > 種類: ウェブアプリ」
   - 次のユーザーとして実行: **自分**
   - アクセスできるユーザー: **全員**
5. 表示されたウェブアプリの URL (`https://script.google.com/macros/s/.../exec`) を控える。

`data` シートが自動で作られ、`timestamp, temperature_c, humidity_pct, pressure_hpa, device, rssi_dbm, uptime_sec` の順に追記されます。
タイムスタンプは GAS 側の受信時刻です。

> Code.gs を書き換えたときは「デプロイを管理 > 編集 > バージョン: 新バージョン」で更新すると URL が変わりません。

### 2. secrets.h

```sh
cp include/secrets.example.h include/secrets.h
```

`WIFI_SSID` / `WIFI_PASSWORD` / `GAS_URL` / `GAS_TOKEN` (手順 3 で設定した場合) を書き換えます。
`secrets.h` は `.gitignore` 済みです。ESP32 は 2.4GHz の Wi-Fi にしかつながりません。

### 3. ビルドと書き込み

```sh
pio run -e m5atom-matrix -t upload
pio device monitor
```

一度 USB で書き込んだ後は Wi-Fi 経由でも書き込めます (`OTA_PASSWORD` を設定したら [platformio.ini](platformio.ini) の `--auth` も合わせる)。

```sh
pio run -e m5atom-matrix-ota -t upload
```

## ブラウザ

マトリクスに流れる IP、または `http://env3.local/` を開きます (mDNS 非対応の Android などは IP で)。

- 現在の気温・湿度・気圧と 2 進数表記
- 24 時間のグラフと最低 / 最高 (1 分ごとに記録。デバイス内 RAM に保持するので再起動で消えます)
- マトリクスのミラー表示、10進 / 2進 切り替え、明るさ調整、IP 表示
- スプレッドシート送信の状態、次回送信までの時間、「今すぐ送信」
- Wi-Fi 強度、稼働時間、空きメモリ
- 履歴 CSV のダウンロード

### API

| メソッド | パス | 内容 |
| --- | --- | --- |
| GET | `/api/status` | 現在値・送信状況・デバイス情報 (JSON) |
| GET | `/api/history` | 直近 24 時間の履歴 (JSON, `[uptime秒, 気温, 湿度, 気圧]` の配列) |
| GET | `/history.csv` | 履歴 CSV |
| GET | `/api/matrix` | マトリクス 25 ピクセルの色 (`rrggbb` x 25) |
| POST | `/api/mode?value=decimal\|binary` | 表示モード切り替え (`value` なしでトグル) |
| POST | `/api/brightness?value=1..80` | 明るさ |
| POST | `/api/upload` | 今すぐスプレッドシートへ送信 |
| POST | `/api/showip` | マトリクスに IP を流す |

## 送信タイミング

NTP で時刻が合っていれば毎時 00, 10, 20 … 分ちょうどに送ります。時刻が合う前は起動直後に 1 回送り、以降 10 分おき。
失敗したときは 30 秒おきに 2 回まで再送します。

## 設定の変更

間隔・色・ピン・明るさの上限などは [include/config.h](include/config.h) にまとめています。

## 補足

- SHT30 は Atom 本体の熱を拾いやすいので、ケーブルで少し離して設置すると気温が正確になります。
- マトリクスは明るくすると発熱するため、明るさの上限を 80/255 にしています。
- GAS への HTTPS は証明書検証を省略しています (`setInsecure`)。送っているのは測定値と合言葉だけです。
