import assert from 'node:assert/strict';
import { beforeEach, test } from 'node:test';

import {
  DEFAULT_SHORTCUTS,
  defaultSettings,
  isSingleKeyShortcut,
  loadSettings,
  saveSettings,
  SHORTCUT_SLOTS,
  type WebSettings,
} from '../src/lib/settings.ts';
import { installMemoryStorage } from './support/local-storage.ts';

const memory = installMemoryStorage();
const KEY = 'klogg.web.settings';

beforeEach(() => {
  memory.clear();
});

test('settings defaults include the current search and highlight options', () => {
  const settings = defaultSettings();
  assert.equal(settings.autoRunSearch, true);
  assert.equal(settings.liveSearch, true);
  assert.equal(settings.wordHighlightBackground, '#89b4fa');
  assert.equal(settings.wordHighlightForeground, '#1e1e2e');
  assert.equal(settings.customSearchMatchColors, false);
  assert.equal(settings.searchMatchBackground, '#f9e2af');
  assert.equal(settings.searchMatchForeground, '#1e1e2e');
  assert.equal(settings.restoreSession, true);
  assert.equal(settings.filePollIntervalMs, 1000);
  assert.equal(settings.recentLimit, 15);
  assert.equal(settings.searchHistoryLimit, 60);
});

test('settings fall back when storage is absent or malformed', () => {
  assert.deepEqual(loadSettings(), defaultSettings());
  localStorage.setItem(KEY, '{not json');
  assert.deepEqual(loadSettings(), defaultSettings());
});

test('settings clamp numbers, validate colors and keep booleans', () => {
  localStorage.setItem(
    KEY,
    JSON.stringify({
      fontSize: 99,
      filePollIntervalMs: 1,
      recentLimit: 999,
      searchHistoryLimit: 1,
      wordHighlightBackground: 'red',
      wordHighlightForeground: '#AABBCC',
      autoRunSearch: false,
      defaultSearchRegex: false,
      language: 'fr',
      theme: 'unknown',
    }),
  );

  const settings = loadSettings();
  assert.equal(settings.fontSize, 40);
  assert.equal(settings.filePollIntervalMs, 500);
  assert.equal(settings.recentLimit, 20);
  assert.equal(settings.searchHistoryLimit, 5);
  assert.equal(settings.wordHighlightBackground, '#89b4fa');
  assert.equal(settings.wordHighlightForeground, '#aabbcc');
  assert.equal(settings.autoRunSearch, false);
  assert.equal(settings.defaultSearchRegex, false);
  assert.equal(settings.language, 'auto');
  assert.equal(settings.theme, 'mocha');
});

test('live search and the larger history range round-trip', () => {
  localStorage.setItem(KEY, JSON.stringify({ searchHistoryLimit: 500, liveSearch: false }));
  const settings = loadSettings();
  assert.equal(settings.searchHistoryLimit, 200);
  assert.equal(settings.liveSearch, false);
});

test('legacy profiles adopt the new polling, recent and history defaults', () => {
  localStorage.setItem(
    KEY,
    JSON.stringify({ filePollIntervalMs: 2000, recentLimit: 8, searchHistoryLimit: 20 }),
  );
  const settings = loadSettings();
  assert.equal(settings.settingsVersion, defaultSettings().settingsVersion);
  assert.equal(settings.filePollIntervalMs, 1000);
  assert.equal(settings.recentLimit, 15);
  assert.equal(settings.searchHistoryLimit, 60);
});

test('values chosen after the migration are kept as-is', () => {
  localStorage.setItem(
    KEY,
    JSON.stringify({
      settingsVersion: 2,
      filePollIntervalMs: 2000,
      recentLimit: 8,
      searchHistoryLimit: 20,
    }),
  );
  const settings = loadSettings();
  assert.equal(settings.filePollIntervalMs, 2000);
  assert.equal(settings.recentLimit, 8);
  assert.equal(settings.searchHistoryLimit, 20);
});

test('search match colours validate and survive a reload', () => {
  localStorage.setItem(
    KEY,
    JSON.stringify({
      customSearchMatchColors: true,
      searchMatchBackground: '#ABCDEF',
      searchMatchForeground: 'not-a-colour',
    }),
  );
  const settings = loadSettings();
  assert.equal(settings.customSearchMatchColors, true);
  assert.equal(settings.searchMatchBackground, '#abcdef');
  assert.equal(settings.searchMatchForeground, '#1e1e2e');
  saveSettings(settings);
  assert.equal(loadSettings().searchMatchBackground, '#abcdef');
  assert.equal(loadSettings().customSearchMatchColors, true);
});

