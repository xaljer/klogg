import assert from 'node:assert/strict';
import test from 'node:test';

import {
  applyColorLabel,
  applyNextColorLabel,
  clearColorLabel,
  colorLabelKey,
  colorLabelOf,
  colorLabelRanges,
} from '../src/lib/color-labels.ts';
import type { QuickHighlighterDef } from '../src/lib/types.ts';

const defs: QuickHighlighterDef[] = [0, 1, 2].map((index) => ({
  name: `Color label ${index + 1}`,
  color: '#ffcdd6f4',
  backColor: '#ff89b4fa',
  useInCycle: true,
}));

test('labels are keyed by the selected text, not by a trimmed line', () => {
  assert.equal(colorLabelKey('worker=1\n'), 'worker=1');
  assert.equal(colorLabelKey('  spaced  '), '  spaced  ');
  const slots = applyColorLabel([[], [], []], 1, '  spaced  ');
  assert.equal(colorLabelOf(slots, '  spaced  '), 1);
  assert.equal(colorLabelOf(slots, 'spaced'), -1);
});

test('re-applying the label a text already has clears it', () => {
  const labelled = applyColorLabel([[], [], []], 0, 'alpha');
  assert.equal(colorLabelOf(labelled, 'alpha'), 0);
  const cleared = applyColorLabel(labelled, 0, 'alpha');
  assert.equal(colorLabelOf(cleared, 'alpha'), -1);
});

test('applying a different label moves the text instead of duplicating it', () => {
  const labelled = applyColorLabel([[], [], []], 0, 'alpha');
  const moved = applyColorLabel(labelled, 2, 'alpha');
  assert.deepEqual(moved[0], []);
  assert.deepEqual(moved[2], ['alpha']);
});

test('clear removes the text from every slot', () => {
  const slots = applyColorLabel(applyColorLabel([[], [], []], 0, 'alpha'), 2, 'beta');
  const cleared = clearColorLabel(slots, 'alpha');
  assert.deepEqual(cleared, [[], [], ['beta']]);
});

test('cycling labels only visits the slots marked for the cycle', () => {
  const cycleDefs = defs.map((def, index) => ({ ...def, useInCycle: index !== 1 }));
  const first = applyNextColorLabel([[], [], []], cycleDefs, -1, 'alpha');
  assert.equal(first.label, 0);
  assert.equal(colorLabelOf(first.slots, 'alpha'), 0);
  // The text is already labelled, so the next press removes it.
  const second = applyNextColorLabel(first.slots, cycleDefs, first.label, 'alpha');
  assert.equal(second.label, 2);
  assert.equal(colorLabelOf(second.slots, 'alpha'), -1);
  const third = applyNextColorLabel(second.slots, cycleDefs, second.label, 'alpha');
  // The cycle wraps back to its first slot.
  assert.equal(third.label, 0);
  assert.equal(colorLabelOf(third.slots, 'alpha'), 0);
});

test('ranges are plain text, case sensitive and independent per line', () => {
  const slots = [['alpha'], ['Beta']];
  assert.deepEqual(colorLabelRanges(slots, 'alpha Beta beta', 2), [
    { start: 0, end: 5, label: 0 },
    { start: 6, end: 10, label: 1 },
  ]);
  assert.deepEqual(colorLabelRanges(slots, 'ALPHA', 2), []);
});

test('label words beyond the configured slots are ignored', () => {
  assert.deepEqual(colorLabelRanges([['alpha'], ['beta']], 'alpha beta', 1), [
    { start: 0, end: 5, label: 0 },
  ]);
});
