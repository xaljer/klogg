import type { LanguageSetting } from './i18n';
// Node runs the unit tests on the sources directly, so runtime imports need the
// explicit extension (see allowImportingTsExtensions in tsconfig.json).
import { isThemeId, type ThemeId } from './theme.ts';

export type ShortcutMap = Record<string, string[]>;

// Every action has two bindings. A letter/symbol key is always the first one:
// single-key shortcuts are what the log view is driven with, so they lead.
export const SHORTCUT_SLOTS = 2;

// Log font size in pixels. The desktop UI steps through every size the font
// offers, so this range is deliberately wide instead of the old 11..18 window
// that made large displays and high zoom levels impossible.
export const FONT_SIZE_MIN = 8;
export const FONT_SIZE_MAX = 40;

// Version a profile has to be older than for a migration to run. They are kept
// apart so a later bump does not re-run the numeric default swaps.
const VALUE_DEFAULTS_VERSION = 2;
const SHORTCUT_SLOTS_VERSION = 4;

// Bumped when a stored default changes shape or value and has to be adopted by
// existing browser profiles.
const SETTINGS_VERSION = SHORTCUT_SLOTS_VERSION;

export interface RecorderCommand {
  name: string;
  command: string;
}

export interface WebSettings {
  settingsVersion: number;
  restoreSession: boolean;
  autoWrap: boolean;
  autoFollow: boolean;
  language: LanguageSetting;
  theme: ThemeId;
  fontFamily: string;
  fontSize: number;
  boldFont: boolean;
  showMainLineNumbers: boolean;
  showFilteredLineNumbers: boolean;
  wordHighlightBackground: string;
  wordHighlightForeground: string;
  customSearchMatchColors: boolean;
  searchMatchBackground: string;
  searchMatchForeground: string;
  defaultSearchRegex: boolean;
  defaultSearchCaseSensitive: boolean;
  autoRunSearch: boolean;
  liveSearch: boolean;
  defaultEncoding: string;
  filePollIntervalMs: number;
  recentLimit: number;
  searchHistoryLimit: number;
  shortcuts: ShortcutMap;
}

export const SHORTCUT_ACTIONS: { id: string; labelKey: string }[] = [
  { id: 'nextMatch', labelKey: 'shortcut.nextMatch' },
  { id: 'prevMatch', labelKey: 'shortcut.prevMatch' },
  { id: 'search', labelKey: 'shortcut.search' },
  { id: 'selectAll', labelKey: 'shortcut.selectAll' },
  { id: 'openFile', labelKey: 'shortcut.openFile' },
  { id: 'openClipboard', labelKey: 'shortcut.openClipboard' },
  { id: 'openLocation', labelKey: 'shortcut.openLocation' },
  { id: 'jumpToLine', labelKey: 'shortcut.jumpToLine' },
  { id: 'goTop', labelKey: 'shortcut.goTop' },
  { id: 'goBottom', labelKey: 'shortcut.goBottom' },
  { id: 'wrap', labelKey: 'shortcut.wrap' },
  { id: 'selectionUp', labelKey: 'shortcut.selectionUp' },
  { id: 'selectionDown', labelKey: 'shortcut.selectionDown' },
  { id: 'closeTab', labelKey: 'shortcut.closeTab' },
  { id: 'settings', labelKey: 'shortcut.settings' },
  { id: 'mark', labelKey: 'shortcut.mark' },
  { id: 'nextMark', labelKey: 'shortcut.nextMark' },
  { id: 'prevMark', labelKey: 'shortcut.prevMark' },
  { id: 'addSearch', labelKey: 'shortcut.addSearch' },
  { id: 'excludeSearch', labelKey: 'shortcut.excludeSearch' },
  { id: 'replaceSearch', labelKey: 'shortcut.replaceSearch' },
  { id: 'followFile', labelKey: 'shortcut.followFile' },
  { id: 'nextColorLabel', labelKey: 'shortcut.nextColorLabel' },
  { id: 'clearColorLabels', labelKey: 'shortcut.clearColorLabels' },
  { id: 'copyWithColor', labelKey: 'shortcut.copyWithColor' },
  ...Array.from({ length: 9 }, (_, index) => ({
    id: `colorLabel${index + 1}`,
    labelKey: 'shortcut.colorLabel',
  })),
];

