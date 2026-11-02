// Node runs the unit tests on the sources directly, so runtime imports need the
// explicit extension (see allowImportingTsExtensions in tsconfig.json).
import { toApiColor, toHtmlColor } from './highlighters.ts';
import type { ThemeDefinition } from './theme';

export interface SearchMatchColorSettings {
  custom: boolean;
  background: string;
  foreground: string;
}

export interface SearchMatchColors {
  /** QColor style colours for `render.viewport`. */
  apiFore: string;
  apiBack: string;
  /** `#rrggbb` colours for the filtered view CSS. */
  cssFore: string;
  cssBack: string;
}

/**
 * One colour pair per search, shared by the main viewport (rendered server
 * side) and the filtered view (rendered in the browser), so a match looks the
 * same in both panes. Without overrides the pair comes from the theme palette.
 */
export function resolveSearchMatchColors(
  theme: ThemeDefinition,
  settings: SearchMatchColorSettings,
): SearchMatchColors {
  const cssBack = settings.custom ? settings.background : toHtmlColor(theme.palette.matchBack);
  const cssFore = settings.custom ? settings.foreground : toHtmlColor(theme.palette.matchFore);
  return {
    apiBack: toApiColor(cssBack),
    apiFore: toApiColor(cssFore),
    cssBack,
    cssFore,
  };
}
