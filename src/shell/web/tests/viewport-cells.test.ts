import assert from 'node:assert/strict';
import test from 'node:test';

import {
  buildRows,
  splitByCells,
  textCells,
  trimToCells,
  utf16IndexAtCell,
} from '../src/lib/viewport-cells.ts';
import type { ViewportLine } from '../src/lib/types.ts';

test('textCells counts ASCII, CJK and non-BMP display cells', () => {
  assert.equal(textCells('abc'), 3);
  assert.equal(textCells('中a'), 3);
  assert.equal(textCells('😀'), 2);
});

test('splitByCells never splits a wide character', () => {
  assert.deepEqual(
    splitByCells('a中b', 2).map((part) => part.text),
    ['a', '中', 'b'],
  );
  assert.equal(trimToCells('a中b', 2), 'a');
});

test('utf16IndexAtCell maps display columns to UTF-16 offsets', () => {
  assert.equal(utf16IndexAtCell('abc', 2), 2);
  assert.equal(utf16IndexAtCell('中a', 1), 1);
  assert.equal(utf16IndexAtCell('😀a', 2), 2);
});

test('buildRows wraps by display cells and preserves source offsets', () => {
  const line: ViewportLine = {
    text: 'ab中cd',
    parts: [{ line: 0, start: 0, length: 5 }],
    chunks: [],
    lineNumber: 0,
    displayLineNumber: 1,
    flags: 0,
    foreColor: '#ff000000',
    backColor: '#ffffffff',
  };
  const rows = buildRows([line], 3, 0, 3);
  assert.deepEqual(
    rows.map((row) => row.text),
    ['ab', '中c', 'd'],
  );
  assert.deepEqual(
    rows.map((row) => row.visStart),
    [0, 2, 4],
  );
});
