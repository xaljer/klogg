import assert from 'node:assert/strict';
import test from 'node:test';

import {
  chipsToExpression,
  chipsToText,
  parseChipText,
  type ChipSpec,
} from '../src/lib/search-chips.ts';

function chip(type: ChipSpec['type'], ...terms: string[]): ChipSpec {
  return { type, terms };
}

test('compact chip text splits into alternatives, conjunctions and negations', () => {
  assert.deepEqual(parseChipText('ERROR|WARN'), [chip('or', 'ERROR'), chip('or', 'WARN')]);
  assert.deepEqual(parseChipText('disk&full'), [chip('and', 'disk', 'full')]);
  assert.deepEqual(parseChipText('!trace'), [chip('not', 'trace')]);
  assert.deepEqual(parseChipText('not trace'), [chip('not', 'trace')]);
  assert.deepEqual(parseChipText('!disk&full'), [chip('notAnd', 'disk', 'full')]);
  assert.deepEqual(parseChipText('a|!b'), [chip('or', 'a'), chip('not', 'b')]);
  assert.deepEqual(parseChipText(''), []);
});

test('quoted terms keep separators and spaces literal', () => {
  assert.deepEqual(parseChipText('"a|b"|c'), [chip('or', 'a|b'), chip('or', 'c')]);
  assert.deepEqual(parseChipText('"disk full"'), [chip('or', 'disk full')]);
});

test('the engine expression keeps quoting and precedence', () => {
  assert.equal(chipsToExpression([chip('or', 'ERROR'), chip('or', 'WARN')]), '("ERROR" or "WARN")');
  assert.equal(chipsToExpression([chip('and', 'disk', 'full')]), '("disk" and "full")');
  assert.equal(chipsToExpression([chip('not', 'trace')]), 'not("trace")');
  assert.equal(
    chipsToExpression([chip('or', 'ERROR'), chip('not', 'debug')]),
    '"ERROR" and not("debug")',
  );
});

test('history stores the compact form, not the boolean expression', () => {
  assert.equal(chipsToText([chip('or', 'ERROR'), chip('or', 'WARN')]), 'ERROR|WARN');
  assert.equal(chipsToText([chip('and', 'disk', 'full')]), 'disk&full');
  assert.equal(chipsToText([chip('not', 'trace')]), '!trace');
  assert.equal(chipsToText([chip('notAnd', 'disk', 'full')]), '!disk&full');
  assert.equal(chipsToText([chip('or', 'ERROR'), chip('not', 'debug')]), 'ERROR|!debug');
});

test('compact text round-trips through the chip parser', () => {
  const cases: ChipSpec[][] = [
    [chip('or', 'ERROR'), chip('or', 'WARN')],
    [chip('and', 'disk', 'full')],
    [chip('not', 'trace')],
    [chip('notAnd', 'disk', 'full')],
    [chip('or', 'ERROR'), chip('not', 'debug')],
    [chip('or', 'disk full'), chip('or', 'timeout')],
  ];
  for (const chips of cases) {
    const text = chipsToText(chips);
    assert.deepEqual(parseChipText(text), chips, text);
  }
});

test('terms that the parser would split are quoted in history', () => {
  assert.equal(chipsToText([chip('or', 'a|b')]), '"a|b"');
  assert.equal(chipsToText([chip('or', 'disk full')]), '"disk full"');
  assert.equal(chipsToText([chip('or', '!important')]), '"!important"');
  assert.equal(chipsToText([chip('or', 'notable')]), 'notable');
});
