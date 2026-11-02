export interface Palette {
  text: string;
  base: string;
  dimmed: string;
  selectionText: string;
  selectionBack: string;
  matchFore: string;
  matchBack: string;
  matchLine: string;
  mark: string;
  event: string;
  currentLineBack: string;
}

export type ThemeName = 'mocha' | 'latte' | 'nord' | 'nord-light' | 'spring';
export type ThemeId = 'system' | ThemeName;

export interface ThemeDefinition {
  id: ThemeName;
  labelKey: string;
  dark: boolean;
  /** Colours consumed by the server renderer and the overview canvas. */
  palette: Palette;
  xterm: Record<string, string>;
  /** CSS custom properties; the only source of colour for app.css. */
  css: Record<string, string>;
}

// Palette values travel to the renderer as QColor-compatible #aarrggbb.
const argb = (rgb: string): string => `#ff${rgb.replace('#', '')}`;
const htmlColor = (argbColor: string): string => `#${argbColor.slice(3)}`;

const mocha: ThemeDefinition = {
  id: 'mocha',
  labelKey: 'settings.themeMocha',
  dark: true,
  palette: {
    text: argb('#cdd6f4'),
    base: argb('#1e1e2e'),
    dimmed: argb('#6c7086'),
    selectionText: argb('#1e1e2e'),
    selectionBack: argb('#89b4fa'),
    matchFore: argb('#1e1e2e'),
    matchBack: argb('#f9e2af'),
    matchLine: argb('#f38ba8'),
    mark: argb('#74c7ec'),
    event: argb('#f9e2af'),
    currentLineBack: argb('#2a2b3a'),
  },
  xterm: {
    background: '#1e1e2e',
    foreground: '#cdd6f4',
    cursor: '#cdd6f4',
    cursorAccent: '#1e1e2e',
    selectionBackground: '#585b70',
    black: '#45475a',
    red: '#f38ba8',
    green: '#a6e3a1',
    yellow: '#f9e2af',
    blue: '#89b4fa',
    magenta: '#cba6f7',
    cyan: '#94e2d5',
    white: '#bac2de',
    brightBlack: '#585b70',
    brightRed: '#f38ba8',
    brightGreen: '#a6e3a1',
    brightYellow: '#f9e2af',
    brightBlue: '#89b4fa',
    brightMagenta: '#cba6f7',
    brightCyan: '#94e2d5',
    brightWhite: '#a6adc8',
  },
  css: {
    '--bg': '#1e1e2e',
    '--surface': '#313244',
    '--surface-2': '#292938',
    '--overlay': '#45475a',
    '--text': '#cdd6f4',
    '--sub': '#a6adc8',
    '--muted': '#7f849c',
    '--faint': '#4a4d63',
    '--blue': '#89b4fa',
    '--mark': '#74c7ec',
    '--green': '#a6e3a1',
    '--red': '#f38ba8',
    '--yellow': '#f9e2af',
    '--yellow-ink': '#f9e2af',
    '--mauve': '#cba6f7',
    '--gutter-bg': '#181825',
    '--accent-bg': '#404e6f',
    '--accent-border': '#58709e',
    '--accent-hover': '#4b5883',
    '--accent-text': '#eef3ff',
    '--accent-sub': '#c3cdf0',
    '--on-highlight': '#1e1e2e',
    '--current-line-bg': 'rgba(249, 226, 175, 0.035)',
    '--current-line-gutter': 'rgba(249, 226, 175, 0.07)',
    '--error-bg': '#3a2028',
    '--error-border': '#6d2c3a',
    '--error-text': '#f5c2cf',
    '--backdrop': 'rgba(10, 10, 18, 0.62)',
    '--shadow-color': 'rgba(0, 0, 0, 0.45)',
  },
};

