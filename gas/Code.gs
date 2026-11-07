/**
 * 百葉箱ロガー (M5Atom Matrix など) の受け口と読み出し API。
 *
 * スプレッドシートの「拡張機能 > Apps Script」に貼り付け、ウェブアプリとしてデプロイする。
 *   - 次のユーザーとして実行: 自分
 *   - アクセスできるユーザー: 全員
 *
 * スクリプトプロパティ (プロジェクトの設定 > スクリプト プロパティ) で次を設定できる。どちらも任意。
 *   TOKEN       … デバイスからの書き込みに必要な合言葉 (デバイス側の「合言葉」と同じ値)
 *   READ_TOKEN  … 読み出し API に必要な合言葉。未設定なら URL を知っていれば誰でも読める
 *
 * ■ 書き込み (POST, デバイスが送る)
 *   {"token": "...", "device": "env3", "values": {"temperature_c": 21.6, "humidity_pct": 41.2, ...}}
 *   values のキーがそのまま列名になる。知らないキーが来たら右端に列を足すので、
 *   デバイスにセンサーを追加してもこのスクリプトを変える必要はない。
 *
 * ■ 読み出し (GET)
 *   ?action=latest                       最新の 1 行 (action 省略時も同じ)
 *   ?action=at&time=2026-11-07T19:30      指定時刻にいちばん近い 1 行
 *   ?action=range&from=...&to=...         期間内の行 (limit で件数上限、既定 500 / 最大 5000)
 *   ?action=info                          列名・行数・最初と最後の時刻
 *   共通: device=env3 でデバイスを絞り込み、keys=temperature_c,humidity_pct で列を絞り込み、
 *         READ_TOKEN を設定したときは token=... が必要
 *   時刻は 2026-11-07T19:30:00+09:00 / 2026-11-07 19:30 (スクリプトのタイムゾーン) / UNIX 秒・ミリ秒
 */

const SHEET_NAME = 'data';
const TIME_COLUMN = 'timestamp';
const DEVICE_COLUMN = 'device';
const DEFAULT_LIMIT = 500;
const MAX_LIMIT = 5000;

// ---------------------------------------------------------------- 書き込み

function doPost(e) {
  try {
    const body = JSON.parse(e.postData.contents);
    const token = PropertiesService.getScriptProperties().getProperty('TOKEN');
    if (token && body.token !== token) {
      return json_({ ok: false, error: 'unauthorized' });
    }
    const values = body.values || legacyValues_(body);

    const lock = LockService.getScriptLock();
    lock.waitLock(10000);
    try {
      const sheet = getSheet_();
      const header = ensureColumns_(sheet, Object.keys(values));
      const row = header.map((name) => {
        if (name === TIME_COLUMN) return new Date();
        if (name === DEVICE_COLUMN) return String(body.device || '');
        const v = values[name];
        return v === undefined || v === null ? '' : v;
      });
      sheet.appendRow(row);
    } finally {
      lock.releaseLock();
    }
    return json_({ ok: true });
  } catch (err) {
    return json_({ ok: false, error: String(err) });
  }
}

// v1.1 以前のファームウェアが送っていた形式
function legacyValues_(body) {
  return {
    temperature_c: body.temperature,
    humidity_pct: body.humidity,
    pressure_hpa: body.pressure,
    rssi_dbm: body.rssi,
    uptime_sec: body.uptime,
  };
}

function getSheet_() {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  const sheet = ss.getSheetByName(SHEET_NAME) || ss.insertSheet(SHEET_NAME);
  if (sheet.getLastRow() === 0) {
    sheet.appendRow([TIME_COLUMN, DEVICE_COLUMN]);
    sheet.setFrozenRows(1);
    sheet.getRange('A:A').setNumberFormat('yyyy/MM/dd HH:mm:ss');
  }
  return sheet;
}

function readHeader_(sheet) {
  const lastCol = sheet.getLastColumn();
  return lastCol === 0 ? [] : sheet.getRange(1, 1, 1, lastCol).getValues()[0].map(String);
}

// 足りない列を右端に追加して、最新のヘッダーを返す
function ensureColumns_(sheet, names) {
  const header = readHeader_(sheet);
  const missing = names.filter((n) => header.indexOf(n) < 0);
  if (missing.length) {
    sheet.getRange(1, header.length + 1, 1, missing.length).setValues([missing]);
    header.push(...missing);
  }
  return header;
}

// ---------------------------------------------------------------- 読み出し API

