import type { HighlightRule, HighlighterSet, QuickHighlighterDef } from './types';

const SETS_KEY = 'klogg.web.highlighterSets';
// Legacy flat rule list from before named sets existed; migrated on first load.
const LEGACY_RULES_KEY = 'klogg.web.highlighters';
const QUICK_KEY = 'klogg.web.quickHighlighters';
const SETS_VERSION = 1;

export const DEFAULT_SET_NAME = 'Default';

// Exact desktop klogg defaults: 10 slots, dark foreground on a light pastel
// background. Keep the RGB values in sync with core/state/highlighterset.cpp.
export const DEFAULT_QUICK_DEFS: QuickHighlighterDef[] = [
  { name: 'Color label 1', color: '#ff001e80', backColor: '#ffa1b7ff', useInCycle: true },
  { name: 'Color label 2', color: '#ff80005d', backColor: '#ffffa1c6', useInCycle: true },
  { name: 'Color label 3', color: '#ff0f8000', backColor: '#ffacffa1', useInCycle: true },
  { name: 'Color label 4', color: '#ff806000', backColor: '#ffffe8a1', useInCycle: true },
  { name: 'Color label 5', color: '#ff420080', backColor: '#ffd2a1ff', useInCycle: true },
  { name: 'Color label 6', color: '#ff007f80', backColor: '#ffa1feff', useInCycle: true },
  { name: 'Color label 7', color: '#ff004e80', backColor: '#ffa1dbff', useInCycle: true },
  { name: 'Color label 8', color: '#ff120080', backColor: '#ffa29ccf', useInCycle: true },
  { name: 'Color label 9', color: '#ffb14b28', backColor: '#ffffd6b0', useInCycle: true },
  { name: 'Color label 10', color: '#ffc21f30', backColor: '#ffffbeba', useInCycle: true },
];

export const DEFAULT_HIGHLIGHT_RULES: HighlightRule[] = [
  {
    name: 'ERROR',
    pattern: '\\bERROR\\b',
    useRegex: true,
    caseSensitive: true,
    color: '#fff38ba8',
    backColor: '',
    useThemeBack: true,
    onlyMatch: false,
    variateColors: false,
    colorVariance: 15,
    enabled: true,
  },
  {
    name: 'WARN',
    pattern: '\\bWARN(?:ING)?\\b',
    useRegex: true,
    caseSensitive: true,
    color: '#fff9e2af',
    backColor: '',
    useThemeBack: true,
    onlyMatch: false,
    variateColors: false,
    colorVariance: 15,
    enabled: true,
  },
];

export interface HighlighterStore {
  sets: HighlighterSet[];
  activeSetId: string;
}

export function createHighlightRule(overrides: Partial<HighlightRule> = {}): HighlightRule {
  return {
    name: '',
    pattern: '',
    useRegex: true,
    caseSensitive: false,
    color: '#ffffffff',
    backColor: '',
    useThemeBack: true,
    onlyMatch: false,
    variateColors: false,
    colorVariance: 15,
    enabled: true,
    ...overrides,
  };
}