const latte: ThemeDefinition = {
  id: 'latte',
  labelKey: 'settings.themeLatte',
  dark: false,
  palette: {
    text: argb('#4c4f69'),
    base: argb('#eff1f5'),
    dimmed: argb('#9ca0b0'),
    selectionText: argb('#eff1f5'),
    selectionBack: argb('#1e66f5'),
    matchFore: argb('#eff1f5'),
    matchBack: argb('#df8e1d'),
    matchLine: argb('#d20f39'),
    mark: argb('#1259c7'),
    event: argb('#df8e1d'),
    currentLineBack: argb('#e6e9ef'),
  },
  xterm: {
    background: '#eff1f5',
    foreground: '#4c4f69',
    cursor: '#dc8a78',
    cursorAccent: '#eff1f5',
    selectionBackground: '#ccd0da',
    black: '#5c5f77',
    red: '#d20f39',
    green: '#40a02b',
    yellow: '#df8e1d',
    blue: '#1e66f5',
    magenta: '#8839ef',
    cyan: '#179299',
    white: '#acb0be',
    brightBlack: '#6c6f85',
    brightRed: '#d20f39',
    brightGreen: '#40a02b',
    brightYellow: '#df8e1d',
    brightBlue: '#1e66f5',
    brightMagenta: '#8839ef',
    brightCyan: '#179299',
    brightWhite: '#bcc0cc',
  },
  css: {
    '--bg': '#eff1f5',
    '--surface': '#e6e9ef',
    '--surface-2': '#dce0e8',
    '--overlay': '#bcc0cc',
    '--text': '#4c4f69',
    '--sub': '#5c5f77',
    '--muted': '#7c7f93',
    '--faint': '#b3b7c4',
    '--blue': '#1e66f5',
    '--mark': '#1259c7',
    '--green': '#2f7d1f',
    '--red': '#d20f39',
    '--yellow': '#df8e1d',
    '--yellow-ink': '#8f5300',
    '--mauve': '#8839ef',
    '--gutter-bg': '#e6e9ef',
    '--accent-bg': '#1e66f5',
    '--accent-border': '#1e66f5',
    '--accent-hover': '#0b57d0',
    '--accent-text': '#ffffff',
    '--accent-sub': '#dbe4ff',
    '--on-highlight': '#2f3244',
    '--current-line-bg': 'rgba(223, 142, 29, 0.08)',
    '--current-line-gutter': 'rgba(223, 142, 29, 0.12)',
    '--error-bg': '#f5d9de',
    '--error-border': '#d20f39',
    '--error-text': '#8a1c2b',
    '--backdrop': 'rgba(76, 79, 105, 0.28)',
    '--shadow-color': 'rgba(76, 79, 105, 0.18)',
  },
};

// Qt desktop "Nord" palette: polar-night chrome, frost accents.
const nord: ThemeDefinition = {
  id: 'nord',
  labelKey: 'settings.themeNord',
  dark: true,
  palette: {
    text: argb('#d8dee9'),
    base: argb('#2e3440'),
    dimmed: argb('#7b88a1'),
    selectionText: argb('#eceff4'),
    selectionBack: argb('#5e81ac'),
    matchFore: argb('#2e3440'),
    matchBack: argb('#ebcb8b'),
    matchLine: argb('#bf616a'),
    mark: argb('#88c0d0'),
    event: argb('#ebcb8b'),
    currentLineBack: argb('#3b4252'),
  },
  xterm: {
    background: '#2e3440',
    foreground: '#d8dee9',
    cursor: '#d8dee9',
    cursorAccent: '#2e3440',
    selectionBackground: '#4c566a',
    black: '#3b4252',
    red: '#bf616a',
    green: '#a3be8c',
    yellow: '#ebcb8b',
    blue: '#81a1c1',
    magenta: '#b48ead',
    cyan: '#88c0d0',
    white: '#e5e9f0',
    brightBlack: '#4c566a',
    brightRed: '#bf616a',
    brightGreen: '#a3be8c',
    brightYellow: '#ebcb8b',
    brightBlue: '#81a1c1',
    brightMagenta: '#b48ead',
    brightCyan: '#8fbcbb',
    brightWhite: '#eceff4',
  },
  css: {
    '--bg': '#2e3440',
    '--surface': '#3b4252',
    '--surface-2': '#343b48',
    '--overlay': '#4c566a',
    '--text': '#d8dee9',
    '--sub': '#b7c2d4',
    '--muted': '#8c99b0',
    '--faint': '#58637a',
    '--blue': '#81a1c1',
    '--mark': '#88c0d0',
    '--green': '#a3be8c',
    '--red': '#dc8790',
    '--yellow': '#ebcb8b',
    '--yellow-ink': '#ebcb8b',
    '--mauve': '#b48ead',
    '--gutter-bg': '#272c36',
    '--accent-bg': '#4b6b93',
    '--accent-border': '#6f92b8',
    '--accent-hover': '#5a7ba6',
    '--accent-text': '#eceff4',
    '--accent-sub': '#e6eef7',
    '--on-highlight': '#2e3440',
    '--current-line-bg': 'rgba(235, 203, 139, 0.05)',
    '--current-line-gutter': 'rgba(235, 203, 139, 0.09)',
    '--error-bg': '#3b2a30',
    '--error-border': '#8a4a55',
    '--error-text': '#eec9d0',
    '--backdrop': 'rgba(20, 24, 32, 0.62)',
    '--shadow-color': 'rgba(0, 0, 0, 0.45)',
  },
};

