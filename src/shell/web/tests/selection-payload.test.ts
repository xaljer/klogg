import assert from 'node:assert/strict';
import test from 'node:test';

import {
  selectionCopyLinesText,
  selectionEndLine,
  selectionHasSelection,
  selectionIsMultiRow,
  selectionLines,
  selectionPortionText,
  selectionRowCount,
  selectionStartLine,
  selectionText,
  type SelectionPayload,
} from '../src/lib/selection/payload.ts';

function payload(overrides: Partial<SelectionPayload> = {}): SelectionPayload {
  return {
    focusLine: 7,
    focusText: 'focus row',
    range: null,
    rows: [],
    portion: null,
    ...overrides,
  };
}

test('nothing selected falls back to the pointed-at line', () => {
  const empty = payload();
  assert.equal(selectionHasSelection(empty), false);
  assert.equal(selectionRowCount(empty), 0);
  assert.equal(selectionIsMultiRow(empty), false);
  assert.equal(selectionStartLine(empty), 7);
  assert.equal(selectionEndLine(empty), 7);
  assert.deepEqual(selectionLines(empty), [7]);
  assert.equal(selectionText(empty), 'focus row');
  assert.equal(selectionCopyLinesText(empty), '8: focus row');
  assert.equal(selectionPortionText(empty), '');
});

test('a single selected row is not a multi-row selection', () => {
  const single = payload({ range: { start: 3, end: 3 }, rows: [{ line: 3, text: 'line three' }] });
  assert.equal(selectionHasSelection(single), true);
  assert.equal(selectionRowCount(single), 1);
  assert.equal(selectionIsMultiRow(single), false);
  assert.equal(selectionStartLine(single), 3);
  assert.equal(selectionText(single), 'line three');
  assert.equal(selectionCopyLinesText(single), '4: line three');
});

test('a row range keeps file order for lines and text', () => {
  const range = payload({
    range: { start: 2, end: 3 },
    rows: [
      { line: 2, text: 'two' },
      { line: 3, text: 'three' },
    ],
  });
  assert.equal(selectionHasSelection(range), true);
  assert.equal(selectionRowCount(range), 2);
  assert.equal(selectionIsMultiRow(range), true);
  assert.deepEqual(selectionLines(range), [2, 3]);
  assert.equal(selectionStartLine(range), 2);
  assert.equal(selectionEndLine(range), 3);
  assert.equal(selectionText(range), 'two\nthree');
  assert.equal(selectionCopyLinesText(range), '3: two\n4: three');
});

test('a range that outlived its visible rows still reports its line bounds', () => {
  const scrolled = payload({ range: { start: 100, end: 500 } });
  assert.equal(selectionHasSelection(scrolled), true);
  assert.equal(selectionIsMultiRow(scrolled), true);
  assert.equal(selectionStartLine(scrolled), 100);
  assert.equal(selectionEndLine(scrolled), 500);
  assert.equal(selectionRowCount(scrolled), 401);
  // No loaded row means no text to copy; the pointed-at line is the fallback.
  assert.equal(selectionText(scrolled), 'focus row');
  assert.deepEqual(selectionLines(scrolled), [7]);
});

test('a portion selection is copied on its own and drives search actions', () => {
  const portion = payload({
    range: { start: 5, end: 5 },
    rows: [{ line: 5, text: '2026-10-25 INFO alpha' }],
    portion: { line: 5, text: 'INFO' },
  });
  assert.equal(selectionText(portion), 'INFO');
  assert.equal(selectionCopyLinesText(portion), '6: INFO');
  assert.equal(selectionPortionText(portion), 'INFO');
  // The row-based actions still apply to the whole row the portion sits in.
  assert.deepEqual(selectionLines(portion), [5]);
});

test('a portion of a multi-row selection wins for text, the range for line actions', () => {
  const mixed = payload({
    range: { start: 10, end: 11 },
    rows: [
      { line: 10, text: 'ten' },
      { line: 11, text: 'eleven' },
    ],
    portion: { line: 11, text: 'eve' },
  });
  assert.equal(selectionText(mixed), 'eve');
  assert.equal(selectionCopyLinesText(mixed), '12: eve');
  assert.deepEqual(selectionLines(mixed), [10, 11]);
  assert.equal(selectionStartLine(mixed), 10);
  assert.equal(selectionEndLine(mixed), 11);
});