function newSetId(): string {
  const random = globalThis.crypto?.randomUUID?.();
  return random
    ? `set-${random}`
    : `set-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
}

export function createHighlighterSet(
  name: string,
  rules: HighlightRule[] = [],
  id = newSetId(),
): HighlighterSet {
  return { id, name, rules };
}

function readJson(key: string): unknown {
  try {
    const raw = localStorage.getItem(key);
    return raw ? JSON.parse(raw) : null;
  } catch {
    return null;
  }
}

function writeJson(key: string, value: unknown): void {
  try {
    localStorage.setItem(key, JSON.stringify(value));
  } catch {
    // storage disabled
  }
}

function bool(value: unknown, fallback: boolean): boolean {
  return typeof value === 'boolean' ? value : fallback;
}

function text(value: unknown, fallback: string): string {
  return typeof value === 'string' ? value : fallback;
}

export function normalizeRule(value: unknown): HighlightRule | null {
  if (!value || typeof value !== 'object') {
    return null;
  }
  const source = value as Record<string, unknown>;
  const defaults = createHighlightRule();
  return {
    name: text(source.name, defaults.name),
    pattern: text(source.pattern, defaults.pattern),
    useRegex: bool(source.useRegex, defaults.useRegex),
    caseSensitive: bool(source.caseSensitive, defaults.caseSensitive),
    color: text(source.color, defaults.color),
    backColor: text(source.backColor, defaults.backColor),
    useThemeBack: bool(source.useThemeBack, defaults.useThemeBack),
    onlyMatch: bool(source.onlyMatch, defaults.onlyMatch),
    variateColors: bool(source.variateColors, defaults.variateColors),
    colorVariance:
      typeof source.colorVariance === 'number' && Number.isFinite(source.colorVariance)
        ? Math.min(100, Math.max(0, Math.round(source.colorVariance)))
        : defaults.colorVariance,
    enabled: bool(source.enabled, defaults.enabled),
  };
}

export function normalizeSet(value: unknown): HighlighterSet | null {
  if (!value || typeof value !== 'object') {
    return null;
  }
  const source = value as Record<string, unknown>;
  const rules = Array.isArray(source.rules)
    ? source.rules.map(normalizeRule).filter((rule): rule is HighlightRule => rule !== null)
    : [];
  const name = text(source.name, '').trim() || DEFAULT_SET_NAME;
  const id = text(source.id, '').trim() || newSetId();
  return { id, name, rules };
}

export function activeSet(store: HighlighterStore): HighlighterSet | null {
  return store.sets.find((set) => set.id === store.activeSetId) ?? store.sets[0] ?? null;
}

/** Rules the viewport should receive: the active set only, in list order. */
export function activeRules(store: HighlighterStore): HighlightRule[] {
  return activeSet(store)?.rules ?? [];
}

/** Empty when the rule can be used, otherwise why it cannot. */
export function patternError(rule: HighlightRule): string {
  const pattern = rule.pattern.trim();
  if (!pattern) {
    return 'empty';
  }
  if (!rule.useRegex) {
    return '';
  }
  try {
    new RegExp(pattern);
    return '';
  } catch (error) {
    return error instanceof Error ? error.message : 'invalid';
  }
}

function defaultStore(): HighlighterStore {
  const set = createHighlighterSet(
    DEFAULT_SET_NAME,
    DEFAULT_HIGHLIGHT_RULES.map((rule) => ({ ...rule })),
    'set-default',
  );
  return { sets: [set], activeSetId: set.id };
}

export function loadHighlighterStore(): HighlighterStore {
  const stored = readJson(SETS_KEY);
  if (stored && typeof stored === 'object') {
    const source = stored as Record<string, unknown>;
    const sets = Array.isArray(source.sets)
      ? source.sets.map(normalizeSet).filter((set): set is HighlighterSet => set !== null)
      : [];
    if (sets.length > 0) {
      const wanted = text(source.activeSetId, '');
      const activeSetId = sets.some((set) => set.id === wanted) ? wanted : sets[0].id;
      return { sets, activeSetId };
    }
  }

  // One-time migration from the flat rule list of older Web builds.
  const legacy = readJson(LEGACY_RULES_KEY);
  if (Array.isArray(legacy)) {
    const rules = legacy.map(normalizeRule).filter((rule): rule is HighlightRule => rule !== null);
    const set = createHighlighterSet(DEFAULT_SET_NAME, rules, 'set-default');
    const store: HighlighterStore = { sets: [set], activeSetId: set.id };
    saveHighlighterStore(store);
    try {
      localStorage.removeItem(LEGACY_RULES_KEY);
    } catch {
      // storage disabled
    }
    return store;
  }

  return defaultStore();
}

export function saveHighlighterStore(store: HighlighterStore): void {
  writeJson(SETS_KEY, {
    version: SETS_VERSION,
    activeSetId: store.activeSetId,
    sets: store.sets,
  });
}

/** JSON text for a downloadable export; self describing with a version. */
export function exportHighlighterStore(store: HighlighterStore): string {
  return JSON.stringify(
    {
      format: 'klogg-web-highlighters',
      version: SETS_VERSION,
      activeSetId: store.activeSetId,
      sets: store.sets,
    },
    null,
    2,
  );
}

export interface ImportResult {
  store: HighlighterStore;
  imported: number;
  skipped: number;
}

/**
 * Merges sets from an export into the current store. A set is skipped when its
 * id or its name is already present, matching the desktop import behaviour.
 */
export function importHighlighterStore(store: HighlighterStore, jsonText: string): ImportResult {
  const parsed = JSON.parse(jsonText) as unknown;
  const source = Array.isArray(parsed)
    ? { sets: parsed }
    : ((parsed ?? {}) as Record<string, unknown>);
  const rawSets = Array.isArray(source.sets) ? source.sets : [];
  const sets = [...store.sets];
  let imported = 0;
  let skipped = 0;
  for (const raw of rawSets) {
    const incoming = normalizeSet(raw);
    if (!incoming) {
      skipped++;
      continue;
    }
    const clash = sets.some(
      (set) => set.id === incoming.id || set.name.trim() === incoming.name.trim(),
    );
    if (clash) {
      skipped++;
      continue;
    }
    sets.push(incoming);
    imported++;
  }
  const activeSetId = sets.some((set) => set.id === store.activeSetId)
    ? store.activeSetId
    : (sets[0]?.id ?? '');
  return { store: { sets, activeSetId }, imported, skipped };
}

export function loadQuickDefs(): QuickHighlighterDef[] {
  const stored = readJson(QUICK_KEY);
  const defs = Array.isArray(stored) ? (stored as QuickHighlighterDef[]) : DEFAULT_QUICK_DEFS;
  // Older Web builds used 8 slots with a translucent foreground-like
  // background. The desktop palette is fixed at 10, so migrate that layout
  // instead of keeping the visually wrong colors.
  if (defs.length !== DEFAULT_QUICK_DEFS.length) {
    const defaults = DEFAULT_QUICK_DEFS.map((def) => ({ ...def }));
    saveQuickDefs(defaults);
    return defaults;
  }
  return defs;
}

export function saveQuickDefs(defs: QuickHighlighterDef[]): void {
  writeJson(QUICK_KEY, defs);
}

export function toApiColor(htmlColor: string): string {
  if (/^#[0-9a-f]{8}$/i.test(htmlColor)) {
    return htmlColor;
  }
  if (/^#[0-9a-f]{6}$/i.test(htmlColor)) {
    return `#ff${htmlColor.slice(1)}`;
  }
  return htmlColor;
}

export function toHtmlColor(apiColor: string): string {
  if (/^#[0-9a-f]{8}$/i.test(apiColor)) {
    return `#${apiColor.slice(3)}`;
  }
  if (/^#[0-9a-f]{6}$/i.test(apiColor)) {
    return apiColor;
  }
  return '#f38ba8';
}