// Desktop klogg stores one or two (sometimes three) bindings per action. The
// Web settings expose two slots and keep a letter/symbol key in the first one.
export const DEFAULT_SHORTCUTS: ShortcutMap = {
  // Desktop LogViewQfForward binds FindNext, N and Ctrl+G to "find next"; two
  // slots keep the single-key N first and the conventional function key second.
  nextMatch: ['N', 'F3'],
  prevMatch: ['Shift+F3', 'Shift+N'],
  search: ['Ctrl+F', 'Ctrl+S'],
  selectAll: ['Ctrl+A', ''],
  openFile: ['Ctrl+O', ''],
  openClipboard: ['Ctrl+V', ''],
  // Desktop Ctrl+L jumps to a line; opening a URL/path is a Web addition and
  // therefore uses the non-conflicting Ctrl+Shift+L.
  openLocation: ['Ctrl+Shift+L', ''],
  jumpToLine: ['Ctrl+L', ''],
  // Desktop has both MainWindowGoToTop (T) and LogViewJumpToTop (Ctrl+Home).
  goTop: ['T', 'Ctrl+Home'],
  goBottom: ['Ctrl+End', 'Shift+G'],
  // Desktop MainWindowTextWrap.
  wrap: ['W', ''],
  // Desktop LogViewSelectionUp/Down: J and K move the selected line.
  selectionUp: ['K', ''],
  selectionDown: ['J', ''],
  closeTab: ['Ctrl+W', 'Ctrl+F4'],
  settings: ['Ctrl+,', ''],
  mark: ['M', ''],
  nextMark: [']', ''],
  prevMark: ['[', ''],
  addSearch: ['A', 'Shift+A'],
  excludeSearch: ['Shift+E', ''],
  replaceSearch: ['R', 'Shift+R'],
  // Desktop bindings for the checkable "Follow File" action.
  followFile: ['F', 'F10'],
  nextColorLabel: ['D', 'Ctrl+D'],
  clearColorLabels: ['Ctrl+Shift+0', ''],
  // Desktop MainWindowCopyWithColor.
  copyWithColor: ['Ctrl+Shift+C', ''],
  colorLabel1: ['Ctrl+Shift+1', ''],
  colorLabel2: ['Ctrl+Shift+2', ''],
  colorLabel3: ['Ctrl+Shift+3', ''],
  colorLabel4: ['Ctrl+Shift+4', ''],
  colorLabel5: ['Ctrl+Shift+5', ''],
  colorLabel6: ['Ctrl+Shift+6', ''],
  colorLabel7: ['Ctrl+Shift+7', ''],
  colorLabel8: ['Ctrl+Shift+8', ''],
  colorLabel9: ['Ctrl+Shift+9', ''],
};

const KEY = 'klogg.web.settings';

function clampInt(value: unknown, min: number, max: number, fallback: number): number {
  return typeof value === 'number' && Number.isFinite(value)
    ? Math.min(max, Math.max(min, Math.round(value)))
    : fallback;
}

function normalizeColor(value: unknown, fallback: string): string {
  if (typeof value !== 'string' || !/^#[0-9a-f]{6}$/i.test(value.trim())) {
    return fallback;
  }
  return value.trim().toLowerCase();
}

// Bindings the desktop UI gained after the Web defaults were first written; a
// profile stored before the version bump takes them into its empty slots.
const ADOPTED_DEFAULT_BINDINGS: Record<string, string[]> = {
  goTop: ['T'],
};

// "W" / "]" are single-key bindings; "Shift+E", "F3" and "Ctrl+Home" are not.
export function isSingleKeyShortcut(shortcut: string): boolean {
  const trimmed = shortcut.trim();
  return trimmed.length === 1;
}

/**
 * Collapses stored bindings into the two usable slots: no duplicates, empty
 * entries dropped, and single-key bindings first.
 */
function normalizeSlotList(values: string[]): string[] {
  const unique: string[] = [];
  for (const value of values) {
    const trimmed = value.trim();
    if (trimmed && !unique.includes(trimmed)) {
      unique.push(trimmed);
    }
  }
  const single = unique.filter(isSingleKeyShortcut);
  const rest = unique.filter((value) => !isSingleKeyShortcut(value));
  const slots = [...single, ...rest].slice(0, SHORTCUT_SLOTS);
  while (slots.length < SHORTCUT_SLOTS) {
    slots.push('');
  }
  return slots;
}

