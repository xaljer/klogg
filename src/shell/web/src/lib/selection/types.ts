// Shared selection vocabulary for the main log view and the filtered view.
// The key space is view specific (source line number vs. filtered result
// index), but the gesture, range and autoscroll rules are not.

export type SelectionKey = number;

export interface SelectionRange {
  /** Fixed end; Shift extension keeps it. */
  base: SelectionKey;
  /** Moving end. */
  focus: SelectionKey;
}

export interface SelectionSnapshot {
  start: SelectionKey;
  end: SelectionKey;
  count: number;
  startText: string;
  endText: string;
}

export type SelectionMode = 'rows-only' | 'rows-or-native';
export type SelectionPhase = 'idle' | 'pressed' | 'dragging';

export function orderedRange(range: SelectionRange): {
  start: SelectionKey;
  end: SelectionKey;
} {
  return range.base <= range.focus
    ? { start: range.base, end: range.focus }
    : { start: range.focus, end: range.base };
}

export function rangeCount(range: SelectionRange): number {
  const { start, end } = orderedRange(range);
  return end - start + 1;
}

export function rangeContains(range: SelectionRange, key: SelectionKey): boolean {
  const { start, end } = orderedRange(range);
  return key >= start && key <= end;
}
