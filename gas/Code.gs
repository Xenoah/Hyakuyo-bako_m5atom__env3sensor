/**
 * M5Atom Matrix + ENV III から送られてくる測定値をスプレッドシートに追記する。
 *
 * スプレッドシートの「拡張機能 > Apps Script」に貼り付け、ウェブアプリとしてデプロイする。
 *   - 次のユーザーとして実行: 自分
 *   - アクセスできるユーザー: 全員
 * 合言葉を使う場合は「プロジェクトの設定 > スクリプト プロパティ」に TOKEN を追加し、
 * デバイス側 secrets.h の GAS_TOKEN と同じ値にする。
 */

const SHEET_NAME = 'data';
const HEADER = ['timestamp', 'temperature_c', 'humidity_pct', 'pressure_hpa', 'device', 'rssi_dbm', 'uptime_sec'];

function doPost(e) {
  try {
    const body = JSON.parse(e.postData.contents);

    const token = PropertiesService.getScriptProperties().getProperty('TOKEN');
    if (token && body.token !== token) {
      return json_({ ok: false, error: 'unauthorized' });
    }

    const lock = LockService.getScriptLock();
    lock.waitLock(10000);
    try {
      getSheet_().appendRow([
        new Date(),
        Number(body.temperature),
        Number(body.humidity),
        Number(body.pressure),
        String(body.device || ''),
        body.rssi ?? '',
        body.uptime ?? '',
      ]);
    } finally {
      lock.releaseLock();
    }
    return json_({ ok: true });
  } catch (err) {
    return json_({ ok: false, error: String(err) });
  }
}

// ブラウザでデプロイ URL を開いたときの疎通確認用
function doGet() {
  return json_({ ok: true, message: 'env3 logger is running' });
}

function getSheet_() {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  let sheet = ss.getSheetByName(SHEET_NAME);
  if (!sheet) {
    sheet = ss.insertSheet(SHEET_NAME);
  }
  if (sheet.getLastRow() === 0) {
    sheet.appendRow(HEADER);
    sheet.setFrozenRows(1);
    sheet.getRange('A:A').setNumberFormat('yyyy/MM/dd HH:mm:ss');
  }
  return sheet;
}

function json_(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj)).setMimeType(ContentService.MimeType.JSON);
}