function normalizeShortcuts(
  stored: Record<string, unknown> | undefined,
  fallback: ShortcutMap,
  adoptNewDefaults: boolean,
): ShortcutMap {
  const result: ShortcutMap = {};
  for (const [action, defaults] of Object.entries(fallback)) {
    const value = stored?.[action];
    let values: string[];
    if (Array.isArray(value)) {
      values = value.filter((item): item is string => typeof item === 'string');
    } else if (typeof value === 'string') {
      // Migrate the original Web single-shortcut layout without losing the
      // desktop secondary binding for actions that have one.
      values = value ? [value, ...defaults.slice(1)] : [...defaults];
    } else {
      values = [...defaults];
    }
    if (adoptNewDefaults) {
      for (const binding of ADOPTED_DEFAULT_BINDINGS[action] ?? []) {
        if (!values.includes(binding)) {
          values.push(binding);
        }
      }
    }
    result[action] = normalizeSlotList(values);
  }
  // Ctrl+L belonged to the old Web "open URL/path" action; desktop uses it
  // for jump-to-line, so migrate existing Web settings to the new binding.
  if (result.openLocation?.[0] === 'Ctrl+L') {
    result.openLocation = normalizeSlotList(['Ctrl+Shift+L', ...result.openLocation.slice(1)]);
  }
  return result;
}

export function defaultSettings(): WebSettings {
  return {
    settingsVersion: SETTINGS_VERSION,
    restoreSession: true,
    autoWrap: false,
    autoFollow: false,
    language: 'auto',
    theme: 'mocha',
    fontFamily: '"Cascadia Mono", Consolas, monospace',
    fontSize: 13,
    boldFont: false,
    showMainLineNumbers: true,
    showFilteredLineNumbers: true,
    wordHighlightBackground: '#89b4fa',
    wordHighlightForeground: '#1e1e2e',
    customSearchMatchColors: false,
    searchMatchBackground: '#f9e2af',
    searchMatchForeground: '#1e1e2e',
    defaultSearchRegex: true,
    defaultSearchCaseSensitive: false,
    autoRunSearch: true,
    liveSearch: true,
    defaultEncoding: '',
    filePollIntervalMs: 1000,
    recentLimit: 15,
    searchHistoryLimit: 60,
    shortcuts: Object.fromEntries(
      Object.entries(DEFAULT_SHORTCUTS).map(([action, values]) => {
        const slots = [...values];
        while (slots.length < SHORTCUT_SLOTS) {
          slots.push('');
        }
        return [action, slots];
      }),
    ),
  };
}

