const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, 'Code.gs'), 'utf8');
const HEADER = ['timestamp', 'temperature_c', 'humidity_pct', 'pressure_hpa', 'device', 'rssi_dbm', 'uptime_sec'];

// All fixtures are synthetic. No live sheet identifiers, endpoints or readings.
function row(time, temperature = 20, device = 'test-env3') {
  return [time, temperature, 45, 1012, device, -60, 600];
}

function pad(number, width = 2) {
  return String(number).padStart(width, '0');
}

// Mock the GAS Utilities date APIs using Node's timezone database, independently
// of the host process timezone. Production execution still needs GAS validation.
function fields(date, timeZone) {
  return Object.fromEntries(new Intl.DateTimeFormat('en-US', {
    timeZone, year: 'numeric', month: '2-digit', day: '2-digit',
    hour: '2-digit', minute: '2-digit', second: '2-digit', hourCycle: 'h23',
  }).formatToParts(date).filter((part) => part.type !== 'literal').map((part) => [part.type, part.value]));
}

function offsetMillis(date, timeZone) {
  const p = fields(date, timeZone);
  return Date.UTC(+p.year, +p.month - 1, +p.day, +p.hour, +p.minute, +p.second) -
    (date.getTime() - date.getUTCMilliseconds());
}

const utilities = {
  formatDate(date, timeZone, pattern) {
    const p = fields(date, timeZone);
    const civil = `${pad(p.year, 4)}-${p.month}-${p.day} ${p.hour}:${p.minute}:${p.second}`;
    const offset = offsetMillis(date, timeZone) / 60000;
    const sign = offset < 0 ? '-' : '+';
    const zone = `${sign}${pad(Math.floor(Math.abs(offset) / 60))}:${pad(Math.abs(offset) % 60)}`;
    if (pattern === 'Z') return zone.replace(':', '');
    if (pattern === 'yyyy-MM-dd HH:mm:ss.SSS') return `${civil}.${pad(date.getUTCMilliseconds(), 3)}`;
    if (pattern === "yyyy-MM-dd'T'HH:mm:ssXXX") return `${civil.replace(' ', 'T')}${offset === 0 ? 'Z' : zone}`;
    throw new Error(`Unsupported test format: ${pattern}`);
  },
  parseDate(text, timeZone, pattern) {
    assert.equal(pattern, 'yyyy-MM-dd HH:mm:ss.SSS');
    const nominal = Date.parse(`${text.replace(' ', 'T')}Z`);
    let result = nominal;
    for (let i = 0; i < 4; i++) result = nominal - offsetMillis(new Date(result), timeZone);
    return new Date(result);
  },
};

function setup(rows, options = {}) {
  const values = [HEADER.slice(), ...rows];
  const writes = [];
  const sheet = {
    getLastRow: () => values.length,
    getLastColumn: () => values[0].length,
    getRange: (startRow, startColumn, numRows, numColumns) => ({
      getValues: () => values.slice(startRow - 1, startRow - 1 + numRows)
        .map((r) => r.slice(startColumn - 1, startColumn - 1 + numColumns)),
      setValues: () => { throw new Error('Unexpected sheet edit'); },
    }),
    appendRow: (value) => { writes.push(value); },
  };
  const spreadsheet = {
    getSheetByName: (name) => name === 'data' ? sheet : null,
    getSpreadsheetTimeZone: () => options.sheetTimeZone || 'Asia/Tokyo',
    getSpreadsheetLocale: () => options.locale === undefined ? 'en_US' : options.locale,
    insertSheet: () => { throw new Error('Unexpected sheet creation'); },
  };
  if (options.missingSettings) {
    delete spreadsheet.getSpreadsheetTimeZone;
    delete spreadsheet.getSpreadsheetLocale;
  }
  if (options.failingSettings) {
    spreadsheet.getSpreadsheetTimeZone = () => { throw new Error('Unavailable'); };
    spreadsheet.getSpreadsheetLocale = () => { throw new Error('Unavailable'); };
  }
  const context = vm.createContext({
    SpreadsheetApp: { getActiveSpreadsheet: () => spreadsheet },
    Session: { getScriptTimeZone: () => options.scriptTimeZone || 'Asia/Tokyo' },
    Utilities: utilities,
    PropertiesService: { getScriptProperties: () => ({ getProperty: (name) => (options.tokens || {})[name] || null }) },
    LockService: { getScriptLock: () => ({ waitLock: () => {}, releaseLock: () => {} }) },
    ContentService: {
      MimeType: { JSON: 'application/json' },
      createTextOutput: (text) => ({ text, setMimeType() { return this; } }),
    },
  });
  vm.runInContext(source, context);
  return {
    context, values, writes,
    get: (parameter = {}) => JSON.parse(context.doGet({ parameter }).text),
    post: (body) => JSON.parse(context.doPost({ postData: { contents: JSON.stringify(body) } }).text),
    parse: (value, settings = { locale: 'en_us', timeZone: 'Asia/Tokyo' }) => context.parseSheetTime_(value, settings),
  };
}

