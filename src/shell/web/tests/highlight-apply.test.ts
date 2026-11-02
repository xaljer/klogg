import assert from 'node:assert/strict';
import test from 'node:test';

import {
  applyHighlightRules,
  compileRules,
  crc32,
  darker,
  varianceFactor,
} from '../src/lib/highlight-apply.ts';
import { createHighlightRule } from '../src/lib/highlighters.ts';
import type { HighlightRule } from '../src/lib/types.ts';

function rule(overrides: Partial<HighlightRule>): HighlightRule {
  return createHighlightRule({ pattern: 'x', caseSensitive: true, ...overrides });
}

function spansOf(text: string, rules: HighlightRule[]) {
  return applyHighlightRules(text, compileRules(rules)).map((span) => ({
    text: text.slice(span.start, span.end),
    fore: span.fore,
    back: span.back,
  }));
}

test('crc32 matches the reference vectors of the desktop implementation', () => {
  assert.equal(crc32(''), 0);
  assert.equal(crc32('123456789'), 0xcbf43926);
});

test('darker scales towards black like QColor::darker', () => {
  assert.equal(darker('#808080', 100), '#808080');
  assert.equal(darker('#808080', 200), '#404040');
  assert.equal(darker('#ffffff', 50), '#ffffff', 'lighter is capped at 255');
});

test('varianceFactor is deterministic and inside the requested range', () => {
  const first = varianceFactor('ERROR', 15);
  assert.equal(first, varianceFactor('ERROR', 15));
  assert.notEqual(first, varianceFactor('WARNING', 15));
  for (const match of ['a', 'bb', 'ccc', 'dddd', 'eeeee']) {
    const factor = varianceFactor(match, 20);
    assert.ok(factor >= 80 && factor <= 120, `factor ${factor} out of range`);
  }
});

test('a whole-line rule colours every character', () => {
  assert.deepEqual(spansOf('an error happened', [rule({ pattern: 'error', color: '#ffff0000' })]), [
    { text: 'an error happened', fore: '#ff0000', back: '' },
  ]);
});

test('onlyMatch colours just the matched text', () => {
  assert.deepEqual(
    spansOf('an error happened', [
      rule({ pattern: 'error', color: '#ffff0000', onlyMatch: true }),
    ]),
    [{ text: 'error', fore: '#ff0000', back: '' }],
  );
});

test('the earlier rule wins for whole-line colouring', () => {
  assert.deepEqual(
    spansOf('warn and error', [
      rule({ pattern: 'warn', color: '#ff00ff00' }),
      rule({ pattern: 'error', color: '#ffff0000' }),
    ]),
    [{ text: 'warn and error', fore: '#00ff00', back: '' }],
  );
});

test('an onlyMatch rule above a line rule keeps its own span', () => {
  const spans = spansOf('warn and error', [
    rule({ pattern: 'warn', color: '#ff00ff00', onlyMatch: true }),
    rule({ pattern: 'error', color: '#ffff0000' }),
  ]);
  assert.deepEqual(spans, [
    { text: 'warn', fore: '#00ff00', back: '' },
    { text: ' and error', fore: '#ff0000', back: '' },
  ]);
});

test('disabled, empty and invalid rules are ignored', () => {
  assert.deepEqual(
    spansOf('error', [
      rule({ pattern: 'error', enabled: false }),
      rule({ pattern: '', color: '#ffff0000' }),
      rule({ pattern: '(', color: '#ff0000ff' }),
    ]),
    [],
  );
});

test('plain text rules escape their pattern', () => {
  assert.deepEqual(spansOf('a.b', [rule({ pattern: 'a.b', useRegex: false, color: '#ff00ff00' })]), [
    { text: 'a.b', fore: '#00ff00', back: '' },
  ]);
  assert.deepEqual(spansOf('axb', [rule({ pattern: 'a.b', useRegex: false })]), []);
});

test('variate colours shift the matched colour deterministically', () => {
  const rules = [
    rule({
      pattern: 'ERROR',
      color: '#ff4040ff',
      onlyMatch: true,
      variateColors: true,
      colorVariance: 30,
    }),
  ];
  const first = spansOf('ERROR ERROR', rules);
  assert.equal(first.length, 2);
  assert.deepEqual(first[0], first[1], 'the same text keeps the same colour');
  assert.notEqual(first[0].fore, '#4040ff', 'variation changed the colour');
});
