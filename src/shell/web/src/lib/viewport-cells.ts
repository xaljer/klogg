import type { ViewportLine } from './types';

// The core lays text out in UTF-16 columns; xterm lays it out in display cells
// (CJK is two cells). This module is the glue between the two, mirroring the
// P0 spike that proved the approach.
export interface VisualRow {
  line: ViewportLine;
  partIndex: number;
  /** First UTF-16 index of this row inside line.text. */
  visStart: number;
  text: string;
}

export function isWide(codePoint: number): boolean {
  return (
    (codePoint >= 0x1100 && codePoint <= 0x115f) ||
    (codePoint >= 0x2e80 && codePoint <= 0xa4cf && codePoint !== 0x303f) ||
    (codePoint >= 0xac00 && codePoint <= 0xd7a3) ||
    (codePoint >= 0xf900 && codePoint <= 0xfaff) ||
    (codePoint >= 0xfe10 && codePoint <= 0xfe19) ||
    (codePoint >= 0xfe30 && codePoint <= 0xfe6f) ||
    (codePoint >= 0xff00 && codePoint <= 0xff60) ||
    (codePoint >= 0xffe0 && codePoint <= 0xffe6) ||
    (codePoint >= 0x1f300 && codePoint <= 0x1f64f) ||
    (codePoint >= 0x1f900 && codePoint <= 0x1f9ff) ||
    (codePoint >= 0x20000 && codePoint <= 0x3fffd)
  );
}

export function cellWidth(codePoint: number): number {
  return isWide(codePoint) ? 2 : 1;
}

export function textCells(text: string): number {
  let cells = 0;
  for (const ch of text) {
    cells += cellWidth(ch.codePointAt(0)!);
  }
  return cells;
}

export function splitByCells(text: string, maxCells: number): { start: number; end: number; text: string }[] {
  const pieces: { start: number; end: number; text: string }[] = [];
  let start = 0;
  let cells = 0;
  let index = 0;

  while (index < text.length) {
    const codePoint = text.codePointAt(index)!;
    const width = cellWidth(codePoint);
    if (cells > 0 && cells + width > maxCells) {
      pieces.push({ start, end: index, text: text.slice(start, index) });
      start = index;
      cells = 0;
    }
    cells += width;
    index += String.fromCodePoint(codePoint).length;
  }

  pieces.push({ start, end: text.length, text: text.slice(start) });
  return pieces;
}

export function utf16IndexAtCell(text: string, targetCell: number): number {
  let cells = 0;
  let index = 0;
  while (index < text.length && cells < targetCell) {
    const codePoint = text.codePointAt(index)!;
    cells += cellWidth(codePoint);
    index += String.fromCodePoint(codePoint).length;
  }
  return index;
}

export function trimToCells(text: string, maxCells: number): string {
  const first = splitByCells(text, maxCells)[0];
  return first ? first.text : '';
}

/**
 * Turns the server's composed lines into terminal rows.
 *
 * wrapCells > 0: re-wrap each backend part by display cells (the backend wraps
 * by UTF-16 columns, which is not the same for CJK).
 * wrapCells <= 0: one row per line, a cell window starting at firstColumn
 * (UTF-16) and trimmed to visibleCells cells.
 */
export function buildRows(
  lines: ViewportLine[],
  wrapCells: number,
  firstColumn: number,
  visibleCells: number,
): VisualRow[] {
  const rows: VisualRow[] = [];

  for (const line of lines) {
    if (wrapCells > 0) {
      const parts = line.parts.length ? line.parts : [{ line: 0, start: 0, length: 0 }];
      for (let partIndex = 0; partIndex < parts.length; partIndex++) {
        const part = parts[partIndex];
        const partText = line.text.slice(part.start, part.start + part.length);
        for (const piece of splitByCells(partText, wrapCells)) {
          rows.push({
            line,
            partIndex,
            visStart: part.start + piece.start,
            text: piece.text,
          });
        }
      }
    } else {
      const start = Math.max(0, Math.min(firstColumn, line.text.length));
      const window = line.text.slice(start, start + Math.max(visibleCells * 2, 64));
      const text = trimToCells(window, visibleCells);
      rows.push({
        line,
        partIndex: 0,
        visStart: start,
        text,
      });
    }
  }

  return rows;
}