test('valid native and cross-realm Dates are included, invalid Dates and impostors are not', () => {
  const foreignDate = vm.runInNewContext('new Date("2027-03-14T03:04:05Z")');
  const fakeDate = { getTime: () => Date.now(), [Symbol.toStringTag]: 'Date' };
  const app = setup([row(new Date('2027-03-14T01:00:00Z')), row(foreignDate, 22),
    row(new Date(NaN)), row(fakeDate)]);
  assert.equal(app.get({ action: 'info' }).rows, 2);
  assert.equal(app.get().data.timestamp, '2027-03-14T12:04:05+09:00');
  assert.equal(app.get().data.temperature_c, 22);
  const ownDate = vm.runInContext('new Date("2027-03-14T01:00:00Z")', app.context);
  assert.equal(app.parse(ownDate), Date.parse('2027-03-14T01:00:00Z'));
});

test('33 US text rows plus empty cells remain readable without changing source data', () => {
  const rows = Array.from({ length: 33 }, (_, index) => {
    const minutes = 8 * 60 + index * 10;
    return [...row(`3/14/2027 ${pad(Math.floor(minutes / 60))}:${pad(minutes % 60)}:09`, 18 + index / 10), ...Array(19).fill('')];
  });
  const app = setup([...rows.reverse(), ...Array.from({ length: 10 }, () => Array(26).fill(''))]);
  app.values[0].push(...Array(19).fill(''));
  const before = structuredClone(app.values);
  const info = app.get({ action: 'info' });
  assert.equal(info.rows, 33);
  assert.equal(info.first, '2027-03-14T08:00:09+09:00');
  assert.equal(info.last, '2027-03-14T13:20:09+09:00');
  const latest = app.get();
  assert.equal(latest.ok, true);
  assert.equal(latest.data.timestamp, info.last);
  assert.equal(latest.data.temperature_c, 21.2);
  assert.equal(latest.data.humidity_pct, 45);
  assert.equal(latest.data.pressure_hpa, 1012);
  assert.deepEqual(app.values, before);
  assert.deepEqual(app.writes, []);
});

test('year-first dates, slash dates, optional seconds and fractions use the spreadsheet timezone', () => {
  const app = setup([], { scriptTimeZone: 'Etc/UTC' });
  for (const value of ['2027-3-14 12:04', '2027/03/14T12:04:00', ' 2027-03-14 12:04:00.000 ']) {
    assert.equal(app.parse(value), Date.parse('2027-03-14T03:04:00Z'), value);
  }
  assert.equal(app.parse('2027/3/14'), Date.parse('2027-03-13T15:00:00Z'));
  assert.equal(app.parse('2028-02-29 01:02:03.12'), Date.parse('2028-02-28T16:02:03.120Z'));
  const output = setup([row('2027/3/14 12:04:05')], { scriptTimeZone: 'Etc/UTC' }).get();
  assert.equal(output.data.timestamp, '2027-03-14T03:04:05Z');
});

test('explicit ISO offsets are respected regardless of sheet locale or timezone', () => {
  const app = setup([]);
  for (const value of ['2027-03-14T12:04:05+09:00', '2027-03-14T03:04:05Z', '2027-03-13T20:04:05-07:00']) {
    assert.equal(app.parse(value, { locale: 'ja_jp', timeZone: 'America/New_York' }), Date.parse('2027-03-14T03:04:05Z'));
  }
  assert.equal(app.parse('2027-03-14T03:04:05.123Z'), Date.parse('2027-03-14T03:04:05.123Z'));
});

test('month/day/year requires explicit US locale and never guesses from the numbers', () => {
  for (const locale of ['', 'ja_JP', 'en_GB', 'fr_FR', 'unknown']) {
    assert.equal(setup([row('3/4/2027 05:06:07'), row('3/14/2027 05:06:07')], { locale }).get({ action: 'info' }).rows, 0, locale);
  }
  for (const locale of ['en_US', 'en-US']) {
    assert.equal(setup([row('3/4/2027 05:06:07')], { locale }).get().data.timestamp, '2027-03-04T05:06:07+09:00');
  }
  assert.equal(setup([row('14/3/2027 05:06:07')]).get().error, 'no data');
});

