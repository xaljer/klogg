import assert from 'node:assert/strict';
import { beforeEach, test } from 'node:test';

import {
  DEFAULT_HIGHLIGHT_RULES,
  DEFAULT_QUICK_DEFS,
  activeRules,
  activeSet,
  createHighlightRule,
  createHighlighterSet,
  exportHighlighterStore,
  importHighlighterStore,
  loadHighlighterStore,
  loadQuickDefs,
  patternError,
  saveHighlighterStore,
  toApiColor,
  toHtmlColor,
} from '../src/lib/highlighters.ts';
import { installMemoryStorage } from './support/local-storage.ts';

const memory = installMemoryStorage();

beforeEach(() => {
  memory.clear();
});

test('highlighters expose the desktop default rule and label sets', () => {
  assert.equal(DEFAULT_HIGHLIGHT_RULES.length, 2);
  assert.equal(DEFAULT_QUICK_DEFS.length, 10);
  const store = loadHighlighterStore();
  assert.equal(store.sets.length, 1);
  assert.deepEqual(activeRules(store), DEFAULT_HIGHLIGHT_RULES);
  assert.deepEqual(loadQuickDefs(), DEFAULT_QUICK_DEFS);
});

test('sets and the active set round-trip through storage', () => {
  const first = createHighlighterSet('First', [createHighlightRule({ pattern: 'aaa' })], 'set-a');
  const second = createHighlighterSet('Second', [createHighlightRule({ pattern: 'bbb' })], 'set-b');
  saveHighlighterStore({ sets: [first, second], activeSetId: 'set-b' });

  const loaded = loadHighlighterStore();
  assert.equal(loaded.sets.length, 2);
  assert.equal(activeSet(loaded)?.name, 'Second');
  assert.deepEqual(
    activeRules(loaded).map((rule) => rule.pattern),
    ['bbb'],
  );
});

test('a stored active set that disappeared falls back to the first set', () => {
  const only = createHighlighterSet('Only', [], 'set-only');
  saveHighlighterStore({ sets: [only], activeSetId: 'missing' });
  assert.equal(loadHighlighterStore().activeSetId, 'set-only');
});

test('the flat rule list of older builds migrates into one set', () => {
  const legacy = [{ ...DEFAULT_HIGHLIGHT_RULES[0], name: 'custom', pattern: 'custom' }];
  localStorage.setItem('klogg.web.highlighters', JSON.stringify(legacy));

  const store = loadHighlighterStore();
  assert.equal(store.sets.length, 1);
  assert.equal(store.sets[0].rules[0].pattern, 'custom');
  assert.equal(localStorage.getItem('klogg.web.highlighters'), null, 'legacy key is removed');
  assert.ok(localStorage.getItem('klogg.web.highlighterSets'), 'new key is written');
});

test('malformed stored sets are normalized instead of breaking the panel', () => {
  localStorage.setItem(
    'klogg.web.highlighterSets',
    JSON.stringify({ sets: [{ name: '  ', rules: [{ pattern: 'x', enabled: 'yes' }, 'junk'] }] }),
  );
  const store = loadHighlighterStore();
  assert.equal(store.sets.length, 1);
  assert.equal(store.sets[0].name, 'Default');
  assert.equal(store.sets[0].rules.length, 1);
  assert.equal(store.sets[0].rules[0].enabled, true, 'non-boolean flags fall back to defaults');
});

test('import merges new sets and skips duplicate ids or names', () => {
  const existing = createHighlighterSet('Mine', [], 'set-mine');
  const store = { sets: [existing], activeSetId: 'set-mine' };
  const payload = exportHighlighterStore({
    sets: [
      existing,
      createHighlighterSet('Other', [createHighlightRule({ pattern: 'other' })], 'set-other'),
      createHighlighterSet('Mine', [], 'set-renamed'),
    ],
    activeSetId: 'set-mine',
  });

  const result = importHighlighterStore(store, payload);
  assert.equal(result.imported, 1);
  assert.equal(result.skipped, 2);
  assert.deepEqual(
    result.store.sets.map((set) => set.name),
    ['Mine', 'Other'],
  );
});

test('patternError reports empty and invalid patterns', () => {
  assert.equal(patternError(createHighlightRule({ pattern: '' })), 'empty');
  assert.equal(patternError(createHighlightRule({ pattern: 'ok', useRegex: true })), '');
  assert.equal(patternError(createHighlightRule({ pattern: '(', useRegex: false })), '');
  assert.notEqual(patternError(createHighlightRule({ pattern: '(', useRegex: true })), '');
});

test('old eight-color label layouts migrate to ten desktop labels', () => {
  localStorage.setItem('klogg.web.quickHighlighters', JSON.stringify(DEFAULT_QUICK_DEFS.slice(0, 8)));
  assert.deepEqual(loadQuickDefs(), DEFAULT_QUICK_DEFS);
});

test('color conversion supports desktop #AARRGGBB and HTML #RRGGBB', () => {
  assert.equal(toApiColor('#ff89b4fa'), '#ff89b4fa');
  assert.equal(toApiColor('#89b4fa'), '#ff89b4fa');
  assert.equal(toHtmlColor('#ff89b4fa'), '#89b4fa');
  assert.equal(toHtmlColor('#89b4fa'), '#89b4fa');
  assert.equal(toHtmlColor('invalid'), '#f38ba8');
});
