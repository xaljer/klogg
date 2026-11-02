import assert from 'node:assert/strict';
import test from 'node:test';

import { desktopBridge, dialogDirectory } from '../src/lib/desktop.ts';

type TestWindow = { kloggDesktop?: Partial<ReturnType<typeof fakeBridge>> };

function fakeBridge() {
  return {
    platform: 'win32',
    openFiles: async () => ['C:\\logs\\a.log'],
    revealItem: async () => {},
  };
}

function withWindow<T>(windowValue: TestWindow | undefined, run: () => T): T {
  const globals = globalThis as { window?: unknown };
  const previous = globals.window;
  globals.window = windowValue;
  try {
    return run();
  } finally {
    globals.window = previous;
  }
}

test('a plain browser has no desktop bridge', () => {
  assert.equal(withWindow(undefined, () => desktopBridge()), null);
  assert.equal(withWindow({}, () => desktopBridge()), null);
  assert.equal(withWindow({ kloggDesktop: { platform: 'win32' } }, () => desktopBridge()), null);
});

test('the preload bridge is detected when it can open files', () => {
  const bridge = withWindow({ kloggDesktop: fakeBridge() }, () => desktopBridge());
  assert.equal(bridge?.platform, 'win32');
});

test('dialog directories come from file references, not URLs', () => {
  assert.equal(dialogDirectory('D:\\logs\\app.log'), 'D:\\logs');
  assert.equal(dialogDirectory('D:\\logs\\'), 'D:\\logs');
  assert.equal(dialogDirectory('D:\\logs'), 'D:\\');
  assert.equal(dialogDirectory('D:\\'), 'D:\\');
  assert.equal(dialogDirectory('/var/log/syslog'), '/var/log');
  assert.equal(dialogDirectory('/var/log/'), '/var/log');
  assert.equal(dialogDirectory('app.log'), '');
  assert.equal(dialogDirectory(''), '');
  assert.equal(dialogDirectory('https://example.com/logs/a.log'), '');
});