test('invalid, malformed, numeric, and unrecognized timestamps are excluded', () => {
  const app = setup([]);
  for (const value of ['', ' ', null, undefined, false, 0, 48000.5, 1805000000000, '48000.5', '1805000000000',
    'tomorrow', '2027-02-29', '2027/02/30', '2027-00-01', '2027-13-01', '2027-01-00',
    '2027-03-14 24:00:00', '2027-03-14 12:60:00', '2027-03-14 12:00:60',
    '2027-02-30T12:00:00Z', '2027-03-14T12:00:00+24:00', '2027-03-14T12:00:00+09:60',
    '2027-03/14', '2027-03-14junk', '3/4/27', '3/4/2027 1:00 PM']) {
    assert.equal(app.parse(value), null, String(value));
  }
});

test('missing spreadsheet settings fall back to script timezone without assuming US locale', () => {
  for (const mode of ['missingSettings', 'failingSettings']) {
    const app = setup([row('2027/03/14 05:06:07'), row('3/14/2027 05:06:07')], {
      [mode]: true, scriptTimeZone: 'America/New_York',
    });
    const info = app.get({ action: 'info' });
    assert.equal(info.rows, 1);
    assert.equal(info.first, '2027-03-14T05:06:07-04:00');
  }
  const app = setup([]);
  app.context.SpreadsheetApp.getActiveSpreadsheet = () => { throw new Error('Unavailable'); };
  app.context.Session.getScriptTimeZone = () => { throw new Error('Unavailable'); };
  const settings = app.context.sheetTimeContext_();
  assert.equal(settings.timeZone, 'Etc/UTC');
  assert.equal(settings.locale, '');
});

test('local dates follow daylight saving offsets and reject nonexistent local times', () => {
  const app = setup([]);
  const settings = { locale: 'en_us', timeZone: 'America/New_York' };
  assert.equal(app.parse('2027-01-14 12:00:00', settings), Date.parse('2027-01-14T17:00:00Z'));
  assert.equal(app.parse('2027-07-14 12:00:00', settings), Date.parse('2027-07-14T16:00:00Z'));
  assert.equal(app.parse('2027-03-14 02:30:00', settings), null);
  assert.equal(app.parse('2027-03-14 12:00:00', { ...settings, timeZone: 'Invalid/Zone' }), null);
});

test('latest, nearest and range use normalized chronological ordering and preserve values', () => {
  const app = setup([row('2027/03/14 09:20:00', 0), row('3/14/2027 09:00:00', -5),
    row(new Date('2027-03-14T00:10:00Z'), 19)]);
  assert.equal(app.get().data.temperature_c, 0);
  const nearest = app.get({ action: 'at', time: '2027-03-14T00:06:00Z' });
  assert.equal(nearest.data.temperature_c, 19);
  assert.equal(nearest.diffSec, 240);
  const range = app.get({ action: 'range', from: '2027-03-14T00:00:00Z', to: '2027-03-14T00:20:00Z', limit: '2' });
  assert.equal(range.count, 2);
  assert.deepEqual(range.data.map((r) => r.temperature_c), [-5, 19]);
  assert.equal(app.get({ action: 'range', from: '2027-03-14T00:10:00Z', to: '2027-03-14T00:10:00Z' }).count, 1);
});

test('device and key filters, blank values and empty result handling remain intact', () => {
  const app = setup([row('2027-03-14 09:00', '', 'one'), row('2027-03-14 09:10', 21, 'two')]);
  assert.deepEqual(app.get({ device: 'one', keys: 'temperature_c' }).data, {
    timestamp: '2027-03-14T09:00:00+09:00', temperature_c: null, device: 'one',
  });
  assert.equal(app.get({ device: 'missing' }).error, 'no data');
  assert.equal(setup([row('bad date')]).get({ action: 'info' }).first, null);
  assert.equal(setup([]).get().error, 'no data');
  const missingColumn = setup([row('2027-03-14')]);
  missingColumn.values[0][0] = 'other';
  assert.equal(missingColumn.get().error, 'no data');
});

test('read and write token behavior is unchanged; authorized POST still writes an actual Date', () => {
  const app = setup([row('2027-03-14')], { tokens: { TOKEN: 'test-write', READ_TOKEN: 'test-read' } });
  assert.deepEqual(app.get(), { ok: false, error: 'unauthorized' });
  assert.deepEqual(app.get({ token: 'wrong' }), { ok: false, error: 'unauthorized' });
  assert.equal(app.get({ token: 'test-read' }).ok, true);
  assert.deepEqual(app.post({ token: 'wrong', values: { temperature_c: 21 } }), { ok: false, error: 'unauthorized' });
  assert.equal(app.writes.length, 0);
  assert.deepEqual(app.post({ token: 'test-write', device: 'test-env3', values: { temperature_c: 21, humidity_pct: 40, pressure_hpa: 1015 } }), { ok: true });
  assert.equal(app.writes.length, 1);
  assert.ok(Number.isFinite(Date.prototype.getTime.call(app.writes[0][0])));
  assert.equal(app.writes[0][1], 21);
  assert.equal(app.writes[0][4], 'test-env3');
});
