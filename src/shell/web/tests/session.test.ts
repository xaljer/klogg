import assert from 'node:assert/strict';
import { beforeEach, test } from 'node:test';

import {
  loadSession,
  saveSession,
  sessionStorageKey,
} from '../src/lib/session.ts';
import { installMemoryStorage } from './support/local-storage.ts';

const memory = installMemoryStorage();

beforeEach(() => {
  memory.clear();
});

test('session keys are scoped per window', () => {
  assert.equal(sessionStorageKey(), 'klogg.web.session');
  assert.equal(sessionStorageKey('w1'), 'klogg.web.session.w1');
});

test('window sessions do not overwrite each other', () => {
  saveSession({ tabs: [{ path: 'a.log' }], activeIndex: 0 }, 'w1');
  saveSession({ tabs: [{ path: 'b.log' }], activeIndex: 0 }, 'w2');

  assert.deepEqual(
    loadSession('w1')?.tabs.map((tab) => tab.path),
    ['a.log'],
  );
  assert.deepEqual(
    loadSession('w2')?.tabs.map((tab) => tab.path),
    ['b.log'],
  );
});

test('empty window sessions remove only their own key', () => {
  saveSession({ tabs: [{ path: 'a.log' }], activeIndex: 0 }, 'w1');
  saveSession({ tabs: [{ path: 'b.log' }], activeIndex: 0 }, 'w2');
  saveSession({ tabs: [], activeIndex: 0 }, 'w1');

  assert.equal(loadSession('w1'), null);
  assert.deepEqual(
    loadSession('w2')?.tabs.map((tab) => tab.path),
    ['b.log'],
  );
});

test('pinned tab state round-trips and defaults to false', () => {
  saveSession(
    { tabs: [{ path: 'a.log', pinned: true }, { path: 'b.log' }], activeIndex: 0 },
    'w1',
  );

  const session = loadSession('w1');
  assert.equal(session?.tabs[0].pinned, true);
  assert.equal(session?.tabs[1].pinned, false);
});
