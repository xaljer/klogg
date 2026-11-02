// The two log views report their selection in one shape. The main view owns an
// xterm column selection, the filtered view a DOM one, but everything a menu
// action needs — line bounds, loaded rows, the within-row portion, the
// pointed-at line — is the same, so the pane never has to guess which view
// produced it.

export interface SelectionRow {
  /** Source line, 0-based. */
  line: number;
  text: string;
}

/** Desktop "portion selection": characters inside one row. */
export interface PortionSelection {
  line: number;
  text: string;
}

export interface SelectionRangeInfo {
  /** First selected source line, 0-based. */
  start: number;
  /** Last selected source line, 0-based. */
  end: number;
}

export interface SelectionPayload {
  /** Line the pointer is on; the fallback when nothing is selected. */
  focusLine: number;
  focusText: string;
  /**
   * Line bounds of the selection; null when nothing is selected. Kept apart
   * from `rows` because a range can extend past the loaded window.
   */
  range: SelectionRangeInfo | null;
  /** Loaded rows of the selection, in file order (text for copy actions). */
  rows: SelectionRow[];
  /** Selected characters inside one row, if the selection is a portion. */
  portion: PortionSelection | null;
}

/** Event payload both views send up for the context menu. */
export interface SelectionContextRequest extends SelectionPayload {
  x: number;
  y: number;
  /** Filtered view: size of the result set, for "save all results". */
  total?: number;
}

export function selectionHasSelection(payload: SelectionPayload): boolean {
  return payload.range !== null;
}

/** Whether the payload carries rows whose text is loaded (copy actions). */
export function selectionHasRows(payload: SelectionPayload): boolean {
  return payload.rows.length > 0;
}

export function selectionIsMultiRow(payload: SelectionPayload): boolean {
  return payload.range !== null && payload.range.end > payload.range.start;
}

export function selectionRowCount(payload: SelectionPayload): number {
  return payload.range !== null ? payload.range.end - payload.range.start + 1 : 0;
}

export function selectionStartLine(payload: SelectionPayload): number {
  return payload.range?.start ?? payload.focusLine;
}

export function selectionEndLine(payload: SelectionPayload): number {
  return payload.range?.end ?? payload.focusLine;
}

/** Loaded selected rows; falls back to the pointed-at line (never empty). */
export function selectionLines(payload: SelectionPayload): number[] {
  return payload.rows.length > 0 ? payload.rows.map((row) => row.line) : [payload.focusLine];
}

/**
 * Text a plain copy or a colour label keys on: the selected characters for a
 * portion, the selected lines otherwise, the pointed-at line when nothing is
 * selected.
 */
export function selectionText(payload: SelectionPayload): string {
  if (payload.portion) {
    return payload.portion.text;
  }
  if (payload.rows.length > 0) {
    return payload.rows.map((row) => row.text).join('\n');
  }
  return payload.focusText;
}

/** "Copy with line numbers": a portion keeps its own characters. */
export function selectionCopyLinesText(payload: SelectionPayload): string {
  if (payload.portion) {
    return `${payload.portion.line + 1}: ${payload.portion.text}`;
  }
  const rows =
    payload.rows.length > 0
      ? payload.rows
      : [{ line: payload.focusLine, text: payload.focusText }];
  return rows.map((row) => `${row.line + 1}: ${row.text}`).join('\n');
}

/** Text only a portion selection can drive (replace/add/exclude search). */
export function selectionPortionText(payload: SelectionPayload): string {
  return payload.portion?.text ?? '';
}
