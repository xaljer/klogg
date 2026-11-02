const RECENT_KEY = 'klogg.web.recent';
const LAST_KEY = 'klogg.web.last';

interface RecentEntry {
  path: string;
  openedAt: number;
}

function readRecentEntries(limit: number): RecentEntry[] {
  const boundedLimit = Math.max(1, limit);
  try {
    const raw = localStorage.getItem(RECENT_KEY);
    const parsed = raw ? JSON.parse(raw) : [];
    if (!Array.isArray(parsed)) {
      return [];
    }

    const now = Date.now();
    const entries: RecentEntry[] = [];
    parsed.forEach((item, index) => {
      if (typeof item === 'string' && item.length > 0) {
        // Legacy string arrays are already newest-first. Give them stable
        // descending timestamps so they sort together with new entries.
        entries.push({ path: item, openedAt: now - index });
        return;
      }
      if (
        item &&
        typeof item === 'object' &&
        typeof (item as { path?: unknown }).path === 'string' &&
        (item as { path: string }).path.length > 0
      ) {
        const openedAt = (item as { openedAt?: unknown }).openedAt;
        entries.push({
          path: (item as { path: string }).path,
          openedAt: typeof openedAt === 'number' && Number.isFinite(openedAt) ? openedAt : now - index,
        });
      }
    });

    const unique = new Map<string, RecentEntry>();
    for (const entry of entries) {
      const existing = unique.get(entry.path);
      if (!existing || entry.openedAt > existing.openedAt) {
        unique.set(entry.path, entry);
      }
    }

    return [...unique.values()]
      .sort((left, right) => right.openedAt - left.openedAt)
      .slice(0, boundedLimit);
  } catch {
    return [];
  }
}

export function loadRecent(limit = 8): string[] {
  return readRecentEntries(limit).map((entry) => entry.path);
}

export function rememberRecent(path: string, limit = 8): string[] {
  const boundedLimit = Math.max(1, limit);
  const entries = readRecentEntries(boundedLimit).filter((entry) => entry.path !== path);
  const latestTime = entries.reduce((latest, entry) => Math.max(latest, entry.openedAt), 0);
  const openedAt = Math.max(Date.now(), latestTime + 1);
  const next = [{ path, openedAt }, ...entries].slice(0, boundedLimit);
  try {
    localStorage.setItem(RECENT_KEY, JSON.stringify(next));
    localStorage.setItem(LAST_KEY, path);
  } catch {
    // private mode / storage disabled
  }
  return next.map((entry) => entry.path);
}

export function lastPath(): string {
  try {
    return localStorage.getItem(LAST_KEY) ?? '';
  } catch {
    return '';
  }
}