export function loadSettings(): WebSettings {
  const fallback = defaultSettings();
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) {
      return fallback;
    }
    const parsed = JSON.parse(raw) as Partial<WebSettings>;
    // Profiles written before the defaults changed keep the old numbers in
    // storage; adopt the new defaults once unless the user picked something
    // else on purpose.
    const storedVersion = typeof parsed.settingsVersion === 'number' ? parsed.settingsVersion : 1;
    const adopt = (value: unknown, legacyDefault: number, nextDefault: number): unknown =>
      storedVersion < VALUE_DEFAULTS_VERSION && value === legacyDefault ? nextDefault : value;
    return {
      settingsVersion: SETTINGS_VERSION,
      restoreSession:
        typeof parsed.restoreSession === 'boolean'
          ? parsed.restoreSession
          : fallback.restoreSession,
      autoWrap: typeof parsed.autoWrap === 'boolean' ? parsed.autoWrap : fallback.autoWrap,
      autoFollow: typeof parsed.autoFollow === 'boolean' ? parsed.autoFollow : fallback.autoFollow,
      language:
        parsed.language === 'zh-CN' || parsed.language === 'en' || parsed.language === 'auto'
          ? parsed.language
          : fallback.language,
      theme: isThemeId(parsed.theme) ? parsed.theme : fallback.theme,
      fontFamily:
        typeof parsed.fontFamily === 'string' && parsed.fontFamily.trim()
          ? parsed.fontFamily.trim()
          : fallback.fontFamily,
      fontSize: clampInt(parsed.fontSize, FONT_SIZE_MIN, FONT_SIZE_MAX, fallback.fontSize),
      boldFont: typeof parsed.boldFont === 'boolean' ? parsed.boldFont : fallback.boldFont,
      showMainLineNumbers:
        typeof parsed.showMainLineNumbers === 'boolean'
          ? parsed.showMainLineNumbers
          : fallback.showMainLineNumbers,
      showFilteredLineNumbers:
        typeof parsed.showFilteredLineNumbers === 'boolean'
          ? parsed.showFilteredLineNumbers
          : fallback.showFilteredLineNumbers,
      wordHighlightBackground: normalizeColor(
        parsed.wordHighlightBackground,
        fallback.wordHighlightBackground,
      ),
      wordHighlightForeground: normalizeColor(
        parsed.wordHighlightForeground,
        fallback.wordHighlightForeground,
      ),
      customSearchMatchColors:
        typeof parsed.customSearchMatchColors === 'boolean'
          ? parsed.customSearchMatchColors
          : fallback.customSearchMatchColors,
      searchMatchBackground: normalizeColor(
        parsed.searchMatchBackground,
        fallback.searchMatchBackground,
      ),
      searchMatchForeground: normalizeColor(
        parsed.searchMatchForeground,
        fallback.searchMatchForeground,
      ),
      defaultSearchRegex:
        typeof parsed.defaultSearchRegex === 'boolean'
          ? parsed.defaultSearchRegex
          : fallback.defaultSearchRegex,
      defaultSearchCaseSensitive:
        typeof parsed.defaultSearchCaseSensitive === 'boolean'
          ? parsed.defaultSearchCaseSensitive
          : fallback.defaultSearchCaseSensitive,
      autoRunSearch:
        typeof parsed.autoRunSearch === 'boolean' ? parsed.autoRunSearch : fallback.autoRunSearch,
      liveSearch:
        typeof parsed.liveSearch === 'boolean' ? parsed.liveSearch : fallback.liveSearch,
      defaultEncoding:
        typeof parsed.defaultEncoding === 'string'
          ? parsed.defaultEncoding.trim()
          : fallback.defaultEncoding,
      filePollIntervalMs: clampInt(
        adopt(parsed.filePollIntervalMs, 2000, 1000),
        500,
        10000,
        fallback.filePollIntervalMs,
      ),
      recentLimit: clampInt(adopt(parsed.recentLimit, 8, 15), 4, 20, fallback.recentLimit),
      searchHistoryLimit: clampInt(
        adopt(parsed.searchHistoryLimit, 20, 60),
        5,
        200,
        fallback.searchHistoryLimit,
      ),
      shortcuts: normalizeShortcuts(
        parsed.shortcuts as Record<string, unknown> | undefined,
        fallback.shortcuts,
        storedVersion < SHORTCUT_SLOTS_VERSION,
      ),
    };
  } catch {
    return fallback;
  }
}

export function saveSettings(settings: WebSettings): void {
  try {
    localStorage.setItem(KEY, JSON.stringify(settings));
  } catch {
    // storage disabled
  }
}

const SHIFTED_KEYS: Record<string, string> = {
  '!': '1',
  '@': '2',
  '#': '3',
  $: '4',
  '%': '5',
  '^': '6',
  '&': '7',
  '*': '8',
  '(': '9',
  ')': '0',
  _: '-',
  '+': '=',
  '{': '[',
  '}': ']',
  '|': '\\',
  ':': ';',
  '"': "'",
  '<': ',',
  '>': '.',
  '?': '/',
  '~': '`',
};

function normalizeKey(key: string): string {
  if (key === ' ') {
    return 'Space';
  }
  if (key === 'Escape') {
    return 'Esc';
  }
  if (key.length === 1) {
    return key.toUpperCase();
  }
  return key;
}

export function eventToShortcut(event: KeyboardEvent): string {
  const parts: string[] = [];
  if (event.ctrlKey || event.metaKey) {
    parts.push('Ctrl');
  }
  if (event.altKey) {
    parts.push('Alt');
  }
  if (event.shiftKey) {
    parts.push('Shift');
  }
  const key = event.shiftKey ? (SHIFTED_KEYS[event.key] ?? event.key) : event.key;
  parts.push(normalizeKey(key));
  return parts.join('+');
}

export function matchesShortcut(event: KeyboardEvent, combo: string): boolean {
  if (!combo) {
    return false;
  }
  return eventToShortcut(event) === combo;
}
