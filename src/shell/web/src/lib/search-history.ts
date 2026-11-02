const HISTORY_KEY = 'klogg.web.searchHistory';
const MAX_HISTORY = 200;

export function loadSearchHistory(limit = 20): string[] {
  try {
    const raw = localStorage.getItem(HISTORY_KEY);
    const parsed = raw ? JSON.parse(raw) : [];
    return Array.isArray(parsed)
      ? parsed
          .filter((value): value is string => typeof value === 'string')
          .slice(0, Math.max(5, Math.min(MAX_HISTORY, limit)))
      : [];
  } catch {
    return [];
  }
}

export function clearSearchHistory(): void {
  try {
    localStorage.removeItem(HISTORY_KEY);
  } catch {
    // storage disabled
  }
}

export function removeSearchHistory(pattern: string, limit = 20): string[] {
  const next = loadSearchHistory(limit).filter((item) => item !== pattern);
  try {
    localStorage.setItem(HISTORY_KEY, JSON.stringify(next));
  } catch {
    // storage disabled
  }
  return next;
}

export function rememberSearch(pattern: string, limit = 20): string[] {
  const trimmed = pattern.trim();
  if (!trimmed) {
    return loadSearchHistory(limit);
  }
  const boundedLimit = Math.max(5, Math.min(MAX_HISTORY, limit));
  const next = [trimmed, ...loadSearchHistory(boundedLimit).filter((item) => item !== trimmed)].slice(
    0,
    boundedLimit,
  );
  try {
    localStorage.setItem(HISTORY_KEY, JSON.stringify(next));
  } catch {
    // storage disabled
  }
  return next;
}
