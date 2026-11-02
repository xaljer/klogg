import assert from 'node:assert/strict';
import test from 'node:test';

import { isThemeId, nextTheme, resolveTheme, themeList, themes } from '../src/lib/theme.ts';

test('theme resolution returns the requested desktop palette', () => {
  assert.equal(resolveTheme('mocha').id, 'mocha');
  assert.equal(resolveTheme('latte').id, 'latte');
  assert.equal(resolveTheme('nord').id, 'nord');
  assert.equal(resolveTheme('nord-light').id, 'nord-light');
  assert.equal(resolveTheme('spring').id, 'spring');
  assert.equal(themes.mocha.palette.base, '#ff1e1e2e');
  assert.equal(themes.latte.palette.base, '#ffeff1f5');
  assert.equal(themes.nord.palette.base, '#ff2e3440');
  assert.equal(themes['nord-light'].palette.base, '#ffeceff4');
  assert.equal(themes.spring.palette.base, '#ffeef7f2');
});

test('system falls back to a Mocha/Latte pair without a browser preference', () => {
  assert.equal(resolveTheme('system').id, 'mocha');
});

test('theme palettes expose selection, match and mark colors', () => {
  for (const theme of themeList) {
    assert.match(theme.palette.selectionBack, /^#[0-9a-f]{8}$/i);
    assert.match(theme.palette.matchBack, /^#[0-9a-f]{8}$/i);
    assert.match(theme.palette.mark, /^#[0-9a-f]{8}$/i);
  }
});

test('every theme defines the same CSS token set', () => {
  const expected = Object.keys(themes.mocha.css).sort();
  for (const theme of themeList) {
    assert.deepEqual(Object.keys(theme.css).sort(), expected, `${theme.id} tokens`);
    assert.equal(theme.css['--bg'], theme.xterm.background, `${theme.id} log background`);
    assert.equal(theme.palette.base, `#ff${theme.css['--bg'].slice(1)}`, `${theme.id} base`);
  }
});

test('theme registry exposes each theme once', () => {
  assert.equal(themeList.length, 5);
  assert.equal(new Set(themeList.map((theme) => theme.id)).size, themeList.length);
  assert.equal(new Set(themeList.map((theme) => theme.labelKey)).size, themeList.length);
  for (const theme of themeList) {
    assert.equal(themes[theme.id], theme, `${theme.id} registered`);
  }
});

test('nextTheme cycles through the registry', () => {
  assert.equal(nextTheme('mocha'), 'latte');
  assert.equal(nextTheme('latte'), 'nord');
  assert.equal(nextTheme('nord'), 'nord-light');
  assert.equal(nextTheme('nord-light'), 'spring');
  assert.equal(nextTheme('spring'), 'mocha');
});

test('settings accept registered theme ids only', () => {
  assert.equal(isThemeId('system'), true);
  for (const theme of themeList) {
    assert.equal(isThemeId(theme.id), true);
  }
  assert.equal(isThemeId('unknown'), false);
  assert.equal(isThemeId('toString'), false);
  assert.equal(isThemeId(null), false);
});

function luminance(hex: string): number {
  const channels = [1, 3, 5].map((offset) => {
    const value = Number.parseInt(hex.slice(offset, offset + 2), 16) / 255;
    return value <= 0.03928 ? value / 12.92 : ((value + 0.055) / 1.055) ** 2.4;
  });
  return 0.2126 * channels[0] + 0.7152 * channels[1] + 0.0722 * channels[2];
}

function contrast(foreground: string, background: string): number {
  const [lighter, darker] = [luminance(foreground), luminance(background)].sort((a, b) => b - a);
  return (lighter + 0.05) / (darker + 0.05);
}

// Token pairs that carry text; muted hints and accent-tinted secondary labels
// are allowed to sit lower because they are deliberately de-emphasised.
const READABLE_PAIRS: [string, string, number][] = [
  ['--text', '--bg', 7],
  ['--text', '--surface', 6],
  ['--sub', '--bg', 5],
  ['--muted', '--bg', 3.3],
  ['--accent-text', '--accent-bg', 4.5],
  ['--accent-sub', '--accent-bg', 3.5],
  ['--yellow-ink', '--bg', 4.5],
  ['--mark', '--bg', 4.5],
  ['--red', '--bg', 4.5],
  ['--green', '--bg', 4.5],
  ['--on-highlight', '--yellow', 4.5],
  ['--error-text', '--error-bg', 4.5],
];

test('theme colour pairs stay readable', () => {
  for (const theme of themeList) {
    for (const [foreground, background, minimum] of READABLE_PAIRS) {
      const ratio = contrast(theme.css[foreground], theme.css[background]);
      assert.ok(
        ratio >= minimum,
        `${theme.id}: ${foreground} on ${background} is ${ratio.toFixed(2)}, expected >= ${minimum}`,
      );
    }
  }
});