test('settings keep any registered theme and reject unknown ones', () => {
  for (const theme of ['nord', 'nord-light', 'spring', 'latte'] as const) {
    localStorage.setItem(KEY, JSON.stringify({ theme }));
    assert.equal(loadSettings().theme, theme);
  }
  localStorage.setItem(KEY, JSON.stringify({ theme: 'solarized' }));
  assert.equal(loadSettings().theme, 'mocha');
});

test('settings round-trip through saveSettings', () => {
  const settings: WebSettings = {
    ...defaultSettings(),
    fontSize: 16,
    autoRunSearch: false,
    wordHighlightBackground: '#ff3366',
    shortcuts: { ...defaultSettings().shortcuts, mark: ['F2', ''] },
  };
  saveSettings(settings);
  const loaded = loadSettings();
  assert.equal(loaded.fontSize, 16);
  assert.equal(loaded.autoRunSearch, false);
  assert.equal(loaded.wordHighlightBackground, '#ff3366');
  assert.deepEqual(loaded.shortcuts.mark, ['F2', '']);
});

test('every action exposes two shortcut slots', () => {
  const settings = defaultSettings();
  for (const [action, values] of Object.entries(settings.shortcuts)) {
    assert.equal(values.length, SHORTCUT_SLOTS, `${action} should have two slots`);
    assert.equal(SHORTCUT_SLOTS, 2);
  }
  assert.deepEqual(settings.shortcuts.nextMatch, ['N', 'F3']);
  assert.deepEqual(settings.shortcuts.prevMatch, ['Shift+F3', 'Shift+N']);
  assert.deepEqual(settings.shortcuts.goTop, ['T', 'Ctrl+Home']);
  assert.deepEqual(settings.shortcuts.goBottom, ['Ctrl+End', 'Shift+G']);
  assert.deepEqual(settings.shortcuts.nextColorLabel, ['D', 'Ctrl+D']);
  assert.deepEqual(settings.shortcuts.wrap, ['W', '']);
  assert.deepEqual(settings.shortcuts.selectionUp, ['K', '']);
  assert.deepEqual(settings.shortcuts.selectionDown, ['J', '']);
  assert.deepEqual(settings.shortcuts.copyWithColor, ['Ctrl+Shift+C', '']);
});

test('a single-key binding is always the first slot', () => {
  for (const [action, values] of Object.entries(DEFAULT_SHORTCUTS)) {
    const singleKeys = values.filter(isSingleKeyShortcut);
    if (singleKeys.length > 0) {
      assert.ok(
        isSingleKeyShortcut(values[0]),
        `${action} should lead with its single-key binding, got ${JSON.stringify(values)}`,
      );
    }
  }
  assert.equal(isSingleKeyShortcut('W'), true);
  assert.equal(isSingleKeyShortcut(']'), true);
  assert.equal(isSingleKeyShortcut('Shift+E'), false);
  assert.equal(isSingleKeyShortcut('F3'), false);
  assert.equal(isSingleKeyShortcut('Ctrl+Home'), false);
  assert.equal(isSingleKeyShortcut('Ctrl+Shift+1'), false);
});

test('a three slot profile collapses to two slots with the letter key first', () => {
  localStorage.setItem(
    KEY,
    JSON.stringify({
      settingsVersion: 3,
      shortcuts: {
        nextMatch: ['F3', 'N', 'Ctrl+G'],
        goTop: ['Ctrl+Home', 'T', ''],
        mark: ['M', 'F2', ''],
      },
    }),
  );
  const loaded = loadSettings();
  assert.deepEqual(loaded.shortcuts.nextMatch, ['N', 'F3']);
  assert.deepEqual(loaded.shortcuts.goTop, ['T', 'Ctrl+Home']);
  assert.deepEqual(loaded.shortcuts.mark, ['M', 'F2']);
});

test('a stored two slot profile takes the new top binding once', () => {
  localStorage.setItem(
    KEY,
    JSON.stringify({
      settingsVersion: 2,
      shortcuts: { goTop: ['Ctrl+Home', ''], mark: ['M', ''] },
    }),
  );
  const adopted = loadSettings();
  assert.deepEqual(adopted.shortcuts.goTop, ['T', 'Ctrl+Home']);
  assert.deepEqual(adopted.shortcuts.mark, ['M', '']);

  // A profile written after the migration keeps a slot the user cleared.
  saveSettings({ ...adopted, shortcuts: { ...adopted.shortcuts, goTop: ['Ctrl+Home', ''] } });
  assert.deepEqual(loadSettings().shortcuts.goTop, ['Ctrl+Home', '']);
});
