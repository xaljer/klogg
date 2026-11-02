const TAB_NAMES_KEY = 'klogg.web.tabNames';

export function loadTabNames(): Record<string, string> {
  try {
    const raw = localStorage.getItem(TAB_NAMES_KEY);
    if (!raw) {
      return {};
    }
    const parsed = JSON.parse(raw) as Record<string, unknown>;
    const result: Record<string, string> = {};
    for (const [path, name] of Object.entries(parsed)) {
      if (typeof name === 'string' && name.trim().length > 0) {
        result[path] = name;
      }
    }
    return result;
  } catch {
    return {};
  }
}

export function saveTabNames(names: Record<string, string>): void {
  try {
    localStorage.setItem(TAB_NAMES_KEY, JSON.stringify(names));
  } catch {
    // storage disabled
  }
}

export function setTabName(
  path: string,
  name: string,
  names: Record<string, string>,
): Record<string, string> {
  const next = { ...names };
  if (name.trim()) {
    next[path] = name.trim();
  } else {
    delete next[path];
  }
  saveTabNames(next);
  return next;
}
