import assert from 'node:assert/strict';
import test from 'node:test';

import { selectionWord } from '../src/lib/selection/selection-word.ts';

test('a within-row selection is trimmed into the highlight key', () => {
  assert.equal(selectionWord('INFO'), 'INFO');
  assert.equal(selectionWord('  1024 MB  '), '1024 MB');
  assert.equal(selectionWord('a b'), 'a b');
});

test('a blank or multi-row selection has no key', () => {
  assert.equal(selectionWord(''), '');
  assert.equal(selectionWord(' \t '), '');
  assert.equal(selectionWord('INFO\nalpha'), '');
  assert.equal(selectionWord('INFO\r\nalpha'), '');
});