// Qt desktop "Nord Light" palette: snow-storm chrome over a frost log area.
const nordLight: ThemeDefinition = {
  id: 'nord-light',
  labelKey: 'settings.themeNordLight',
  dark: false,
  palette: {
    text: argb('#2e3440'),
    base: argb('#eceff4'),
    dimmed: argb('#6d7a92'),
    selectionText: argb('#eceff4'),
    selectionBack: argb('#45648c'),
    matchFore: argb('#22303a'),
    matchBack: argb('#8fbcbb'),
    matchLine: argb('#bf616a'),
    mark: argb('#21738f'),
    event: argb('#b8860b'),
    currentLineBack: argb('#e2e8f0'),
  },
  xterm: {
    background: '#eceff4',
    foreground: '#2e3440',
    cursor: '#45648c',
    cursorAccent: '#eceff4',
    selectionBackground: '#c2ccda',
    black: '#3b4252',
    red: '#b04a54',
    green: '#3f7233',
    yellow: '#8a6a1f',
    blue: '#4a6a94',
    magenta: '#8f6690',
    cyan: '#21738f',
    white: '#c2ccda',
    brightBlack: '#4c566a',
    brightRed: '#bf616a',
    brightGreen: '#4b7f3f',
    brightYellow: '#a8791f',
    brightBlue: '#5e81ac',
    brightMagenta: '#a06a98',
    brightCyan: '#2f7f99',
    brightWhite: '#d8dee9',
  },
  css: {
    '--bg': '#eceff4',
    '--surface': '#e5e9f0',
    '--surface-2': '#d8dee9',
    '--overlay': '#c2ccda',
    '--text': '#2e3440',
    '--sub': '#4c566a',
    '--muted': '#6d7a92',
    '--faint': '#b0bac9',
    '--blue': '#4a6a94',
    '--mark': '#21738f',
    '--green': '#3f7233',
    '--red': '#b04a54',
    '--yellow': '#e0a83c',
    '--yellow-ink': '#7d5f18',
    '--mauve': '#8f6690',
    '--gutter-bg': '#e5e9f0',
    '--accent-bg': '#45648c',
    '--accent-border': '#5e81ac',
    '--accent-hover': '#3f5d84',
    '--accent-text': '#eceff4',
    '--accent-sub': '#dfe8f4',
    '--on-highlight': '#2e3440',
    '--current-line-bg': 'rgba(94, 129, 172, 0.10)',
    '--current-line-gutter': 'rgba(94, 129, 172, 0.16)',
    '--error-bg': '#f7e0e4',
    '--error-border': '#b04a54',
    '--error-text': '#8a2630',
    '--backdrop': 'rgba(46, 52, 64, 0.28)',
    '--shadow-color': 'rgba(46, 52, 64, 0.18)',
  },
};

