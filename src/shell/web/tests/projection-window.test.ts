import assert from 'node:assert/strict';
import test from 'node:test';

import {
  centeredWindowStart,
  sortedIndexAtOrBefore,
  windowContains,
} from '../src/lib/selection/projection-window.ts';

test('a window centers the requested index when it can', () => {
  // 600 rows, 200 per page: index 300 opens at 200.
  assert.equal(centeredWindowStart(300, 600, 200), 200);
  // Near the top the window cannot start below zero.
  assert.equal(centeredWindowStart(0, 600, 200), 0);
  assert.equal(centeredWindowStart(99, 600, 200), 0);
  // Near the end it stops on the last full page, otherwise it centers.
  assert.equal(centeredWindowStart(599, 600, 200), 400);
  assert.equal(centeredWindowStart(450, 600, 200), 350);
});

test('a projection smaller than a page opens at the top', () => {
  assert.equal(centeredWindowStart(5, 12, 200), 0);
  assert.equal(centeredWindowStart(0, 12, 200), 0);
  assert.equal(centeredWindowStart(3, 0, 200), 0);
});

test('the requested index is always inside the window it opened', () => {
  for (const [index, total, page] of [
    [0, 600, 200],
    [150, 600, 200],
    [300, 600, 200],
    [599, 600, 200],
    [7, 12, 200],
  ]) {
    const from = centeredWindowStart(index, total, page);
    const count = Math.min(page, total - from);
    assert.ok(windowContains(from, count, index), `index ${index} of ${total}`);
  }
});

test('windowContains checks the half-open window', () => {
  assert.equal(windowContains(200, 200, 200), true);
  assert.equal(windowContains(200, 200, 399), true);
  assert.equal(windowContains(200, 200, 400), false);
  assert.equal(windowContains(200, 200, 199), false);
});

test('sortedIndexAtOrBefore floors onto the mark list', () => {
  const marks = [0, 5, 9, 20];
  assert.equal(sortedIndexAtOrBefore(marks, 0), 0);
  assert.equal(sortedIndexAtOrBefore(marks, 4), 0);
  assert.equal(sortedIndexAtOrBefore(marks, 5), 1);
  assert.equal(sortedIndexAtOrBefore(marks, 19), 2);
  assert.equal(sortedIndexAtOrBefore(marks, 1000), 3);
  assert.equal(sortedIndexAtOrBefore([], 3), null);
  assert.equal(sortedIndexAtOrBefore([5, 9], 4), null);
});
