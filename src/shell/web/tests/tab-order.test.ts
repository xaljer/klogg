import assert from 'node:assert/strict';
import test from 'node:test';

import { reorderTabList, type TabOrderEntry } from '../src/lib/tab-order.ts';

const tabs: TabOrderEntry[] = [
  { id: 'a' },
  { id: 'b' },
  { id: 'c' },
  { id: 'd' },
];

const order = (list: TabOrderEntry[]): string => list.map((tab) => tab.id).join('');

test('a tab dropped before another one moves in front of it', () => {
  assert.equal(order(reorderTabList(tabs, { id: 'd', targetId: 'b', after: false })), 'adbc');
  assert.equal(order(reorderTabList(tabs, { id: 'b', targetId: 'a', after: false })), 'bacd');
});

test('a tab dropped after another one moves behind it', () => {
  assert.equal(order(reorderTabList(tabs, { id: 'a', targetId: 'c', after: true })), 'bcad');
  assert.equal(order(reorderTabList(tabs, { id: 'c', targetId: 'd', after: true })), 'abdc');
});

test('the input list is kept when the move changes nothing', () => {
  assert.equal(reorderTabList(tabs, { id: 'a', targetId: 'a', after: false }), tabs);
  assert.equal(reorderTabList(tabs, { id: 'missing', targetId: 'a', after: false }), tabs);
  assert.equal(reorderTabList(tabs, { id: 'a', targetId: 'missing', after: false }), tabs);
  assert.equal(reorderTabList(tabs, { id: 'a', targetId: 'b', after: false }), tabs);
  assert.equal(reorderTabList(tabs, { id: 'b', targetId: 'a', after: true }), tabs);
});

test('pinned and unpinned tabs do not mix', () => {
  const mixed: TabOrderEntry[] = [
    { id: 'p1', pinned: true },
    { id: 'p2', pinned: true },
    { id: 'u1' },
    { id: 'u2' },
  ];
  // Cross group moves are ignored, the tab keeps its pinned state.
  assert.equal(reorderTabList(mixed, { id: 'u1', targetId: 'p1', after: false }), mixed);
  assert.equal(reorderTabList(mixed, { id: 'p2', targetId: 'u1', after: true }), mixed);
  // Inside a group the order changes as usual.
  assert.equal(
    order(reorderTabList(mixed, { id: 'p2', targetId: 'p1', after: false })),
    'p2p1u1u2',
  );
  assert.equal(
    order(reorderTabList(mixed, { id: 'u2', targetId: 'u1', after: false })),
    'p1p2u2u1',
  );
});
