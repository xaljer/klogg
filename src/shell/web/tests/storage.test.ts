import assert from 'node:assert/strict';
import { beforeEach, test } from 'node:test';

import { lastPath, loadRecent, rememberRecent } from '../src/lib/storage.ts';
import { installMemoryStorage } from './support/local-storage.ts';

const memory = installMemoryStorage();

beforeEach(() => {
  memory.clear();
});

test('recent files keep most recent first and deduplicate', () => {
  rememberRecent('a.log');
  rememberRecent('b.log');
  assert.deepEqual(loadRecent(), ['b.log', 'a.log']);
  rememberRecent('a.log');
  assert.deepEqual(loadRecent(), ['a.log', 'b.log']);
  assert.equal(lastPath(), 'a.log');
});

test('recent files honour the limit and preserve legacy string arrays', () => {
  localStorage.setItem('klogg.web.recent', JSON.stringify(['first.log', 'second.log']));
  assert.deepEqual(loadRecent(), ['first.log', 'second.log']);
  rememberRecent('third.log', 2);
  assert.deepEqual(loadRecent(2), ['third.log', 'first.log']);
});

test('recent files sort timestamped entries newest first', () => {
  localStorage.setItem(
    'klogg.web.recent',
    JSON.stringify([
      { path: 'old.log', openedAt: 100 },
      { path: 'new.log', openedAt: 300 },
      { path: 'middle.log', openedAt: 200 },
    ]),
  );
  assert.deepEqual(loadRecent(), ['new.log', 'middle.log', 'old.log']);
});

test('recent files tolerate malformed storage', () => {
  localStorage.setItem('klogg.web.recent', '{broken');
  assert.deepEqual(loadRecent(), []);
  assert.equal(lastPath(), '');
});
