import assert from 'node:assert/strict';
import test from 'node:test';

import {
  indexAtScrollTop,
  pixelPerRowFor,
  scrollTopForIndex,
  virtualHeightFor,
} from '../src/lib/virtual-scroll.ts';

const MAX = 8_000_000;
const ROW = 20;

test('virtual height is exact for small projections', () => {
  assert.equal(virtualHeightFor(1000, ROW, MAX), 20_000);
  assert.equal(pixelPerRowFor(1000, 20_000, ROW), ROW);
});

test('virtual height is capped for million-match projections', () => {
  const total = 10_000_000;
  const height = virtualHeightFor(total, ROW, MAX);
  assert.equal(height, MAX);
  assert.equal(pixelPerRowFor(total, height, ROW), 0.8);
});

test('scrollbar middle maps to the middle result when the window is at the top', () => {
  const state = {
    total: 10_000_000,
    rowHeight: ROW,
    maxHeight: MAX,
    loadedFrom: 0,
    loadedCount: 200,
  };
  assert.equal(indexAtScrollTop(state, 4_000_000), 5_000_000);
});

test('scroll inside the loaded window uses the real row height', () => {
  const state = {
    total: 10_000_000,
    rowHeight: ROW,
    maxHeight: MAX,
    loadedFrom: 5_000_000,
    loadedCount: 200,
  };
  const topSpacer = 4_000_000;
  assert.equal(indexAtScrollTop(state, topSpacer), 5_000_000);
  assert.equal(indexAtScrollTop(state, topSpacer + 19 * ROW), 5_000_019);
});

test('the last page maps back to the end of the result', () => {
  const state = {
    total: 10_000_000,
    rowHeight: ROW,
    maxHeight: MAX,
    loadedFrom: 9_999_800,
    loadedCount: 200,
  };
  const topSpacer = Math.round((9_999_800 / 10_000_000) * MAX);
  assert.equal(indexAtScrollTop(state, topSpacer + 199 * ROW), 9_999_999);
  assert.equal(scrollTopForIndex(state, 9_999_999), topSpacer + 199 * ROW);
});

test('global index to pixel never exceeds the capped height', () => {
  const state = {
    total: 10_000_000,
    rowHeight: ROW,
    maxHeight: MAX,
    loadedFrom: 0,
    loadedCount: 200,
  };
  assert.ok(scrollTopForIndex(state, 9_999_999) <= MAX);
});
