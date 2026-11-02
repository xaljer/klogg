import assert from 'node:assert/strict';
import test from 'node:test';

import { resolveSearchMatchColors } from '../src/lib/search-colors.ts';
import { themeList, themes } from '../src/lib/theme.ts';

test('without overrides both panes take the theme match colours', () => {
  for (const theme of themeList) {
    const colors = resolveSearchMatchColors(theme, {
      custom: false,
      background: '#000000',
      foreground: '#ffffff',
    });
    assert.equal(colors.apiBack, theme.palette.matchBack, `${theme.id} foreground`);
    assert.equal(colors.apiFore, theme.palette.matchFore, `${theme.id} background`);
    assert.equal(colors.cssBack, `#${theme.palette.matchBack.slice(3)}`, `${theme.id} css back`);
    assert.equal(colors.cssFore, `#${theme.palette.matchFore.slice(3)}`, `${theme.id} css fore`);
  }
});

test('custom colours override the palette for both panes', () => {
  const colors = resolveSearchMatchColors(themes.mocha, {
    custom: true,
    background: '#ff0000',
    foreground: '#00ff00',
  });
  assert.equal(colors.apiBack, '#ffff0000');
  assert.equal(colors.apiFore, '#ff00ff00');
  assert.equal(colors.cssBack, '#ff0000');
  assert.equal(colors.cssFore, '#00ff00');
});
