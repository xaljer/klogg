export interface SavedTabSession {
  path: string;
  state?: Record<string, unknown>;
  pinned?: boolean;
}

export interface SavedSession {
  tabs: SavedTabSession[];
  activeIndex: number;
}

const KEY = 'klogg.web.session';

function currentWindowId(): string | null {
  try {
    const params = new URLSearchParams(
      typeof window === 'undefined' ? '' : window.location.search,
    );
    return params.get('window') || null;
  } catch {
    return null;
  }
}

export function sessionStorageKey(windowId: string | null | undefined = currentWindowId()): string {
  return windowId ? `${KEY}.${windowId}` : KEY;
}

export function loadSession(
  windowId: string | null | undefined = currentWindowId(),
): SavedSession | null {
  try {
    const raw = localStorage.getItem(sessionStorageKey(windowId));
    if (!raw) {
      return null;
    }
    const parsed = JSON.parse(raw) as Partial<SavedSession>;
    if (!Array.isArray(parsed.tabs)) {
      return null;
    }
    const tabs = parsed.tabs
      .filter(
        (tab): tab is SavedTabSession =>
          !!tab && typeof tab === 'object' && typeof (tab as SavedTabSession).path === 'string',
      )
      .slice(0, 16)
      .map((tab) => ({
        path: tab.path,
        state: tab.state,
        pinned: tab.pinned === true,
      }));
    return { tabs, activeIndex: Number(parsed.activeIndex) || 0 };
  } catch {
    return null;
  }
}

export function saveSession(
  session: SavedSession,
  windowId: string | null | undefined = currentWindowId(),
): void {
  const key = sessionStorageKey(windowId);
  try {
    if (session.tabs.length === 0) {
      localStorage.removeItem(key);
      return;
    }
    localStorage.setItem(key, JSON.stringify(session));
  } catch {
    // storage disabled
  }
}
