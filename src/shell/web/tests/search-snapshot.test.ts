import assert from 'node:assert/strict';
import test from 'node:test';

import { parseChipText } from '../src/lib/search-chips.ts';
import {
  createSearchSnapshot,
  snapshotHighlight,
  snapshotSearchParams,
} from '../src/lib/search-snapshot.ts';

const toExpression = (chips: { type: string; terms: string[] }[]): string =>
  chips.map((chip) => `${chip.type}:${chip.terms.join('&')}`).join('|');

const escape = (text: string): string => text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');

function inputs(overrides: Record<string, unknown> = {}) {
  return {
    chipMode: false,
    chips: [],
    pattern: '  INFO  ',
    regex: true,
    caseSensitive: false,
    invert: false,
    fromLine: null,
    toLine: null,
    maxResults: null,
    ...overrides,
  } as Parameters<typeof createSearchSnapshot>[0];
}

test('a text search snapshots the trimmed pattern and its options', () => {
  const snapshot = createSearchSnapshot(
    inputs({ regex: false, caseSensitive: true, invert: true, fromLine: 3, toLine: 9 }),
    toExpression,
  );
  assert.equal(snapshot.expression, 'INFO');
  assert.equal(snapshot.chipMode, false);
  assert.equal(snapshot.regex, false);
  assert.equal(snapshot.caseSensitive, true);
  assert.equal(snapshot.invert, true);
  assert.equal(snapshot.fromLine, 3);
  assert.deepEqual(snapshotSearchParams(snapshot), {
    pattern: 'INFO',
    caseSensitive: true,
    isRegex: false,
    isBoolean: false,
    invertMatch: true,
    fromLine: 3,
    // The engine end line is exclusive.
    toLine: 10,
    maxResults: null,
  });
});

test('a chip search snapshots a copy of the conditions', () => {
  const chips = parseChipText('ERROR|!debug', false);
  const snapshot = createSearchSnapshot(inputs({ chipMode: true, chips }), toExpression);
  assert.equal(snapshot.expression, toExpression(chips));
  assert.notEqual(snapshot.chips, chips);
  chips[0].terms.push('mutated');
  assert.ok(!snapshot.chips[0].terms.includes('mutated'));
  const params = snapshotSearchParams(snapshot);
  assert.equal(params.isBoolean, true);
  // Chip terms are regexes while the toggle is on; forcing plain text here used
  // to strip every regex metacharacter from the search.
  assert.equal(params.isRegex, true);
  assert.equal(params.invertMatch, false);

  const plainParams = snapshotSearchParams(
    createSearchSnapshot(inputs({ chipMode: true, chips, regex: false }), toExpression),
  );
  assert.equal(plainParams.isBoolean, true);
  assert.equal(plainParams.isRegex, false);
});

test('the highlight of a chip search follows the regex toggle', () => {
  const chips = parseChipText('a.b|!c*d', false);
  const asRegex = snapshotHighlight(
    createSearchSnapshot(
      inputs({ chipMode: true, chips, caseSensitive: true }),
      toExpression,
    ),
    escape,
  );
  assert.equal(asRegex.pattern, 'a.b|c*d');
  assert.equal(asRegex.regex, true);
  assert.equal(asRegex.caseSensitive, true);

  const asPlainText = snapshotHighlight(
    createSearchSnapshot(
      inputs({ chipMode: true, chips, caseSensitive: true, regex: false }),
      toExpression,
    ),
    escape,
  );
  assert.equal(asPlainText.pattern, 'a\\.b|c\\*d');
  assert.equal(asPlainText.regex, true);
});

test('the highlight of a text search keeps its own pattern', () => {
  const snapshot = createSearchSnapshot(inputs({ pattern: 'w.*r', regex: true }), toExpression);
  const highlight = snapshotHighlight(snapshot, escape);
  assert.equal(highlight.pattern, 'w.*r');
  assert.equal(highlight.regex, true);
});

test('an empty pattern produces an empty expression', () => {
  const snapshot = createSearchSnapshot(inputs({ pattern: '   ' }), toExpression);
  assert.equal(snapshot.expression, '');
});
