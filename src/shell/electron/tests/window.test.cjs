const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');

const {
  buildWindowUrl,
  nextWindowId,
  replaceWindowId,
} = require('../electron/window-url.cjs');
const {
  loadWindowState,
  saveWindowState,
} = require('../electron/window-state.cjs');

test('adds window, file and token query parameters', () => {
  assert.equal(
    buildWindowUrl('http://127.0.0.1:49152', {
      windowId: 'w1',
      file: 'C:\\logs\\a.log',
      token: 'secret',
    }),
    'http://127.0.0.1:49152/?window=w1&file=C%3A%5Clogs%5Ca.log&token=secret',
  );
});

test('omits empty optional parameters', () => {
  assert.equal(
    buildWindowUrl('http://127.0.0.1:49152/', { windowId: 'w1', file: '', token: '' }),
    'http://127.0.0.1:49152/?window=w1',
  );
});

test('replaces a copied window id while preserving file and token', () => {
  assert.equal(
    replaceWindowId(
      'http://127.0.0.1:49152/?window=w1&file=a.log&token=secret',
      'w2',
    ),
    'http://127.0.0.1:49152/?window=w2&file=a.log&token=secret',
  );
});

test('nextWindowId avoids reusing a saved window id', () => {
  assert.equal(nextWindowId({ windows: [] }), 'w1');
  assert.equal(nextWindowId({ windows: [{ id: 'w1' }] }), 'w2');
  assert.equal(nextWindowId({ windows: [{ id: 'w1' }, { id: 'w3' }] }), 'w4');
});

test('window state round-trips through a file', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'klogg-window-state-'));
  const file = path.join(dir, 'windows.json');
  const state = {
    windows: [
      {
        id: 'w1',
        bounds: { x: 10, y: 20, width: 1200, height: 800 },
        maximized: false,
      },
    ],
  };

  saveWindowState(file, state);
  assert.deepEqual(loadWindowState(file), state);
  fs.rmSync(dir, { recursive: true, force: true });
});

test('missing or malformed window state falls back to empty', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'klogg-window-state-'));
  const missing = path.join(dir, 'missing.json');
  const malformed = path.join(dir, 'bad.json');
  fs.writeFileSync(malformed, '{bad json');

  assert.deepEqual(loadWindowState(missing), { windows: [] });
  assert.deepEqual(loadWindowState(malformed), { windows: [] });
  fs.rmSync(dir, { recursive: true, force: true });
});
