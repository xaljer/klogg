<script lang="ts">
  import { createEventDispatcher, onMount, tick } from 'svelte';
  import { t } from '../lib/i18n';
  import { Terminal } from '@xterm/xterm';
  import { FitAddon } from '@xterm/addon-fit';
  import '@xterm/xterm/css/xterm.css';

  import { ApiError } from '../lib/api';
  import type { ApiClient } from '../lib/api';
  import { RowSelectionController } from '../lib/selection/selection-controller';
  import type { SelectionSurface } from '../lib/selection/selection-surface';
  import {
    selectionEndLine,
    selectionHasRows,
    selectionHasSelection,
    selectionLines,
    selectionStartLine,
    type SelectionContextRequest,
    type SelectionPayload,
  } from '../lib/selection/payload';
  import { selectionWord } from '../lib/selection/selection-word';
  import type { SelectionRange } from '../lib/selection/types';
  import { createLineTextCache } from '../lib/line-text-cache';
import { FONT_SIZE_MAX, FONT_SIZE_MIN } from '../lib/settings';
  import type { ViewportLine } from '../lib/types';
  import { LineFlagEvent, LineFlagMark, LineFlagMatch } from '../lib/types';
  import type { Palette } from '../lib/theme';
  import {
    buildRows,
    textCells,
    utf16IndexAtCell,
    type VisualRow,
  } from '../lib/viewport-cells';

  export let api: ApiClient;
  export let fileId: number | null = null;
  export let totalLines = 0;
  export let firstLine = 0;
  export let wrap = false;
  // The parent freezes terminal sizing while the splitter is being dragged.
  export let dragging = false;
  export let searchId: number | null = null;
  export let pattern = '';
  export let searchRegex = true;
  export let searchCase = false;
  // Inclusive file-line bounds set from the context menu. The core frame takes
  // an exclusive end index and dims lines outside it.
  export let searchFromLine: number | null = null;
  export let searchToLine: number | null = null;
  export let palette: Palette;
  export let terminalTheme: Record<string, string> = {};
  export let fontSize = 13;
  export let fontFamily = '"Cascadia Mono", Consolas, monospace';
  export let boldFont = false;
  export let showLineNumbers = true;
  // Longest line of the file, in characters: it bounds the horizontal scroll so
  // an unwrapped line can be read to its end (the desktop view sizes its scroll
  // bar from the same value).
  export let maxLineLength = 0;
  // Bumped when marks (or other server-side frame state) change, so the
  // composed frame is fetched again.
  // Bumped when the frame (flags, colours, marks) must be fetched again.
  export let frameRevision = 0;
  // Source line of the current selection, drawn as the desktop UI's
  // single-line selection outline.
  export let currentLine: number | null = null;
  // Per-slot quick highlighter words (transient view state).
  export let quickWords: string[][] = [];
  // Word selected by double-click; all visible occurrences are highlighted.
  export let highlightWord = '';

  const dispatch = createEventDispatcher<{
    lineChange: number;
    rowsChange: number;
    selection: { text: string; range: string; lines: string[]; lineCount: number };
    wordHighlight: string;
    currentLineChange: number;
    markToggle: number;
    fontSizeChange: number;
    contextMenu: SelectionContextRequest;
    error: string;
    ready: void;
  }>();

  let host: HTMLDivElement;
  let viewportEl: HTMLDivElement;
  let gutterEl: HTMLDivElement;
  let currentLineEl: HTMLDivElement;
  let markedLinesEl: HTMLDivElement;
  let wordHitsEl: HTMLDivElement;
  let selection: RowSelectionController | null = null;
  // Endpoint texts for lines that are not rendered right now (Shift extending
  // after scrolling): their timestamps are still needed for the interval.
  const lineTextCache = createLineTextCache(2000);
  let zoomAccumulator = 0;

  let term: Terminal | null = null;
  let fitAddon: FitAddon | null = null;
  let rowMap: VisualRow[] = [];
  let searchStartIndex = 0;
  let searchEndIndex = 0;
  let hOffset = 0;
  let termCols = 0;
  let lastFileId = -1;
  let lastRows = 0;
  let loading = false;
  // The loading pill is delayed so a fast live refresh never flashes it; after
  // the first painted frame only explicit file switches may show it again.
  let loadingVisible = false;
  let hasRendered = false;
  let loadingTimer = 0;
  let errorText = '';
  let requestSeq = 0;
  let renderFrame = 0;
  let resizeRenderTimer = 0;
  let lastHostSize = '';
  let resizeObserver: ResizeObserver | null = null;
  let disposed = false;

  let liveRenderTimer = 0;
  let renderCostMs = 0;

  function scheduleLiveRender(): void {
    if (liveRenderTimer) {
      return;
    }
    // Back off when the server/viewport path is slow: at GB sizes a fixed
    // 200 ms cadence can keep the renderer permanently busy.
    const delay = Math.min(1000, Math.max(200, Math.round(renderCostMs * 4)));
    liveRenderTimer = window.setTimeout(() => {
      liveRenderTimer = 0;
      scheduleRender(true);
    }, delay);
  }

  function scheduleRender(immediate = false) {
    if (!term || fileId === null || disposed) {
      return;
    }
    if (immediate) {
      if (renderFrame) {
        window.cancelAnimationFrame(renderFrame);
        renderFrame = 0;
      }
      void render();
      return;
    }
    if (renderFrame) {
      return;
    }
    renderFrame = window.requestAnimationFrame(() => {
      renderFrame = 0;
      void render();
    });
  }

  function clamp(value: number, min: number, max: number) {
    return Math.min(Math.max(value, min), max);
  }

  function parseColor(hex: string): { r: number; g: number; b: number } | null {
    if (!hex || hex.length !== 9 || hex[0] !== '#') {
      return null;
    }
    const alpha = Number.parseInt(hex.slice(1, 3), 16);
    if (alpha === 0) {
      return null;
    }
    return {
      r: Number.parseInt(hex.slice(3, 5), 16),
      g: Number.parseInt(hex.slice(5, 7), 16),
      b: Number.parseInt(hex.slice(7, 9), 16),
    };
  }

  function sgr(
    fg: string,
    bg: string,
    bold = false,
    italic = false,
    underline = false,
  ): string {
    let out = '\x1b[0m';
    const fore = parseColor(fg);
    const back = parseColor(bg);
    if (fore) out += `\x1b[38;2;${fore.r};${fore.g};${fore.b}m`;
    if (back) out += `\x1b[48;2;${back.r};${back.g};${back.b}m`;
    if (bold) out += '\x1b[1m';
    if (italic) out += '\x1b[3m';
    if (underline) out += '\x1b[4m';
    return out;
  }

  // Log text is arbitrary bytes: C0 controls would move the terminal cursor.
  function sanitize(text: string): string {
    const map: Record<string, string> = {
      '\u0000': '␀',
      '\u0007': '␇',
      '\u0008': '␈',
      '\u000b': '␋',
      '\u000c': '␌',
      '\u000d': '␍',
      '\u001b': '␛',
    };
    return text
      .replace(/\t/g, '    ')
      .replace(/[\u0000-\u0008\u000a-\u001f\u007f]/g, (ch) => map[ch] ?? '·');
  }

  function rowAnsi(row: VisualRow, cols: number): string {
    const line = row.line;
    const text = row.text;
    const isCurrent = currentLine !== null && line.lineNumber === currentLine;
    const rowBack = isCurrent ? palette.currentLineBack : line.backColor;
    const chunks: {
      start: number;
      length: number;
      fg: string;
      bg: string;
      bold: boolean;
      italic: boolean;
      underline: boolean;
    }[] = [];

    for (const chunk of line.chunks) {
      const start = Math.max(chunk.start, row.visStart);
      const end = Math.min(chunk.start + chunk.length, row.visStart + text.length);
      if (end > start) {
        chunks.push({
          start: start - row.visStart,
          length: end - start,
          fg: chunk.foreColor,
          bg: chunk.backColor,
          bold: false,
          italic: false,
          underline: false,
        });
      }
    }
    chunks.sort((a, b) => a.start - b.start);

    let out = '';
    let cursor = 0;
    for (const chunk of chunks) {
      if (chunk.start > cursor) {
        out += sgr(line.foreColor, rowBack) + sanitize(text.slice(cursor, chunk.start));
      }
      out +=
        sgr(
          chunk.fg || line.foreColor,
          chunk.bg || rowBack,
          chunk.bold,
          chunk.italic,
          chunk.underline,
        ) + sanitize(text.slice(chunk.start, chunk.start + chunk.length));
      cursor = Math.max(cursor, chunk.start + chunk.length);
    }
    if (cursor < text.length) {
      out += sgr(line.foreColor, rowBack) + sanitize(text.slice(cursor));
    }

    if (isCurrent || (line.backColor && line.backColor !== palette.base)) {
      out +=
        sgr(line.foreColor, rowBack) +
        ' '.repeat(Math.max(0, cols - 1 - textCells(text)));
    }
    return out + '\x1b[0m';
  }

  function selectionPayload(line: number, lineText: string): SelectionPayload {
    const rowRange = selection?.snapshot();
    const position = term?.hasSelection() ? term.getSelectionPosition() : undefined;
    if (!rowRange && !position) {
      return { focusLine: line, focusText: lineText, range: null, rows: [], portion: null };
    }
    if (!position) {
      // The controller range can outlive the visible selection when the list was
      // scrolled away; the line bounds stay usable without loaded rows.
      return {
        focusLine: line,
        focusText: lineText,
        range: { start: rowRange!.start, end: rowRange!.end },
        rows: [],
        portion: null,
      };
    }

    const forward =
      position.start.y < position.end.y ||
      (position.start.y === position.end.y && position.start.x <= position.end.x);
    const first = forward ? position.start : position.end;
    const last = forward ? position.end : position.start;

    const byLine = new Map<number, string>();
    for (let y = first.y; y <= last.y; y++) {
      const row = rowMap[y];
      if (!row) {
        continue;
      }
      const from = y === first.y ? utf16IndexAtCell(row.text, first.x) : 0;
      const to = y === last.y ? utf16IndexAtCell(row.text, last.x) : row.text.length;
      const sourceLine = row.line.lineNumber;
      byLine.set(sourceLine, (byLine.get(sourceLine) ?? '') + row.text.slice(from, to));
    }

    const rows = [...byLine.entries()]
      .sort((a, b) => a[0] - b[0])
      .map(([lineNumber, text]) => ({ line: lineNumber, text }));
    const text = rows.map((row) => row.text).join('\n');
    const fullLineText =
      rows.length === 1
        ? (rowMap.find((entry) => entry.line.lineNumber === rows[0].line)?.line.text ?? '')
        : '';
    // A row range is line based; only a native column selection is a portion.
    const portion =
      !rowRange && rows.length === 1 && text.length > 0 && text !== fullLineText
        ? { line: rows[0].line, text }
        : null;
    const range = rowRange
      ? { start: rowRange.start, end: rowRange.end }
      : rows.length > 0
        ? { start: rows[0].line, end: rows[rows.length - 1].line }
        : null;
    return { focusLine: line, focusText: lineText, range, rows, portion };
  }

  function rowIndexAt(clientY: number): number {
    if (!term) {
      return 0;
    }
    const screen = term.element?.querySelector('.xterm-screen') as HTMLElement | null;
    const rect = screen?.getBoundingClientRect();
    const rowHeight = rect && term.rows > 0 ? rect.height / term.rows : 18;
    return clamp(
      Math.floor((clientY - (rect?.top ?? 0)) / rowHeight),
      0,
      Math.max(0, term.rows - 1),
    );
  }

  function sourceLineAtRow(rowIndex: number): number | null {
    return rowMap[rowIndex]?.line.lineNumber ?? null;
  }

  function visibleRowsOf(startLine: number, endLine: number): { firstRow: number; lastRow: number } {
    let firstRow = Number.MAX_SAFE_INTEGER;
    let lastRow = -1;
    for (let index = 0; index < rowMap.length; index++) {
      const lineNumber = rowMap[index].line.lineNumber;
      if (lineNumber >= startLine && lineNumber <= endLine) {
        firstRow = Math.min(firstRow, index);
        lastRow = Math.max(lastRow, index);
      }
    }
    return { firstRow, lastRow };
  }

  // Re-applies the anchored selection to whatever part of it is on screen. A
  // fully scrolled-out range has no visible row: the highlight is dropped but the
  // controller keeps the range, so scrolling back restores it.
  function applyRowRange(range: SelectionRange | null): void {
    if (!term) {
      return;
    }
    if (!range) {
      term.clearSelection();
      updateSelection();
      return;
    }
    const start = Math.min(range.base, range.focus);
    const end = Math.max(range.base, range.focus);
    const { firstRow, lastRow } = visibleRowsOf(start, end);
    if (firstRow <= lastRow) {
      term.selectLines(firstRow, lastRow);
    } else {
      term.clearSelection();
    }
    updateSelection();
  }

  // A within-row drag selection is the same key a double click uses: every
  // visible occurrence of the selected text is highlighted. It is applied on
  // release, not while the pointer moves, so the overlay never chases the
  // selection cell by cell.
  function applySelectionWord(): void {
    // A row gesture keeps its own range: only xterm's native column selection
    // can be a word.
    if (!term || selection?.currentRange) {
      return;
    }
    // A selection that stays inside one visual row is a word; a taller one is a
    // row range.
    const position = term.getSelectionPosition();
    if (!position || position.start.y !== position.end.y) {
      return;
    }
    // The selection is padded to the terminal width; trimming drops the blanks
    // the user did not mean to match.
    const word = selectionWord(selectedText());
    if (word && word !== highlightWord) {
      dispatch('wordHighlight', word);
    }
  }

  // The line at the edge the drag is scrolling towards, or null without rows.
  function edgeLine(direction: number): number | null {
    if (!rowMap.length) {
      return null;
    }
    return direction > 0
      ? rowMap[rowMap.length - 1].line.lineNumber
      : rowMap[0].line.lineNumber;
  }

  function endpointText(lineNumber: number): string {
    return lineTextCache.get(lineNumber);
  }

  function createSelection(): RowSelectionController {
    const surface: SelectionSurface = {
      mode: 'rows-or-native',
      element: host,
      // The main view may start a row drag in the empty area below the last
      // line, matching the old mouse handler.
      allowEmptyStart: true,
      keyAt: (clientX, clientY) => {
        const rect = host.getBoundingClientRect();
        if (
          clientX < rect.left ||
          clientX > rect.right ||
          clientY < rect.top ||
          clientY > rect.bottom
        ) {
          return null;
        }
        return sourceLineAtRow(rowIndexAt(clientY));
      },
      viewportRect: () => host.getBoundingClientRect(),
      scrollBy: (direction, step) => {
        const target = clamp(firstLine + direction * step, 0, Math.max(0, totalLines - 1));
        if (target === firstLine) {
          // Both edges of the file: keep the selection, stop scrolling.
          return false;
        }
        dispatch('lineChange', target);
        return true;
      },
      edgeKey: (direction) => edgeLine(direction),
      endpointText: (line) => endpointText(line),
      applyRange: (range) => applyRowRange(range),
      clearNativeSelection: () => term?.clearSelection(),
      onCurrentKeyChange: (line) => dispatch('currentLineChange', line),
      onClick: (line) => {
        term?.focus();
        dispatch('currentLineChange', line);
        // xterm's own click handler runs after our capture listener; restore
        // the row highlight on the next task.
        const range = selection?.currentRange;
        if (range) {
          window.setTimeout(() => {
            if (selection?.currentRange === range) {
              applyRowRange(range);
            }
          }, 0);
        }
      },
      onDoubleClick: (_line, event) => onDoubleClick(event),
      onGestureEnd: (moved) => {
        if (moved) {
          applySelectionWord();
        }
      },
      onRowGestureEnd: (range) => {
        if (!range) {
          return;
        }
        // Let xterm receive mouseup/click so its internal drag state resets,
        // then restore the row selection after its click handler has run.
        window.setTimeout(() => {
          if (selection?.currentRange === range) {
            applyRowRange(range);
          }
        }, 0);
      },
    };
    return new RowSelectionController({ surface });
  }

  function onDoubleClick(event: MouseEvent): void {
    if (!term || event.button !== 0) {
      return;
    }
    const rowIndex = rowIndexAt(event.clientY);
    const row = rowMap[rowIndex];
    if (!row) {
      return;
    }

    const screen = term.element?.querySelector('.xterm-screen') as HTMLElement | null;
    if (!screen || term.cols <= 0) {
      return;
    }
    const rect = screen.getBoundingClientRect();
    const cellWidthPx = rect.width / term.cols;
    if (cellWidthPx <= 0) {
      return;
    }

    const clickCell = clamp(Math.floor((event.clientX - rect.left) / cellWidthPx), 0, term.cols - 1);
    const text = row.text;
    const clickIndex = Math.min(utf16IndexAtCell(text, clickCell), text.length);
    const clickChar = text.slice(clickIndex).match(/^./u)?.[0] ?? '';
    const isWord = (character: string) => /[\p{L}\p{N}\p{Pc}]/u.test(character);
    if (!clickChar || !isWord(clickChar)) {
      dispatch('wordHighlight', '');
      return;
    }

    const startMatch = text.slice(0, clickIndex).match(/[\p{L}\p{N}\p{Pc}]+$/u);
    const endMatch = text.slice(clickIndex).match(/^[\p{L}\p{N}\p{Pc}]+/u);
    const startIndex = startMatch ? clickIndex - startMatch[0].length : clickIndex;
    const endIndex = clickIndex + (endMatch?.[0].length ?? 0);
    const startCell = textCells(text.slice(0, startIndex));
    const lengthCells = textCells(text.slice(startIndex, endIndex));
    if (lengthCells === 0) {
      dispatch('wordHighlight', '');
      return;
    }

    dispatch('wordHighlight', text.slice(startIndex, endIndex));
    // Our single-click and multi-line selection handlers run before and after
    // the native dblclick; reapply xterm's word selection last.
    window.setTimeout(() => {
      // A word selection is not line anchored; drop the row range so a later
      // re-render cannot resurrect the previous line selection.
      selection?.clear();
      term?.select(startCell, rowIndex, lengthCells);
      dispatch('currentLineChange', row.line.lineNumber);
    }, 0);
  }

  function onContextMenu(event: MouseEvent): void {
    event.preventDefault();
    if (!term) {
      return;
    }
    host.focus();

    const rowIndex = rowIndexAt(event.clientY);
    const clicked = rowMap[rowIndex] ?? rowMap[rowMap.length - 1];
    const line = clicked?.line.lineNumber ?? firstLine + rowIndex;
    const lineText = clicked?.line.text ?? clicked?.text ?? '';
    if (clicked) {
      dispatch('currentLineChange', line);
    }
    let payload = selectionPayload(line, lineText);
    if (
      !selectionHasSelection(payload) ||
      line < selectionStartLine(payload) ||
      line > selectionEndLine(payload)
    ) {
      selection?.selectKey(line);
      payload = selectionPayload(line, lineText);
    }

    dispatch('contextMenu', { x: event.clientX, y: event.clientY, ...payload });
  }

  export function selectAll(): void {
    if (!term || totalLines <= 0) {
      term?.selectAll();
      return;
    }
    // The desktop view selects the whole file, not just the visible window.
    selection?.selectRange(0, totalLines - 1);
  }

  /** Selects a whole line and makes it current, like a keyboard move in the desktop view. */
  export function selectLine(line: number): void {
    selection?.selectKey(line);
    dispatch('currentLineChange', line);
  }

  /**
   * Lines the desktop "copy with color" would write for this view: the row
   * range, else the native/column selection, else the current line.
   */
  export function copyTargetLines(): number[] {
    const range = selection?.currentRange;
    if (range) {
      const start = Math.min(range.base, range.focus);
      const end = Math.max(range.base, range.focus);
      const lines: number[] = [];
      for (let line = start; line <= end; line++) {
        lines.push(line);
      }
      return lines;
    }
    const payload = selectionPayload(currentLine ?? firstLine, '');
    if (selectionHasRows(payload)) {
      return selectionLines(payload);
    }
    if (selectionHasSelection(payload)) {
      return [selectionStartLine(payload)];
    }
    const line = currentLine ?? firstLine;
    return totalLines > 0 ? [clamp(line, 0, totalLines - 1)] : [];
  }

  export function clearSelection(): void {
    selection?.clear();
    term?.clearSelection();
  }

  /** Whether the terminal owns the keyboard focus (per-view actions, copy). */
  export function hasFocus(): boolean {
    return !!host?.contains(document.activeElement);
  }

  function buildFrameData(rows: VisualRow[], cols: number, fillRows = rows.length): string {
    let out = '\x1b[0m\x1b[H';
    const count = Math.max(rows.length, fillRows);
    for (let i = 0; i < count; i++) {
      const row = rows[i];
      if (row) {
        out += rowAnsi(row, cols);
      }
      out += '\x1b[K';
      if (i < count - 1) {
        out += '\r\n';
      }
    }
    return out;
  }

  function layoutGutter() {
    if (!gutterEl || !term) {
      return;
    }
    const screen = term.element?.querySelector('.xterm-screen') as HTMLElement | null;
    const height = screen ? screen.getBoundingClientRect().height : 0;
    const rowHeight = height > 0 && term.rows > 0 ? height / term.rows : 18;

    gutterEl.innerHTML = rowMap
      .map((row) => {
        const line = row.line;
        const number = row.partIndex === 0 ? line.displayLineNumber : '↳';
        const flags = line.flags;
        let marks = '';
        if (flags & LineFlagMark) marks += '<span class="flag mark">●</span>';
        if (flags & LineFlagMatch) marks += '<span class="flag match">◆</span>';
        if (flags & LineFlagEvent) marks += '<span class="flag event">▮</span>';
        const cls = row.partIndex === 0 ? 'gutter-num' : 'gutter-cont';
        const current = currentLine !== null && line.lineNumber === currentLine;
        const marked = (line.flags & LineFlagMark) !== 0;
        return `<div class="gutter-row${marked ? ' marked' : ''}${current ? ' current' : ''}" data-line="${line.lineNumber}" style="height:${rowHeight}px;line-height:${rowHeight}px">
          <span class="${cls}">${number}</span><span class="marks">${marks}</span></div>`;
      })
      .join('');
  }

  function layoutCurrentLine() {
    if (!currentLineEl || !term) {
      return;
    }
    if (currentLine === null || rowMap.length === 0) {
      currentLineEl.style.display = 'none';
      return;
    }
    const indices: number[] = [];
    for (let index = 0; index < rowMap.length; index++) {
      if (rowMap[index].line.lineNumber === currentLine) {
        indices.push(index);
      }
    }
    if (indices.length === 0) {
      currentLineEl.style.display = 'none';
      return;
    }
    const screen = term.element?.querySelector('.xterm-screen') as HTMLElement | null;
    if (!screen) {
      currentLineEl.style.display = 'none';
      return;
    }
    const screenRect = screen.getBoundingClientRect();
    const hostRect = host.getBoundingClientRect();
    const rowHeight = term.rows > 0 ? screenRect.height / term.rows : 18;
    const first = indices[0];
    const last = indices[indices.length - 1];
    currentLineEl.style.display = 'block';
    currentLineEl.style.top = `${screenRect.top - hostRect.top + first * rowHeight}px`;
    currentLineEl.style.height = `${(last - first + 1) * rowHeight}px`;
    currentLineEl.style.left = `${screenRect.left - hostRect.left}px`;
    currentLineEl.style.width = `${screenRect.width}px`;
  }

  function layoutMarkedLines() {
    if (!markedLinesEl || !term) {
      return;
    }
    if (rowMap.length === 0) {
      markedLinesEl.replaceChildren();
      return;
    }
    const screen = term.element?.querySelector('.xterm-screen') as HTMLElement | null;
    if (!screen) {
      markedLinesEl.replaceChildren();
      return;
    }
    const screenRect = screen.getBoundingClientRect();
    const hostRect = host.getBoundingClientRect();
    const rowHeight = term.rows > 0 ? screenRect.height / term.rows : 18;

    const runs: { first: number; last: number }[] = [];
    rowMap.forEach((row, index) => {
      if ((row.line.flags & LineFlagMark) === 0) {
        return;
      }
      const previous = runs[runs.length - 1];
      if (previous && previous.last === index - 1) {
        previous.last = index;
      } else {
        runs.push({ first: index, last: index });
      }
    });

    markedLinesEl.replaceChildren(
      ...runs.map((run) => {
        const row = document.createElement('div');
        row.className = 'marked-line-overlay-row';
        row.style.top = `${screenRect.top - hostRect.top + run.first * rowHeight}px`;
        row.style.height = `${(run.last - run.first + 1) * rowHeight}px`;
        row.style.left = `${screenRect.left - hostRect.left}px`;
        row.style.width = `${screenRect.width}px`;
        return row;
      }),
    );
  }

  function layoutWordHighlights() {
    if (!wordHitsEl || !term) {
      return;
    }
    const word = highlightWord.trim();
    if (!word || rowMap.length === 0) {
      wordHitsEl.replaceChildren();
      return;
    }
    const screen = term.element?.querySelector('.xterm-screen') as HTMLElement | null;
    if (!screen) {
      wordHitsEl.replaceChildren();
      return;
    }
    const screenRect = screen.getBoundingClientRect();
    const hostRect = host.getBoundingClientRect();
    const rowHeight = term.rows > 0 ? screenRect.height / term.rows : 18;
    const cellWidth = term.cols > 0 ? screenRect.width / term.cols : 0;
    if (cellWidth <= 0) {
      wordHitsEl.replaceChildren();
      return;
    }

    const hits: HTMLDivElement[] = [];
    const wordFont =
      `${term.options.fontWeight ?? 'normal'} ${term.options.fontSize ?? fontSize}px ` +
      (term.options.fontFamily ?? fontFamily);
    rowMap.forEach((row, rowIndex) => {
      let from = 0;
      while (from <= row.text.length - word.length) {
        const index = row.text.indexOf(word, from);
        if (index < 0) {
          break;
        }
        const startCell = textCells(row.text.slice(0, index));
        const lengthCells = textCells(row.text.slice(index, index + word.length));
        if (lengthCells > 0) {
          const hit = document.createElement('div');
          hit.className = 'word-hit-overlay';
          // The overlay paints the word itself, otherwise the label or search
          // colours under it would still show through the solid background.
          hit.textContent = row.text.slice(index, index + word.length);
          hit.style.top = `${screenRect.top - hostRect.top + rowIndex * rowHeight}px`;
          hit.style.left = `${screenRect.left - hostRect.left + startCell * cellWidth}px`;
          hit.style.width = `${lengthCells * cellWidth}px`;
          hit.style.height = `${rowHeight}px`;
          hit.style.lineHeight = `${rowHeight}px`;
          hit.style.font = wordFont;
          hits.push(hit);
        }
        from = index + word.length;
      }
    });
    wordHitsEl.replaceChildren(...hits);
  }

  const emptySelection = { text: '', range: '', lines: [] as string[], lineCount: 0 };

  /**
   * Selected text without the frame's padding.
   *
   * Every row is padded to the terminal width so the background is uniform, and
   * that padding ends up in xterm's selection. It is not part of what the user
   * selected: copying it, searching for it or using it as a colour-label key
   * would never match the line text shown by the other view.
   */
  function selectedText(): string {
    return (term?.getSelection() ?? '').replace(/[ \t]+$/gm, '');
  }

  function updateSelection() {
    if (!term) {
      return;
    }
    // A row range is the source of truth even when no part of it is on screen;
    // the pinned endpoint texts keep the interval badge stable while scrolling.
    const rowSelection = selection?.snapshot();
    if (rowSelection) {
      dispatch('selection', {
        text: selectedText(),
        range: $t('selection.lines', { count: rowSelection.count.toLocaleString() }),
        lines: [rowSelection.startText, rowSelection.endText],
        lineCount: rowSelection.count,
      });
      return;
    }
    const position = term.getSelectionPosition();
    if (!position || !rowMap.length) {
      dispatch('selection', emptySelection);
      return;
    }
    const start = rowMap[position.start.y];
    const end = rowMap[position.end.y];
    if (!start || !end) {
      dispatch('selection', emptySelection);
      return;
    }
    const text = selectedText();
    // Whole rows in visual order: wrapped parts of one source line stay
    // separate entries, which keeps a timestamp in the first part findable.
    const firstRow = Math.min(position.start.y, position.end.y);
    const lastRow = Math.max(position.start.y, position.end.y);
    const visible = {
      lines: rowMap.slice(firstRow, lastRow + 1).map((row) => row.text),
      lineCount: Math.abs(end.line.lineNumber - start.line.lineNumber) + 1,
    };
    const wholeLine =
      visible.lineCount === 1 && position.start.x === 0 && position.end.x >= term.cols;
    const range =
      visible.lineCount > 1 || wholeLine
        ? $t('selection.lines', { count: Math.max(1, visible.lineCount).toLocaleString() })
        : $t('selection.chars', {
            count: Array.from(text).length.toLocaleString(),
          });
    dispatch('selection', { text, range, ...visible });
  }

  function onGutterClick(event: MouseEvent) {
    const target = (event.target as HTMLElement | null)?.closest('.gutter-row') as HTMLElement | null;
    if (!target) {
      return;
    }
    const line = Number(target.dataset.line);
    if (Number.isFinite(line)) {
      dispatch('markToggle', line);
    }
  }

  function onWheel(event: WheelEvent) {
    if (!term) {
      return;
    }
    if (event.ctrlKey || event.metaKey) {
      event.preventDefault();
      event.stopPropagation();
      const delta = event.deltaMode === 1 ? event.deltaY * 16 : event.deltaY;
      if (delta === 0) {
        return;
      }
      zoomAccumulator += delta;
      if (Math.abs(zoomAccumulator) < 40) {
        return;
      }
      // Wheel up grows the text, like the desktop view's changeFontSize.
      const zoomIn = zoomAccumulator < 0;
      zoomAccumulator = 0;
      dispatch(
        'fontSizeChange',
        clamp(fontSize + (zoomIn ? 1 : -1), FONT_SIZE_MIN, FONT_SIZE_MAX),
      );
      return;
    }
    zoomAccumulator = 0;
    if (event.altKey) {
      event.preventDefault();
      event.stopPropagation();
      const direction = Math.sign(event.deltaY);
      if (direction !== 0) {
        const fastStep = Math.max(10, Math.round(term.rows / 2));
        dispatch(
          'lineChange',
          clamp(firstLine + direction * fastStep, 0, Math.max(0, totalLines - 1)),
        );
      }
      return;
    }
    const horizontal =
      event.shiftKey || Math.abs(event.deltaX) > Math.abs(event.deltaY);
    if (horizontal && !wrap) {
      event.preventDefault();
      event.stopPropagation();
      const delta = Math.sign(event.deltaX || event.deltaY || 0);
      if (delta !== 0) {
        hOffset = clamp(hOffset + delta * 4, 0, horizontalLimit);
        scheduleRender(true);
      }
      return;
    }
    if (Math.abs(event.deltaY) > 0) {
      event.preventDefault();
      event.stopPropagation();
      const direction = Math.sign(event.deltaY);
      dispatch(
        'lineChange',
        clamp(firstLine + direction * 3, 0, Math.max(0, totalLines - 1)),
      );
    }
  }

  function onKeyDown(event: KeyboardEvent) {
    if (!term || event.ctrlKey || event.metaKey || event.altKey) {
      return;
    }
    const page = Math.max(1, term.rows - 1);
    let target: number | null = null;
    switch (event.key) {
      case 'PageDown':
        target = firstLine + page;
        break;
      case 'PageUp':
        target = firstLine - page;
        break;
      case 'ArrowDown':
        target = firstLine + 1;
        break;
      case 'ArrowUp':
        target = firstLine - 1;
        break;
      case 'Home':
        target = 0;
        break;
      case 'End':
        // One line past the bottom, like the desktop scroll range.
        target = Math.max(0, totalLines - term.rows + 1);
        break;
      default:
        return;
    }
    event.preventDefault();
    event.stopPropagation();
    dispatch('lineChange', clamp(target, 0, Math.max(0, totalLines - 1)));
  }

  function hostSizeKey(): string {
    if (!host) {
      return '';
    }
    const rect = host.getBoundingClientRect();
    return `${Math.round(rect.width)}x${Math.round(rect.height)}`;
  }

  // Columns the render window shows; two are reserved for the line-number gutter.
  $: visibleColumns = Math.max(1, termCols - 2);
  // How far the first displayed column may move: keep a fallback while the file
  // length is unknown so the scroll bar still works.
  $: horizontalLimit =
    maxLineLength > 0
      ? Math.max(0, maxLineLength - visibleColumns + 1)
      : 4000;
  $: if (hOffset > horizontalLimit) {
    hOffset = horizontalLimit;
    scheduleRender(true);
  }

  function fitTerminal(): void {
    if (!term || !fitAddon || disposed) {
      return;
    }
    // A collapsed pane (splitter at 0/100) has no drawable area; fitting there
    // would clamp the terminal to one row and trigger a pointless re-render.
    const rect = host.getBoundingClientRect();
    if (rect.width < 10 || rect.height < 10) {
      return;
    }
    try {
      fitAddon.fit();
    } catch {
      return;
    }
    termCols = term.cols;
    lastHostSize = hostSizeKey();
    if (term.rows !== lastRows) {
      lastRows = term.rows;
      dispatch('rowsChange', term.rows);
    }
    layoutGutter();
    layoutCurrentLine();
    layoutMarkedLines();
    layoutWordHighlights();
    scheduleRender(true);
  }

  function scheduleTerminalFit(): void {
    if (resizeRenderTimer) {
      window.clearTimeout(resizeRenderTimer);
    }
    resizeRenderTimer = window.setTimeout(() => {
      resizeRenderTimer = 0;
      if (!dragging && hostSizeKey() !== lastHostSize) {
        fitTerminal();
      }
    }, 200);
  }

  export function fitAfterResize(): void {
    if (resizeRenderTimer) {
      window.clearTimeout(resizeRenderTimer);
      resizeRenderTimer = 0;
    }
    fitTerminal();
  }

  function setLoading(value: boolean): void {
    loading = value;
    if (value) {
      if (hasRendered) {
        return;
      }
      if (!loadingTimer) {
        loadingTimer = window.setTimeout(() => {
          loadingTimer = 0;
          if (loading && !hasRendered) {
            loadingVisible = true;
          }
        }, 300);
      }
    } else {
      if (loadingTimer) {
        window.clearTimeout(loadingTimer);
        loadingTimer = 0;
      }
      loadingVisible = false;
    }
  }

  async function render() {
    if (!term || !api || fileId === null || disposed) {
      return;
    }

    const cols = Math.max(20, term.cols - 2);
    const termRows = Math.max(4, term.rows);
    const wrapCells = wrap ? cols : 0;
    const visibleLines = Math.min(2000, termRows + (wrap ? 24 : 6));
    const visibleColumns = wrap ? 0 : Math.max(cols * 2, 200);
    const seq = ++requestSeq;
    const renderStartedAt = performance.now();
    setLoading(true);
    errorText = '';

    const options: Record<string, unknown> = {
      wrapColumns: wrapCells,
      firstColumn: wrap ? 0 : hOffset,
      visibleColumns,
      searchStartIndex,
      searchEndIndex,
      firstDisplayLineNumber: firstLine + 1,
      ansiProcessing: 'renderColors',
      palette: {
        text: palette.text,
        base: palette.base,
        dimmedText: palette.dimmed,
        selectionText: palette.selectionText,
        selectionBack: palette.selectionBack,
      },
      quickHighlighterWords: quickWords,
    };

    if (pattern) {
      options.patternHighlight = {
        pattern,
        caseSensitive: searchCase,
        useRegex: searchRegex,
        variateColors: false,
        foreColor: palette.matchFore,
        backColor: palette.matchBack,
      };
    }

    try {
      const result = await api.renderViewport(
        fileId,
        searchId,
        firstLine,
        visibleLines,
        options,
      );
      if (disposed || seq !== requestSeq || !term) {
        return;
      }

      const visual = buildRows(result.lines, wrapCells, wrap ? 0 : hOffset, cols);
      const rows = visual.slice(0, termRows);
      rowMap = rows;
      for (const row of rows) {
        lineTextCache.set(row.line.lineNumber, row.line.text);
      }
      // Fill the whole terminal: the desktop lets the last line scroll one row up,
    // so the trailing row must be cleared instead of keeping the previous frame.
    const data = buildFrameData(rows, cols, termRows);
      term.write(`\x1b[?2026h${data}\x1b[?2026l`, () => {
        if (disposed) {
          return;
        }
        layoutGutter();
        layoutCurrentLine();
        layoutMarkedLines();
        layoutWordHighlights();
        // A drag that is auto-scrolling grows to the row the scroll revealed;
        // otherwise the row range is re-applied to the frame that was just
        // written. A native column selection has no row range and is left alone.
        selection?.handleSurfaceFrame();
        const rowRange = selection?.currentRange;
        if (rowRange) {
          applyRowRange(rowRange);
        } else {
          updateSelection();
        }
        if (rows.length > 0) {
          hasRendered = true;
        }
        const sampleMs = performance.now() - renderStartedAt;
        renderCostMs = renderCostMs > 0 ? renderCostMs * 0.7 + sampleMs * 0.3 : sampleMs;
        setLoading(false);
        dispatch('ready');
      });
    } catch (error) {
      if (disposed || seq !== requestSeq) {
        return;
      }
      const sampleMs = performance.now() - renderStartedAt;
      renderCostMs = renderCostMs > 0 ? renderCostMs * 0.7 + sampleMs * 0.3 : sampleMs;
      setLoading(false);
      errorText = error instanceof ApiError ? error.message : String(error);
      dispatch('error', errorText);
    }
  }

  function onFileChanged() {
    if (!term) {
      return;
    }
    if (fileId === null) {
      selection?.reset();
      lineTextCache.clear();
      term.clear();
      rowMap = [];
      if (gutterEl) gutterEl.innerHTML = '';
      if (currentLineEl) currentLineEl.style.display = 'none';
      dispatch('selection', emptySelection);
    }
  }

  onMount(() => {
    term = new Terminal({
      cols: 120,
      rows: 30,
      scrollback: 0,
      disableStdin: true,
      cursorBlink: false,
      convertEol: false,
      fontSize,
      lineHeight: 1.2,
      fontFamily,
      fontWeight: boldFont ? 'bold' : 'normal',
      theme: terminalTheme,
    });

    fitAddon = new FitAddon();
    term.loadAddon(fitAddon);
    term.open(host);
    // The WebGL renderer can leave a stale half-canvas offset (or a blank
    // viewport) after rapid splitter/window resizes on some Chromium/DPR
    // configurations. The DOM renderer keeps the same ~55 fps at the viewport
    // sizes used here and does not have that resize failure mode.
    term.onSelectionChange(updateSelection);
    selection = createSelection();
    selection.attach();

    // xterm handles wheel on its own screen and stops propagation there; capture
    // at the viewport so both the log and the gutter stay Core-backed.
    viewportEl.addEventListener('wheel', onWheel, { passive: false, capture: true });
    host.addEventListener('keydown', onKeyDown, true);
    host.addEventListener('contextmenu', onContextMenu);
    gutterEl?.addEventListener('click', onGutterClick);

    resizeObserver = new ResizeObserver(() => {
      if (dragging || hostSizeKey() === lastHostSize) {
        return;
      }
      scheduleTerminalFit();
    });
    resizeObserver.observe(host);

    fitTerminal();

    (window as any).__kloggViewport = {
      get term() {
        return term;
      },
      get rowMap() {
        return rowMap;
      },
      get hOffset() {
        return hOffset;
      },
      render,
      fitTerminal,
      selectAll,
      clearSelection,
      get selection() {
        return selectionPayload(currentLine ?? firstLine, '');
      },
    };

    return () => {
      disposed = true;
      selection?.detach();
      selection = null;
      if (renderFrame) {
        window.cancelAnimationFrame(renderFrame);
      }
      if (resizeRenderTimer) {
        window.clearTimeout(resizeRenderTimer);
      }
      if (liveRenderTimer) {
        window.clearTimeout(liveRenderTimer);
        liveRenderTimer = 0;
      }
      if (loadingTimer) {
        window.clearTimeout(loadingTimer);
        loadingTimer = 0;
      }
      resizeObserver?.disconnect();
      viewportEl.removeEventListener('wheel', onWheel, true);
      host.removeEventListener('keydown', onKeyDown, true);
      host.removeEventListener('contextmenu', onContextMenu);
      gutterEl?.removeEventListener('click', onGutterClick);
      term?.dispose();
      term = null;
      delete (window as any).__kloggViewport;
    };
  });

  $: if (term) {
    void fileId;
    if (fileId !== lastFileId) {
      lastFileId = fileId ?? -1;
      hOffset = 0;
      hasRendered = false;
      loadingVisible = false;
      // Line numbers of the previous file mean nothing here.
      selection?.reset();
      lineTextCache.clear();
      onFileChanged();
      scheduleRender(true);
    }
  }

  $: if (term && terminalTheme) {
    term.options.theme = terminalTheme;
  }

  $: if (term) {
    const weight = boldFont ? 'bold' : 'normal';
    if (
      term.options.fontSize !== fontSize ||
      term.options.fontFamily !== fontFamily ||
      term.options.fontWeight !== weight
    ) {
      term.options.fontSize = fontSize;
      term.options.fontFamily = fontFamily;
      term.options.fontWeight = weight;
      fitTerminal();
    }
  }

  $: if (term) {
    void currentLine;
    void quickWords;
    layoutCurrentLine();
    layoutGutter();
    layoutMarkedLines();
    layoutWordHighlights();
    scheduleRender();
  }

  $: if (term) {
    void highlightWord;
    layoutWordHighlights();
  }

  $: if (term) {
    void showLineNumbers;
    void tick().then(() => {
      fitTerminal();
    });
  }

  $: searchStartIndex = searchFromLine ?? 0;
  $: searchEndIndex = searchToLine !== null ? searchToLine + 1 : Math.max(1, totalLines);

  // User-driven frame changes render on the next animation frame; live growth
  // is coalesced so a fast-appending file cannot keep the viewport reloading.
  $: if (term && fileId !== null) {
    void firstLine;
    void wrap;
    void searchId;
    void pattern;
    void searchRegex;
    void searchCase;
    void searchStartIndex;
    void palette;
    scheduleRender();
  }

  $: if (term && fileId !== null) {
    void searchEndIndex;
    void totalLines;
    void frameRevision;
    scheduleLiveRender();
  }
</script>

<div
  class="viewport"
  class:no-wrap={!wrap}
  class:hide-line-numbers={!showLineNumbers}
  bind:this={viewportEl}
>
  <div class="gutter" bind:this={gutterEl}></div>
  <!-- svelte-ignore a11y_no_noninteractive_tabindex -->
  <div
    class="terminal-host"
    bind:this={host}
    tabindex="0"
    role="application"
    aria-label={$t('logView.aria')}
  >
    <div class="word-hits-overlay" bind:this={wordHitsEl}></div>
    <div class="marked-lines-overlay" bind:this={markedLinesEl}></div>
    <div class="current-line-overlay" bind:this={currentLineEl}></div>
  </div>

  {#if loadingVisible}
    <div class="viewport-loading">{$t('logView.loading')}</div>
  {/if}
  {#if errorText}
    <div class="viewport-loading" style="color:var(--red)">{errorText}</div>
  {/if}

  {#if !wrap}
    <div class="hscroll">
      <span>{$t('logView.column')}</span>
      <input
        type="range"
        min="0"
        max={horizontalLimit}
        bind:value={hOffset}
        on:input={() => scheduleRender(true)}
      />
      <span>{hOffset}</span>
    </div>
  {/if}
</div>