// Qt desktop "Spring" palette: mint background with teal highlight.
const spring: ThemeDefinition = {
  id: 'spring',
  labelKey: 'settings.themeSpring',
  dark: false,
  palette: {
    text: argb('#2a475f'),
    base: argb('#eef7f2'),
    dimmed: argb('#6d887c'),
    selectionText: argb('#ffffff'),
    selectionBack: argb('#2c9678'),
    matchFore: argb('#1b2c36'),
    matchBack: argb('#b6d7a8'),
    matchLine: argb('#d92b5c'),
    mark: argb('#1466b8'),
    event: argb('#c47a12'),
    currentLineBack: argb('#e5f4ea'),
  },
  xterm: {
    background: '#eef7f2',
    foreground: '#2a475f',
    cursor: '#2c9678',
    cursorAccent: '#eef7f2',
    selectionBackground: '#b9d8c8',
    black: '#2a475f',
    red: '#d92b5c',
    green: '#1ba784',
    yellow: '#b06f0f',
    blue: '#2a52be',
    magenta: '#7d5ba6',
    cyan: '#179299',
    white: '#c3d5cb',
    brightBlack: '#41637b',
    brightRed: '#eb3c70',
    brightGreen: '#2c9678',
    brightYellow: '#c47a12',
    brightBlue: '#3b63d0',
    brightMagenta: '#8b68b5',
    brightCyan: '#1a9c9c',
    brightWhite: '#d8e6de',
  },
  css: {
    '--bg': '#eef7f2',
    '--surface': '#e3f0e8',
    '--surface-2': '#d8eadd',
    '--overlay': '#b4cfc0',
    '--text': '#2a475f',
    '--sub': '#41637b',
    '--muted': '#5f7a6e',
    '--faint': '#9db8aa',
    '--blue': '#2a52be',
    '--mark': '#1466b8',
    '--green': '#257a61',
    '--red': '#c21f4b',
    '--yellow': '#e0921f',
    '--yellow-ink': '#8a5a00',
    '--mauve': '#7d5ba6',
    '--gutter-bg': '#e3f0e8',
    '--accent-bg': '#24836a',
    '--accent-border': '#1f7460',
    '--accent-hover': '#1f7460',
    '--accent-text': '#ffffff',
    '--accent-sub': '#dff3ec',
    '--on-highlight': '#1b2c36',
    '--current-line-bg': 'rgba(44, 150, 120, 0.09)',
    '--current-line-gutter': 'rgba(44, 150, 120, 0.14)',
    '--error-bg': '#fbe4e8',
    '--error-border': '#d92b5c',
    '--error-text': '#8a1c34',
    '--backdrop': 'rgba(60, 80, 70, 0.30)',
    '--shadow-color': 'rgba(42, 71, 95, 0.20)',
  },
};

export const themes: Record<ThemeName, ThemeDefinition> = {
  mocha,
  latte,
  nord,
  'nord-light': nordLight,
  spring,
};

// Registry order drives the settings list and the tab bar quick toggle.
export const themeList: ThemeDefinition[] = [mocha, latte, nord, nordLight, spring];

export function isThemeId(value: unknown): value is ThemeId {
  return value === 'system' || (typeof value === 'string' && Object.hasOwn(themes, value));
}

export function systemPrefersDark(): boolean {
  return typeof window !== 'undefined' && typeof window.matchMedia === 'function'
    ? window.matchMedia('(prefers-color-scheme: dark)').matches
    : true;
}

export function resolveTheme(id: ThemeId): ThemeDefinition {
  if (id !== 'system' && Object.hasOwn(themes, id)) {
    return themes[id];
  }
  return systemPrefersDark() ? mocha : latte;
}

export function nextTheme(id: ThemeName): ThemeName {
  const index = themeList.findIndex((entry) => entry.id === id);
  return themeList[(index + 1) % themeList.length].id;
}

export function applyTheme(theme: ThemeDefinition): void {
  if (typeof document === 'undefined') {
    return;
  }
  const root = document.documentElement;
  for (const [name, value] of Object.entries(theme.css)) {
    root.style.setProperty(name, value);
  }
  root.dataset.theme = theme.id;
  root.style.colorScheme = theme.dark ? 'dark' : 'light';
  // Search match colours default to the palette; the settings layer may override
  // them, and both panes read the same variables.
  root.style.setProperty('--search-match-bg', htmlColor(theme.palette.matchBack));
  root.style.setProperty('--search-match-fg', htmlColor(theme.palette.matchFore));
}

/** Follows OS colour-scheme changes; the caller decides whether the user asked for it. */
export function onSystemThemeChange(handler: () => void): () => void {
  if (typeof window === 'undefined' || typeof window.matchMedia !== 'function') {
    return () => {};
  }
  const query = window.matchMedia('(prefers-color-scheme: dark)');
  const listener = () => handler();
  query.addEventListener('change', listener);
  return () => query.removeEventListener('change', listener);
}