function doGet(e) {
  try {
    const p = e.parameter || {};
    const readToken = PropertiesService.getScriptProperties().getProperty('READ_TOKEN');
    if (readToken && p.token !== readToken) {
      return json_({ ok: false, error: 'unauthorized' });
    }

    const sheet = SpreadsheetApp.getActiveSpreadsheet().getSheetByName(SHEET_NAME);
    if (!sheet || sheet.getLastRow() < 2) return json_({ ok: false, error: 'no data' });
    const table = new Table_(sheet, p.device, p.keys);

    switch (p.action || 'latest') {
      case 'latest': {
        const i = table.count() - 1;
        return i < 0 ? json_({ ok: false, error: 'no data' }) : json_({ ok: true, data: table.record(i) });
      }
      case 'at': {
        const t = parseTime_(p.time);
        if (t === null) return json_({ ok: false, error: 'time is required (e.g. 2026-11-07T19:30)' });
        const i = table.nearest(t);
        if (i < 0) return json_({ ok: false, error: 'no data' });
        const record = table.record(i);
        return json_({ ok: true, data: record, diffSec: Math.round((table.time(i) - t) / 1000) });
      }
      case 'range': {
        const from = p.from ? parseTime_(p.from) : -Infinity;
        const to = p.to ? parseTime_(p.to) : Infinity;
        if (from === null || to === null) return json_({ ok: false, error: 'invalid from / to' });
        const limit = Math.min(Math.max(parseInt(p.limit, 10) || DEFAULT_LIMIT, 1), MAX_LIMIT);
        const data = [];
        for (let i = table.lowerBound(from); i < table.count() && table.time(i) <= to; i++) {
          if (data.length >= limit) break;
          data.push(table.record(i));
        }
        return json_({ ok: true, count: data.length, data });
      }
      case 'info': {
        const n = table.count();
        return json_({
          ok: true,
          columns: table.columns,
          rows: n,
          first: n ? formatTime_(new Date(table.time(0))) : null,
          last: n ? formatTime_(new Date(table.time(n - 1))) : null,
          timeZone: Session.getScriptTimeZone(),
        });
      }
      default:
        return json_({ ok: false, error: 'unknown action: ' + p.action });
    }
  } catch (err) {
    return json_({ ok: false, error: String(err) });
  }
}

// シートを読み出して、デバイスで絞り込んだ行を時刻順に扱う
class Table_ {
  constructor(sheet, device, keys) {
    const values = sheet.getRange(1, 1, sheet.getLastRow(), sheet.getLastColumn()).getValues();
    const header = values[0].map(String);
    this.timeCol = header.indexOf(TIME_COLUMN);
    this.deviceCol = header.indexOf(DEVICE_COLUMN);
    const wanted = keys ? String(keys).split(',').map((k) => k.trim()).filter(Boolean) : null;
    this.columns = header.filter((name) => !wanted || wanted.indexOf(name) >= 0 || name === TIME_COLUMN || name === DEVICE_COLUMN);
    this.colIndex = this.columns.map((name) => header.indexOf(name));
    this.rows = values.slice(1).filter((r) => r[this.timeCol] instanceof Date && (!device || String(r[this.deviceCol]) === device));
    this.rows.sort((a, b) => a[this.timeCol] - b[this.timeCol]);
  }
  count() {
    return this.rows.length;
  }
  time(i) {
    return this.rows[i][this.timeCol].getTime();
  }
  record(i) {
    const out = {};
    this.columns.forEach((name, k) => {
      const v = this.rows[i][this.colIndex[k]];
      out[name] = v instanceof Date ? formatTime_(v) : v === '' ? null : v;
    });
    return out;
  }
  // time(i) >= t となる最初の i
  lowerBound(t) {
    let lo = 0;
    let hi = this.rows.length;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (this.time(mid) < t) lo = mid + 1;
      else hi = mid;
    }
    return lo;
  }
  nearest(t) {
    const n = this.rows.length;
    if (n === 0) return -1;
    const i = this.lowerBound(t);
    if (i === 0) return 0;
    if (i === n) return n - 1;
    return t - this.time(i - 1) <= this.time(i) - t ? i - 1 : i;
  }
}

// ISO 8601 / "yyyy-MM-dd HH:mm[:ss]" (スクリプトのタイムゾーン) / UNIX 秒・ミリ秒 → ミリ秒。読めなければ null
function parseTime_(text) {
  if (text === undefined || text === null || text === '') return null;
  const s = String(text).trim();
  if (/^\d+(\.\d+)?$/.test(s)) {
    const n = Number(s);
    return n < 1e12 ? n * 1000 : n;
  }
  const m = s.match(/^(\d{4})-(\d{1,2})-(\d{1,2})(?:[ T](\d{1,2}):(\d{2})(?::(\d{2}))?)?$/);
  if (m) {
    // タイムゾーン指定なし → スクリプトのタイムゾーンとして解釈する
    const utc = Date.UTC(+m[1], +m[2] - 1, +m[3], +(m[4] || 0), +(m[5] || 0), +(m[6] || 0));
    const offset = Utilities.formatDate(new Date(utc), Session.getScriptTimeZone(), 'Z'); // 例: +0900
    const sign = offset[0] === '-' ? -1 : 1;
    const minutes = sign * (parseInt(offset.substr(1, 2), 10) * 60 + parseInt(offset.substr(3, 2), 10));
    return utc - minutes * 60000;
  }
  const t = Date.parse(s);
  return isNaN(t) ? null : t;
}

function formatTime_(date) {
  return Utilities.formatDate(date, Session.getScriptTimeZone(), "yyyy-MM-dd'T'HH:mm:ssXXX");
}

function json_(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj)).setMimeType(ContentService.MimeType.JSON);
}
