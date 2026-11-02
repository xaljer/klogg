/**
 * Mapping between a capped virtual scrollbar and result indices.
 *
 * The filtered projection can hold millions of matches, above the browser's
 * maximum element height. The spacer height is therefore capped, and the
 * scrollbar position maps to result indices instead of raw pixels. Inside the
 * currently loaded window the mapping uses the real row height, so scrolling
 * through rendered rows stays exact.
 */
export interface VirtualScrollState {
  total: number;
  rowHeight: number;
  maxHeight: number;
  loadedFrom: number;
  loadedCount: number;
}

export function virtualHeightFor(
  total: number,
  rowHeight: number,
  maxHeight: number,
): number {
  if (total <= 0 || rowHeight <= 0) {
    return 0;
  }
  return Math.min(maxHeight, total * rowHeight);
}

export function pixelPerRowFor(total: number, virtualHeight: number, rowHeight: number): number {
  return total > 0 ? virtualHeight / total : rowHeight;
}

export function indexAtScrollTop(state: VirtualScrollState, scrollTop: number): number {
  const { total, rowHeight, maxHeight, loadedFrom, loadedCount } = state;
  if (total <= 0) {
    return 0;
  }
  const height = virtualHeightFor(total, rowHeight, maxHeight);
  const pixelPerRow = pixelPerRowFor(total, height, rowHeight);
  const topSpacer = Math.min(height, Math.round(loadedFrom * pixelPerRow));
  const rowStart = topSpacer;
  const rowEnd = topSpacer + loadedCount * rowHeight;
  if (loadedCount > 0 && scrollTop >= rowStart - rowHeight && scrollTop <= rowEnd) {
    return loadedFrom + Math.max(0, Math.floor((scrollTop - rowStart) / rowHeight));
  }
  const ratio = scrollTop / Math.max(1, height);
  return Math.min(total - 1, Math.max(0, Math.round(ratio * total)));
}

export function scrollTopForIndex(state: VirtualScrollState, index: number): number {
  const { total, rowHeight, maxHeight, loadedFrom, loadedCount } = state;
  const clamped = Math.min(Math.max(0, index), Math.max(0, total - 1));
  const height = virtualHeightFor(total, rowHeight, maxHeight);
  const pixelPerRow = pixelPerRowFor(total, height, rowHeight);
  const topSpacer = Math.min(height, Math.round(loadedFrom * pixelPerRow));
  if (clamped >= loadedFrom && clamped < loadedFrom + loadedCount) {
    return topSpacer + (clamped - loadedFrom) * rowHeight;
  }
  return Math.round((clamped / Math.max(1, total)) * height);
}
