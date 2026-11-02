// Bounded "line number -> line text" memory for the selection endpoints.
//
// A selection may end on a line that scrolled out of view, and the interval
// still needs that line's timestamp. The window of rendered lines is cached as
// it changes; the cache drops everything once it outgrows its limit, because the
// selection anchor keeps its own copy of the endpoint texts it needs.

export interface LineTextCache {
  set(key: number, text: string): void;
  /** Remembers consecutive keys starting at `from`. */
  setWindow(from: number, texts: readonly string[]): void;
  get(key: number): string;
  clear(): void;
  readonly size: number;
}

export function createLineTextCache(limit = 4000): LineTextCache {
  let entries = new Map<number, string>();

  function set(key: number, text: string): void {
    if (entries.size >= limit) {
      entries = new Map();
    }
    entries.set(key, text);
  }

  return {
    set,
    setWindow(from: number, texts: readonly string[]): void {
      texts.forEach((text, index) => set(from + index, text));
    },
    get(key: number): string {
      return entries.get(key) ?? '';
    },
    clear(): void {
      entries = new Map();
    },
    get size() {
      return entries.size;
    },
  };
}
