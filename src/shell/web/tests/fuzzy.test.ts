import assert from 'node:assert/strict';
import test from 'node:test';

import { fuzzyFilter, fuzzyScore } from '../src/lib/fuzzy.ts';

test('fuzzyScore prefers substring matches', () => {
  assert.ok(fuzzyScore('search-history-term-15', 'history') > 0);
  assert.ok(
    fuzzyScore('search-history-term-15', 'history') > fuzzyScore('search-history-term-15', 'h15'),
  );
});

test('fuzzyScore matches characters in order', () => {
  assert.ok(fuzzyScore('search-history-term-15', 'h15') > 0);
  assert.equal(fuzzyScore('search-history-term-15', 'zzz'), 0);
});

test('fuzzyFilter preserves input order for an empty query', () => {
  const items = ['b', 'a', 'c'];
  assert.deepEqual(
    fuzzyFilter(items, '  ', (item) => item),
    items,
  );
});

test('fuzzyFilter ranks matches and keeps stable ties', () => {
  const items = ['open-tabs', 'tabs-open', 'unrelated'];
  assert.deepEqual(
    fuzzyFilter(items, 'tabs', (item) => item),
    ['tabs-open', 'open-tabs'],
  );
});

test('fuzzyFilter supports a custom searchable key', () => {
  const tabs = [
    { title: 'alpha.log', path: 'D:/logs/alpha.log' },
    { title: 'beta.log', path: 'D:/logs/beta.log' },
  ];
  assert.deepEqual(
    fuzzyFilter(tabs, 'bt', (tab) => `${tab.title} ${tab.path}`),
    [tabs[1]],
  );
});
