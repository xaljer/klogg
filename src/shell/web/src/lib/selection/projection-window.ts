// Window math for the filtered projection. The list renders one page around a
// requested row and maps indices to scroll offsets, so these small pure helpers
// are unit tested instead of being re-derived inside the component.

/** First window index that shows `index` roughly centered in a page. */
export function centeredWindowStart(index: number, total: number, page: number): number {
  if (total <= 0) {
    return 0;
  }
  const pageSize = Math.max(1, page);
  const lastStart = Math.max(0, total - pageSize);
  const wanted = index - Math.floor(pageSize / 2);
  return Math.max(0, Math.min(wanted, lastStart));
}

export function windowContains(from: number, count: number, index: number): boolean {
  return index >= from && index < from + count;
}

/**
 * Index of the last entry at or before `line` in a sorted list, or null when the
 * line is before every entry. Mirrors the session's filtered-line lookup for the
 * marks-only projection, which has no server-side index.
 */
export function sortedIndexAtOrBefore(entries: readonly number[], line: number): number | null {
  let low = 0;
  let high = entries.length - 1;
  let found = -1;
  while (low <= high) {
    const middle = (low + high) >> 1;
    if (entries[middle] <= line) {
      found = middle;
      low = middle + 1;
    } else {
      high = middle - 1;
    }
  }
  return found >= 0 ? found : null;
}
