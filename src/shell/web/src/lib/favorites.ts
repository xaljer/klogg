const FAVORITES_KEY = 'klogg.web.favorites';
const MAX_FAVORITES = 100;

function samePath(left: string, right: string): boolean {
  const a = left.trim().replace(/\//g, '\\').replace(/\\+$/, '');
  const b = right.trim().replace(/\//g, '\\').replace(/\\+$/, '');
  const windowsPath = /^[a-zA-Z]:[\\/]/.test(a) || /^[a-zA-Z]:[\\/]/.test(b);
  return windowsPath ? a.toLowerCase() === b.toLowerCase() : a === b;
}

export function loadFavorites(): string[] {
  try {
    const raw = localStorage.getItem(FAVORITES_KEY);
    const parsed = raw ? JSON.parse(raw) : [];
    return Array.isArray(parsed)
      ? parsed
          .filter((value): value is string => typeof value === 'string' && value.trim().length > 0)
          .slice(0, MAX_FAVORITES)
      : [];
  } catch {
    return [];
  }
}

export function saveFavorites(paths: string[]): void {
  try {
    localStorage.setItem(
      FAVORITES_KEY,
      JSON.stringify([...new Set(paths)].slice(0, MAX_FAVORITES)),
    );
  } catch {
    // storage disabled
  }
}

export function isFavorite(path: string, favorites: string[]): boolean {
  return favorites.some((item) => samePath(item, path));
}

export function addFavorite(path: string, favorites: string[]): string[] {
  const next = [path, ...favorites.filter((item) => !samePath(item, path))].slice(
    0,
    MAX_FAVORITES,
  );
  saveFavorites(next);
  return next;
}

export function removeFavorite(path: string, favorites: string[]): string[] {
  const next = favorites.filter((item) => !samePath(item, path));
  saveFavorites(next);
  return next;
}

export function toggleFavorite(path: string, favorites: string[]): string[] {
  return isFavorite(path, favorites) ? removeFavorite(path, favorites) : addFavorite(path, favorites);
}
