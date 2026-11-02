import assert from 'node:assert/strict';
import test from 'node:test';

import { normalizeHexColor } from '../src/lib/color.ts';

test('typed hex colours normalize to the #rrggbb form', () => {
  assert.equal(normalizeHexColor('#FF0000'), '#ff0000');
  assert.equal(normalizeHexColor('ff0000'), '#ff0000');
  assert.equal(normalizeHexColor('  #AbC  '), '#aabbcc');
  assert.equal(normalizeHexColor('#0f0'), '#00ff00');
});

test('QColor style ARGB loses its alpha, which the picker cannot show', () => {
  assert.equal(normalizeHexColor('#ff00ff00'), '#00ff00');
  assert.equal(normalizeHexColor('#80ffcc00'), '#ffcc00');
});

test('incomplete or invalid input is rejected', () => {
  assert.equal(normalizeHexColor(''), null);
  assert.equal(normalizeHexColor('#'), null);
  assert.equal(normalizeHexColor('#f'), null);
  assert.equal(normalizeHexColor('#ff00'), null);
  assert.equal(normalizeHexColor('#gg0000'), null);
  assert.equal(normalizeHexColor('red'), null);
});
