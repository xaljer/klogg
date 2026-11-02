<script lang="ts">
  import { createEventDispatcher, onMount, tick } from 'svelte';
  import { t } from '../lib/i18n';
  import type { ApiClient } from '../lib/api';
  import { RowSelectionController } from '../lib/selection/selection-controller';
  import type { SelectionSurface } from '../lib/selection/selection-surface';
  import type { SelectionRange, SelectionSnapshot } from '../lib/selection/types';
  import { selectionWord } from '../lib/selection/selection-word';
  import { applyHighlightRules, compileRules } from '../lib/highlight-apply';
  import { createLineTextCache } from '../lib/line-text-cache';
import { FONT_SIZE_MAX, FONT_SIZE_MIN } from '../lib/settings';
  import { colorLabelRanges } from '../lib/color-labels';
  import { escapeRegExp } from '../lib/regex-escape';
  import {
    type SelectionContextRequest,
    type SelectionPayload,
  } from '../lib/selection/payload';
  import {
    centeredWindowStart,
    sortedIndexAtOrBefore,
    windowContains,
  } from '../lib/selection/projection-window';
  import { toHtmlColor } from '../lib/highlighters';
  import type { HighlightRule, LineData, QuickHighlighterDef } from '../lib/types';
  import { LineFlagEvent, LineFlagMark, LineFlagMatch } from '../lib/types';
  import { textCells } from '../lib/viewport-cells';
  import {
    indexAtScrollTop as mapScrollTopToIndex,
    pixelPerRowFor,
    scrollTopForIndex as mapIndexToScrollTop,
    virtualHeightFor,
  } from '../lib/virtual-scroll';

  export let api: ApiClient;
  export let fileId: number | null = null;
  export let searchId: number | null = null;
  export let visibility = 'marks-matches';
  export let pattern = '';
  export let regex = true;
  export let caseSensitive = false;
  // Bumped when the projection's content or identity changes: reload the window.
  export let projectionRevision = 0;
  // Source line the reloaded projection should open at (-1 keeps the window).
  // The pane owns the cursor (desktop CrawlerWidget::currentLineNumber_).
  export let repositionLine = -1;
  // A live search refresh keeps the previous rows visible while the new result
  // window loads; a user-initiated search clears them immediately.
  export let preserveOnRefresh = false;
  export let displayLimit: number | null = null;
  export let wrap = false;
  export let follow = false;
  export let fontSize = 13;
  export let fontFamily = '"Cascadia Mono", Consolas, monospace';
  export let boldFont = false;
  export let showLineNumbers = true;
  export let highlightWord = '';
  // Active highlight rules, applied here with the same ordering as the desktop
  // engine so both panes color a line the same way.
  export let highlightRules: HighlightRule[] = [];
  // Colour labels are shared with the main view; they are composed here in the
  // same order the core uses (after the search highlight, before the word hit).
  export let quickDefs: QuickHighlighterDef[] = [];
  export let quickWords: string[][] = [];

  const dispatch = createEventDispatcher<{
    selectLine: number;
    selection: { lines: string[]; lineCount: number };
    wordHighlight: string;
    fontSizeChange: number;
    contextMenu: SelectionContextRequest;
  }>();

  const ROW_HEIGHT = 20;
  const PAGE = 200;
  // Chromium clamps element heights around 2^25 px; keep a safe margin so a
  // million-match projection still has a usable scrollbar.
  const MAX_SCROLL_HEIGHT = 8_000_000;

  let total = 0;
  let loadedFrom = 0;
  let rows: { num: number; text: string; flags: number }[] = [];
  let allMarks: number[] = [];
  let loading = false;
  let error = '';
  // The row this view paints as current. Unlike the pane's shared cursor it is
  // this view's own selection, the way AbstractLogView::selection_ is: reloads
  // reset it, and the pane tells it where to reopen via repositionLine.
  let selectedLine = -1;
  // Absolute indices in the filtered result, not positions in the loaded window:
  // scrolling reloads the window and the selection has to survive that.
  let selection: RowSelectionController | null = null;
  let selectionRange: SelectionRange | null = null;
  let scrollEl: HTMLDivElement;
  let viewportHeight = 0;
  // Client height of the scroll container as last reported by its observer: a
  // horizontal scrollbar changes it after the rows render, which moves the
  // bottom of the list out of the anchored scroll position.
  let lastScrollClientHeight = 0;
  let resizeObserver: ResizeObserver | null = null;
  let loadSeq = 0;
  let countSeq = 0;
  let scrollTimer = 0;
  let charWidth = 7.2;
  let columns = 80;
  let zoomAccumulator = 0;

  function onWheel(event: WheelEvent): void {
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
        Math.max(
          FONT_SIZE_MIN,
          Math.min(FONT_SIZE_MAX, fontSize + (zoomIn ? 1 : -1)),
        ),
      );
      return;
    }
    zoomAccumulator = 0;
    if (event.altKey) {
      event.preventDefault();
      event.stopPropagation();
      const direction: -1 | 1 = Math.sign(event.deltaY) >= 0 ? 1 : -1;
      if (event.deltaY !== 0) {
        stepScroll(direction, Math.max(1, Math.round(scrollEl.clientHeight / averageRowHeight) - 1));
      }
    }
  }

  function rowHeightFor(row: { text: string }, cols = columns): number {
    if (!wrap) {
      return ROW_HEIGHT;
    }
    return Math.max(ROW_HEIGHT, Math.ceil((textCells(row.text) + 2) / Math.max(20, cols)) * ROW_HEIGHT);
  }

  $: averageRowHeight =
    wrap && rows.length > 0
      ? rows.reduce((sum, row) => sum + rowHeightFor(row, columns), 0) / rows.length
      : ROW_HEIGHT;
  // Virtual height is capped, so the scrollbar maps (approximately when the
  // cap is hit) to result indices instead of raw pixel offsets. This keeps the
  // projection usable for millions of matches without hitting the browser's
  // maximum element height.
  $: virtualHeight = virtualHeightFor(total, averageRowHeight, MAX_SCROLL_HEIGHT);
  $: pixelPerRow = pixelPerRowFor(total, virtualHeight, averageRowHeight);
  $: loadedRowsHeight = rows.length * averageRowHeight;
  // On the last page the top spacer absorbs the difference so the content ends
  // exactly at the virtual height: with wrapped rows the average-height model
  // would otherwise leave the tail rows past the reachable range.
  $: lastPageLoaded = total > 0 && loadedFrom + rows.length >= total;
  $: topSpacer = lastPageLoaded
    ? Math.max(0, virtualHeight - loadedRowsHeight)
    : Math.min(virtualHeight, Math.round(loadedFrom * pixelPerRow));
  $: bottomSpacer = Math.max(0, virtualHeight - topSpacer - loadedRowsHeight);

  function scrollState() {
    return {
      total,
      rowHeight: averageRowHeight,
      maxHeight: MAX_SCROLL_HEIGHT,
      loadedFrom,
      loadedCount: rows.length,
    };
  }

  function indexAtScrollTop(scrollTop: number): number {
    return mapScrollTopToIndex(scrollState(), scrollTop);
  }

  function scrollTopForIndex(index: number): number {
    return mapIndexToScrollTop(scrollState(), index);
  }

  function escapeHtml(text: string): string {
    return text
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }

  // Recompiled whenever the rules change, not once per rendered row.
  $: compiledRules = compileRules(highlightRules);

  function ruleStyle(span: { fore: string; back: string } | null): string {
    if (!span || (!span.fore && !span.back)) {
      return '';
    }
    const parts: string[] = [];
    if (span.fore) {
      parts.push(`color:${span.fore}`);
    }
    if (span.back) {
      parts.push(`background:${span.back}`);
    }
    return ` style="${parts.join(';')}"`;
  }

  // Svelte only re-renders the rows when a value used in the markup changes, so
  // the label state is passed in explicitly instead of being read from the
  // component scope: colour labels must show up as soon as they are applied.
  function highlight(
    text: string,
    word: string,
    slots: string[][],
    defs: QuickHighlighterDef[],
  ): string {
    const searchRanges: { start: number; end: number }[] = [];
    if (pattern) {
      try {
        const source = regex ? pattern : escapeRegExp(pattern);
        const matcher = new RegExp(source, caseSensitive ? 'g' : 'gi');
        for (const match of text.matchAll(matcher)) {
          if (!match[0]) {
            break;
          }
          const index = match.index ?? 0;
          searchRanges.push({ start: index, end: index + match[0].length });
        }
      } catch {
        // Invalid patterns fall back to the word highlight only.
      }
    }

    const wordRanges: { start: number; end: number }[] = [];
    const needle = word.trim();
    if (needle) {
      let from = 0;
      while (from <= text.length - needle.length) {
        const index = text.indexOf(needle, from);
        if (index < 0) {
          break;
        }
        wordRanges.push({ start: index, end: index + needle.length });
        from = index + needle.length;
      }
    }

    const ruleRanges = applyHighlightRules(text, compiledRules);
    const labelRanges = colorLabelRanges(slots, text, defs.length);

    if (
      searchRanges.length === 0 &&
      wordRanges.length === 0 &&
      ruleRanges.length === 0 &&
      labelRanges.length === 0
    ) {
      return escapeHtml(text);
    }

    const boundaries = new Set<number>([0, text.length]);
    for (const range of [...searchRanges, ...wordRanges, ...ruleRanges, ...labelRanges]) {
      boundaries.add(range.start);
      boundaries.add(range.end);
    }
    const sorted = [...boundaries].sort((left, right) => left - right);
    let result = '';
    for (let index = 0; index < sorted.length - 1; index++) {
      const start = sorted[index];
      const end = sorted[index + 1];
      if (end <= start) {
        continue;
      }
      const coveredByWord = wordRanges.some((range) => range.start < end && range.end > start);
      const coveredByLabel = labelRanges.find((range) => range.start <= start && range.end >= end);
      const coveredBySearch = searchRanges.some(
        (range) => range.start < end && range.end > start,
      );
      const ruleSpan =
        ruleRanges.find((range) => range.start <= start && range.end >= end) ?? null;
      const content = escapeHtml(text.slice(start, end));
      const style = ruleStyle(ruleSpan);
      if (coveredByWord) {
        result += `<span class="word-hit"${style}>${content}</span>`;
      } else if (coveredByLabel) {
        result += `<span class="color-label" style="${labelStyle(defs, coveredByLabel.label)}">${content}</span>`;
      } else if (coveredBySearch) {
        // The search match wins over highlight rules here, exactly like the
        // engine's render order; the colours come from the shared CSS pair.
        result += `<mark>${content}</mark>`;
      } else if (ruleSpan) {
        result += `<span${style}>${content}</span>`;
      } else {
        result += content;
      }
    }
    return result;
  }

  function labelStyle(defs: QuickHighlighterDef[], label: number): string {
    const def = defs[label];
    if (!def) {
      return '';
    }
    return `color:${toHtmlColor(def.color)};background:${toHtmlColor(def.backColor)}`;
  }

  function updateColumns(): void {
    if (!scrollEl) {
      return;
    }
    const canvas = document.createElement('canvas');
    const context = canvas.getContext('2d');
    if (context) {
      context.font = `${boldFont ? 'bold ' : ''}${Math.max(10, fontSize - 1)}px ${fontFamily}`;
      charWidth = context.measureText('0000000000').width / 10 || 7.2;
    }
    const textWidth = Math.max(120, scrollEl.clientWidth - 100);
    columns = Math.max(20, Math.floor(textWidth / charWidth));
  }

  let lastFontKey = '';
  $: if (scrollEl) {
    const fontKey = `${fontSize}|${fontFamily}|${boldFont}`;
    if (lastFontKey !== fontKey) {
      lastFontKey = fontKey;
      updateColumns();
    }
  }


  export function followToEnd(): void {
    if (total <= 0) {
      return;
    }
    void loadWindow(firstWindowIndex()).then(() => scrollToEnd());
  }

  /** Takes keyboard focus so the single-letter shortcuts and native scrolling work. */
  export function focusList(): void {
    scrollEl?.focus({ preventScroll: true });
  }

  /** Whether the list owns the keyboard focus (per-view actions, native copy). */
  export function hasFocus(): boolean {
    return !!scrollEl?.contains(document.activeElement);
  }

  /** Desktop Ctrl+A in the search view: every row of the result set. */
  export function selectAll(): void {
    if (total <= 0) {
      return;
    }
    scrollEl?.focus({ preventScroll: true });
    selection?.selectRange(0, total - 1);
  }

  /**
   * Text the colour-label shortcuts key on, like the desktop UI which labels the
   * focused view's selection: the native portion/word selection, else the first
   * selected row, else the current row.
   */
  export function selectionTextForLabel(): string {
    const native = window.getSelection();
    if (native && native.rangeCount > 0 && scrollEl?.contains(native.getRangeAt(0).startContainer)) {
      const text = native.toString();
      if (text.trim()) {
        return text;
      }
    }
    if (selectionRange) {
      const index = Math.min(selectionRange.base, selectionRange.focus) - loadedFrom;
      if (index >= 0 && index < rows.length) {
        return rows[index].text;
      }
    }
    if (selectedLine >= 0) {
      return rows.find((row) => row.num === selectedLine)?.text ?? '';
    }
    return '';
  }

  /**
   * Source lines the desktop "copy with color" would write for this view: the
   * selected rows of the loaded window, else the current row.
   */
  export function copyTargetLines(): number[] {
    if (selectionRange) {
      const start = Math.min(selectionRange.base, selectionRange.focus);
      const end = Math.max(selectionRange.base, selectionRange.focus);
      const selected: number[] = [];
      for (let index = Math.max(0, start - loadedFrom); index < rows.length; index++) {
        const absolute = loadedFrom + index;
        if (absolute > end) {
          break;
        }
        if (absolute >= start) {
          selected.push(rows[index].num);
        }
      }
      if (selected.length > 0) {
        return selected;
      }
    }
    if (selectedLine >= 0) {
      return [selectedLine];
    }
    return rows.length > 0 ? [rows[0].num] : [];
  }

  /**
   * File lines of the whole row selection, including rows outside the loaded
   * window. The desktop view copies/marks every selected row, so a full-range
   * selection (Ctrl+A) must map through the result set instead of the window.
   */
  export async function selectionTargetLines(): Promise<number[]> {
    if (!selectionRange) {
      return copyTargetLines();
    }
    const start = Math.min(selectionRange.base, selectionRange.focus);
    const end = Math.max(selectionRange.base, selectionRange.focus);
    const loadedEnd = loadedFrom + rows.length - 1;
    if (start >= loadedFrom && end <= loadedEnd) {
      return rows.slice(start - loadedFrom, end - loadedFrom + 1).map((row) => row.num);
    }
    if (searchId === null) {
      // Marks-only projection: the full mark list is already loaded.
      return allMarks.slice(start, end + 1);
    }
    const lines: number[] = [];
    const pageSize = Math.max(1, api.lineChunkSize);
    for (let from = start; from <= end; from += pageSize) {
      const count = Math.min(pageSize, end - from + 1);
      const id = searchId;
      const result = await api.searchMatches(id, from, count);
      lines.push(...result.matches);
      if (result.matches.length < count) {
        break;
      }
    }
    return lines;
  }

  // Follow mode opens the list on its newest page and keeps it pinned there
  // after a live search refresh.
  function firstWindowIndex(): number {
    return follow && total > PAGE ? total - PAGE : 0;
  }

  function scrollToEnd(): void {
    if (!scrollEl) {
      return;
    }
    const element = scrollEl;
    // Setting scrollTop to the content height lets the browser clamp it to the
    // real maximum, which is what the tail row needs: the client height changes
    // when a horizontal scrollbar appears with the newly rendered rows, and a
    // height read before that layout pass left the last row clipped.
    let frames = 0;
    const settle = (): void => {
      if (element !== scrollEl) {
        return;
      }
      element.scrollTop = element.scrollHeight;
      // A couple of frames to catch the scrollbar/native layout of the rows.
      if (frames++ < 3) {
        window.requestAnimationFrame(settle);
      }
    };
    settle();
  }

  /**
   * Loading the count and the window is a pair of round trips, and a search that
   * replaced the previous one invalidates both: a stale answer must not publish
   * its rows, and its failure must not replace a newer result with an error.
   */
  async function loadCount(): Promise<void> {
    const seq = ++countSeq;
    // The search this load belongs to: a cancelled search's answer must not
    // overwrite the result of the search that replaced it.
    const requestSearchId = searchId;
    // Where the user was before this load: the filtered view opens a new result
    // set at that source line instead of jumping back to the top.
    const previousLine = repositionLine >= 0 ? repositionLine : null;
    if (fileId === null) {
      total = 0;
      rows = [];
      loadedFrom = 0;
      selectedLine = -1;
      clearRowSelection();
      loading = false;
      error = '';
      return;
    }

    loading = true;
    error = '';
    const keepRows = preserveOnRefresh && rows.length > 0 && searchId !== null;
    if (!keepRows) {
      loadedFrom = 0;
      rows = [];
      selectedLine = -1;
      clearRowSelection();
      // Absolute indices of the previous result set mean nothing here. The
      // selection dispatch itself is driven by the cleared range, which reports
      // an empty selection.
      rowTextCache.clear();
      allMarks = [];
      if (scrollEl) {
        scrollEl.scrollTop = 0;
      }
    }

    try {
      if (searchId === null) {
        if (visibility.includes('marks')) {
          const result = await api.call<{ marks: number[] }>('marks.list', { fileId });
          if (seq !== countSeq) {
            return;
          }
          allMarks = result.marks;
          total = allMarks.length;
          if (total > 0) {
            await loadWindow(firstWindowIndex());
            if (seq !== countSeq) {
              return;
            }
            if (follow) {
              scrollToEnd();
            }
            if (selectedLine < 0 && rows.length > 0) {
              selectedLine = rows[0].num;
            }
          }
        } else {
          total = 0;
        }
        return;
      }

      const result = await api.call<{ lineCount: number }>('search.filteredLineCount', {
        searchId,
      });
      if (seq !== countSeq || searchId !== requestSearchId) {
        return;
      }
      error = '';
      const nextTotal =
        displayLimit !== null && displayLimit > 0
          ? Math.min(result.lineCount, displayLimit)
          : result.lineCount;
      if (nextTotal > 0) {
        let targetFrom = follow ? Math.max(0, nextTotal - PAGE) : 0;
        let revealIndex: number | null = null;
        if (keepRows) {
          targetFrom = follow
            ? Math.max(0, nextTotal - PAGE)
            : Math.min(loadedFrom, Math.max(0, nextTotal - rows.length));
        } else if (previousLine !== null && !follow) {
          // Desktop parity: a replaced search keeps the position of the result
          // the user was on.
          revealIndex = await projectionIndexForLine(previousLine);
          if (seq !== countSeq || searchId !== requestSearchId) {
            return;
          }
          if (revealIndex !== null) {
            targetFrom = centeredWindowStart(revealIndex, nextTotal, PAGE);
          }
        }
        // Publish the new total together with the window that matches it: a
        // larger spacer painted before its rows shows a blank gap for a frame.
        await loadWindow(targetFrom, nextTotal);
        if (seq !== countSeq) {
          return;
        }
        total = nextTotal;
        if (follow) {
          scrollToEnd();
        }
        if (revealIndex !== null) {
          revealRow(revealIndex);
        }
        if (selectedLine < 0 && rows.length > 0) {
          selectedLine = rows[0].num;
        }
      } else {
        total = 0;
        rows = [];
        loadedFrom = 0;
      }
    } catch (loadError) {
      if (seq !== countSeq || searchId !== requestSearchId) {
        return;
      }
      error = String(loadError);
      total = 0;
      rows = [];
    } finally {
      if (seq === countSeq) {
        loading = false;
      }
    }
  }

  async function loadWindow(fromIndex: number, totalOverride?: number): Promise<void> {
    const windowTotal = totalOverride ?? total;
    if (fileId === null || windowTotal <= 0) {
      return;
    }
    if (searchId === null) {
      await loadMarkWindow(fromIndex);
      return;
    }
    const seq = ++loadSeq;
    const requestSearchId = searchId;
    const from = Math.min(Math.max(0, fromIndex), Math.max(0, windowTotal - 1));
    const count = Math.min(PAGE, windowTotal - from);
    if (count <= 0) {
      rows = [];
      loadedFrom = from;
      return;
    }

    loading = true;
    try {
      const result = await api.call<{ lines: LineData[] }>('search.filteredLines', {
        searchId,
        fromIndex: from,
        count,
        ansi: 'strip',
      });
      if (seq !== loadSeq || searchId !== requestSearchId) {
        return;
      }
      const numbers = result.lines.map((line) => line.num);
      let flags: number[] = [];
      if (numbers.length) {
        try {
          const flagResult = await api.call<{ flags: number[] }>('flags.getBatch', {
            fileId,
            searchId,
            lines: numbers,
          });
          flags = flagResult.flags;
        } catch {
          flags = [];
        }
      }
      if (seq !== loadSeq || searchId !== requestSearchId) {
        return;
      }
      error = '';
      loadedFrom = from;
      rows = result.lines.map((line, index) => ({
        num: line.num,
        text: line.text,
        flags: flags[index] ?? 0,
      }));
      rowTextCache.setWindow(
        from,
        rows.map((row) => row.text),
      );
    } catch (loadError) {
      if (seq === loadSeq && searchId === requestSearchId) {
        error = String(loadError);
      }
    } finally {
      if (seq === loadSeq) {
        loading = false;
      }
    }
  }

  async function loadMarkWindow(fromIndex: number): Promise<void> {
    if (fileId === null || total <= 0) {
      return;
    }
    const seq = ++loadSeq;
    const from = Math.min(Math.max(0, fromIndex), Math.max(0, total - 1));
    const count = Math.min(PAGE, total - from);
    if (count <= 0) {
      rows = [];
      loadedFrom = from;
      return;
    }

    loading = true;
    try {
      const marks = allMarks.slice(from, from + count);
      const result = await api.call<{ lines: LineData[] }>('lines.getBatch', {
        fileId,
        lines: marks,
        ansi: 'strip',
      });
      let flags: number[] = [];
      try {
        const flagResult = await api.call<{ flags: number[] }>('flags.getBatch', {
          fileId,
          searchId: null,
          lines: marks,
        });
        flags = flagResult.flags;
      } catch {
        flags = [];
      }
      if (seq !== loadSeq) {
        return;
      }
      loadedFrom = from;
      rows = result.lines.map((line, index) => ({
        num: line.num,
        text: line.text,
        flags: flags[index] ?? 0,
      }));
      rowTextCache.setWindow(
        from,
        rows.map((row) => row.text),
      );
    } catch (loadError) {
      if (seq === loadSeq) {
        error = String(loadError);
      }
    } finally {
      if (seq === loadSeq) {
        loading = false;
      }
    }
  }

  /**
   * Index of the projection row at or before a source line. A search projection
   * is a union of matches, marks and events, so the session answers it; a
   * marks-only projection is the sorted mark list.
   */
  async function projectionIndexForLine(line: number): Promise<number | null> {
    if (searchId !== null) {
      try {
        const result = await api.filteredLineIndexAtOrBefore(searchId, line);
        return result.index;
      } catch {
        return null;
      }
    }
    return sortedIndexAtOrBefore(allMarks, line);
  }

  // Makes the row at an absolute projection index current and scrolls it in.
  // A load that replaced the window in between gets one retry.
  function revealRow(index: number): void {
    const loaded = rows[index - loadedFrom];
    if (!loaded) {
      void loadWindow(centeredWindowStart(index, total, PAGE)).then(() => {
        const retried = rows[index - loadedFrom];
        if (retried) {
          selectRow(retried.num, index);
        }
      });
      return;
    }
    selectRow(loaded.num, index);
  }

  function selectRow(line: number, index: number): void {
    selectedLine = line;
    void tick().then(() => {
      if (scrollEl) {
        scrollEl.scrollTop = scrollTopForIndex(index);
      }
    });
  }

  /**
   * Brings a source line into the filtered projection's view, so mark
   * navigation moves the search pane along with the main view.
   */
  export async function revealSourceLine(line: number): Promise<void> {
    if (total <= 0 || line < 0) {
      return;
    }
    const index = await projectionIndexForLine(line);
    if (index === null) {
      return;
    }
    if (!windowContains(loadedFrom, rows.length, index)) {
      await loadWindow(centeredWindowStart(index, total, PAGE));
    }
    revealRow(index);
  }

  function onScroll(): void {
    if (!scrollEl || total <= 0) {
      return;
    }
    // Dragging the thumb all the way down must land on the newest page, not on
    // an arbitrary window whose rows happen to contain the bottom pixel. A
    // transient spacer state during a window swap can make the container look
    // bottomed out, so the mapped index has to be in the last page as well.
    const lastFrom = total - PAGE;
    if (
      total > PAGE &&
      loadedFrom !== lastFrom &&
      indexAtScrollTop(scrollEl.scrollTop) >= lastFrom &&
      scrollEl.scrollTop + scrollEl.clientHeight >= scrollEl.scrollHeight - 2
    ) {
      if (scrollTimer) {
        window.clearTimeout(scrollTimer);
        scrollTimer = 0;
      }
      void loadWindow(lastFrom).then(() => scrollToEnd());
      return;
    }
    if (scrollTimer) {
      window.clearTimeout(scrollTimer);
    }
    scrollTimer = window.setTimeout(() => {
      const from = indexAtScrollTop(scrollEl.scrollTop);
      const visible = Math.ceil(viewportHeight / Math.max(1, averageRowHeight)) + 12;
      if (from < loadedFrom - 4 || from + visible > loadedFrom + rows.length + 4) {
        const targetFrom = Math.max(
          0,
          Math.min(from - 8, Math.max(0, total - PAGE)),
        );
        void loadWindow(targetFrom).then(() => {
          if (scrollEl) {
            scrollEl.scrollTop =
              topSpacer + Math.max(0, from - targetFrom) * averageRowHeight;
          }
        });
      }
    }, 30);
  }

  $: selectionFrom = selectionRange
    ? Math.min(selectionRange.base, selectionRange.focus)
    : -1;
  $: selectionTo = selectionRange
    ? Math.max(selectionRange.base, selectionRange.focus)
    : -1;

  const emptyRowSelection = { lines: [] as string[], lineCount: 0 };
  let lastSelectionKey = '';
  // Endpoint texts of rows that are not loaded right now: the interval needs
  // their timestamps even after the window moved away from them.
  const rowTextCache = createLineTextCache(4000);

  function absoluteIndex(localIndex: number): number {
    return loadedFrom + localIndex;
  }

  function reportSelection(snapshot: SelectionSnapshot | null): void {
    const key = snapshot
      ? `${snapshot.start}:${snapshot.end}:${snapshot.startText}:${snapshot.endText}`
      : '';
    if (key === lastSelectionKey) {
      return;
    }
    lastSelectionKey = key;
    if (!snapshot) {
      dispatch('selection', emptyRowSelection);
      return;
    }
    dispatch('selection', {
      lines: [snapshot.startText, snapshot.endText],
      lineCount: snapshot.count,
    });
  }

  function clearRowSelection(): void {
    if (selection) {
      selection.reset();
    } else {
      selectionRange = null;
    }
  }

  function rowIsSelected(index: number): boolean {
    const absolute = absoluteIndex(index);
    return selectionFrom >= 0 && absolute >= selectionFrom && absolute <= selectionTo;
  }

  function rowIndexAtPoint(clientX: number, clientY: number): number | null {
    const element = document.elementFromPoint(clientX, clientY)?.closest<HTMLElement>('.filtered-row');
    const raw = element?.dataset.index;
    if (raw === undefined) {
      return null;
    }
    const index = Number.parseInt(raw, 10);
    return Number.isInteger(index) && index >= 0 && index < rows.length ? index : null;
  }

  function edgeIndex(direction: -1 | 1): number | null {
    if (!scrollEl || rows.length === 0) {
      return null;
    }
    const firstVisible = indexAtScrollTop(scrollEl.scrollTop);
    const lastVisible = indexAtScrollTop(scrollEl.scrollTop + scrollEl.clientHeight);
    return direction > 0 ? lastVisible : firstVisible;
  }

  function stepScroll(direction: -1 | 1, step: number): boolean {
    if (!scrollEl || total <= 0) {
      return false;
    }
    const current = indexAtScrollTop(scrollEl.scrollTop);
    const target = Math.min(Math.max(0, current + direction * step), total - 1);
    if (target === current) {
      return false;
    }
    if (target >= loadedFrom && target < loadedFrom + rows.length) {
      scrollEl.scrollTop = scrollTopForIndex(target);
      return true;
    }
    // Move the thumb first so an immediate edge query sees the new index; the
    // window follows once the rows arrive.
    scrollEl.scrollTop = scrollTopForIndex(target);
    const targetFrom = Math.max(
      0,
      Math.min(target - 8, Math.max(0, total - PAGE)),
    );
    void loadWindow(targetFrom).then(() => {
      if (scrollEl) {
        scrollEl.scrollTop = topSpacer + Math.max(0, target - targetFrom) * averageRowHeight;
      }
    });
    return true;
  }

  function createSelection(): RowSelectionController | null {
    if (!scrollEl) {
      return null;
    }
    return new RowSelectionController({
      surface: {
        // A drag that stays inside one row is the browser's native within-line
        // selection, like the desktop view; only a drag to another row becomes
        // a row range.
        mode: 'rows-or-native',
        element: scrollEl,
        keyAt: (clientX, clientY) => {
          const index = rowIndexAtPoint(clientX, clientY);
          return index === null ? null : absoluteIndex(index);
        },
        viewportRect: () => scrollEl.getBoundingClientRect(),
        scrollBy: (direction, step) => stepScroll(direction, step),
        edgeKey: (direction) => edgeIndex(direction),
        endpointText: (key) => rowTextCache.get(key),
        applyRange: (range) => {
          selectionRange = range;
        },
        clearNativeSelection: () => window.getSelection()?.removeAllRanges(),
        onCurrentKeyChange: (absolute) => {
          const row = rows[absolute - loadedFrom];
          if (row) {
            selectedLine = row.num;
          }
        },
        onClick: (absolute) => {
          const row = rows[absolute - loadedFrom];
          if (!row) {
            return;
          }
          selectedLine = row.num;
          dispatch('selectLine', row.num);
        },
        onDoubleClick: (absolute, event) => {
          const row = rows[absolute - loadedFrom];
          if (row) {
            void selectWordAt(event, row);
          }
        },
        onRowGestureEnd: () => {
          // A row drag must not leave a stale native text selection behind.
          window.getSelection()?.removeAllRanges();
        },
        onGestureEnd: (moved) => {
          if (moved) {
            void applyTextSelectionWord();
          }
        },
      },
      onRangeChange: (range, snapshot) => {
        selectionRange = range;
        reportSelection(snapshot);
      },
    });
  }

  $: if (selection) {
    void rows;
    void loadedFrom;
    selection.handleSurfaceFrame();
  }

  function openContextMenu(
    event: MouseEvent,
    index: number,
    row: { num: number; text: string },
  ): void {
    event.preventDefault();
    selectedLine = row.num;
    const clicked = absoluteIndex(index);
    let selectFrom = selectionFrom >= 0 ? selectionFrom : clicked;
    let selectTo = selectionTo >= 0 ? selectionTo : clicked;
    if (!rowIsSelected(index)) {
      // A press inside the native portion keeps it (the menu then acts on the
      // selected characters); anywhere else the desktop replaces the portion
      // with the whole line, so the stale native selection is dropped too.
      if (!pointerInsideTextSelection(event)) {
        window.getSelection()?.removeAllRanges();
        selection?.selectKey(clicked);
      }
      selectFrom = clicked;
      selectTo = clicked;
    }
    const start = Math.min(selectFrom, selectTo);
    const end = Math.max(selectFrom, selectTo);
    // Clamp to the loaded window: the selection itself may reach past it.
    const covered = rows.slice(
      Math.max(0, start - loadedFrom),
      Math.min(rows.length, end - loadedFrom + 1),
    );
    const selectionRows =
      covered.length > 0
        ? covered.map((item) => ({ line: item.num, text: item.text }))
        : [{ line: row.num, text: row.text }];
    const payload: SelectionPayload = {
      focusLine: row.num,
      focusText: row.text,
      range: {
        start: selectionRows[0].line,
        end: selectionRows[selectionRows.length - 1].line,
      },
      rows: selectionRows,
      // A portion only counts while it stays inside the row that was clicked;
      // the press handler already dropped one that landed outside it.
      portion: currentPortion(row),
    };
    dispatch('contextMenu', {
      x: event.clientX,
      y: event.clientY,
      total,
      ...payload,
    });
  }

  /** Within-row selection of the clicked row, if the native selection is one. */
  function currentPortion(row: { num: number; text: string }): SelectionPayload['portion'] {
    const picked = inlineTextSelection();
    if (!picked || picked.line !== row.num) {
      return null;
    }
    const text = row.text.slice(picked.start, picked.end);
    return text.trim() && text !== row.text ? { line: row.num, text } : null;
  }

  function caretRangeAt(x: number, y: number): Range | null {    const documentWithCaret = document as Document & {
      caretRangeFromPoint?: (clientX: number, clientY: number) => Range | null;
      caretPositionFromPoint?: (
        clientX: number,
        clientY: number,
      ) => { offsetNode: Node; offset: number } | null;
    };
    const caretRange = documentWithCaret.caretRangeFromPoint?.(x, y);
    if (caretRange) {
      return caretRange;
    }
    const caretPosition = documentWithCaret.caretPositionFromPoint?.(x, y);
    if (!caretPosition) {
      return null;
    }
    const range = document.createRange();
    const node = caretPosition.offsetNode;
    const offset =
      node.nodeType === Node.TEXT_NODE
        ? Math.min(caretPosition.offset, node.nodeValue?.length ?? 0)
        : caretPosition.offset;
    range.setStart(node, offset);
    range.collapse(true);
    return range;
  }

  function rangeForOffsets(textElement: HTMLElement, start: number, end: number): Range | null {
    const walker = document.createTreeWalker(textElement, NodeFilter.SHOW_TEXT);
    let offset = 0;
    let startNode: Text | null = null;
    let startOffset = 0;
    let endNode: Text | null = null;
    let endOffset = 0;

    let node = walker.nextNode();
    while (node) {
      const textNode = node as Text;
      const length = textNode.nodeValue?.length ?? 0;
      const nextOffset = offset + length;
      if (!startNode && start <= nextOffset) {
        startNode = textNode;
        startOffset = Math.max(0, start - offset);
      }
      if (!endNode && end <= nextOffset) {
        endNode = textNode;
        endOffset = Math.max(0, end - offset);
        break;
      }
      offset = nextOffset;
      node = walker.nextNode();
    }

    if (!startNode || !endNode) {
      return null;
    }
    const range = document.createRange();
    range.setStart(startNode, startOffset);
    range.setEnd(endNode, endOffset);
    return range;
  }

  // Re-rendering the word highlight replaces the row's text nodes, so the
  // native selection is restored from character offsets afterwards.
  async function restoreTextSelection(line: number, start: number, end: number): Promise<void> {
    await tick();
    const textElement = scrollEl?.querySelector<HTMLElement>(
      `.filtered-row[data-line="${line}"] .filtered-text`,
    );
    const range = textElement ? rangeForOffsets(textElement, start, end) : null;
    const domSelection = window.getSelection();
    if (!range || !domSelection) {
      return;
    }
    domSelection.removeAllRanges();
    domSelection.addRange(range);
  }

  // Character offsets of the native selection inside one row's text, or null
  // when it is collapsed or spans rows (that is a row range, not a word).
  function inlineTextSelection(): { line: number; start: number; end: number } | null {
    const domSelection = window.getSelection();
    if (!domSelection || domSelection.rangeCount === 0 || domSelection.isCollapsed) {
      return null;
    }
    const range = domSelection.getRangeAt(0);
    const textElement = filteredTextElement(range.startContainer);
    if (!textElement || textElement !== filteredTextElement(range.endContainer)) {
      return null;
    }
    const line = Number(textElement.closest<HTMLElement>('.filtered-row')?.dataset.line);
    if (!Number.isFinite(line)) {
      return null;
    }
    const start = textOffsetAt(textElement, range.startContainer, range.startOffset);
    const end = textOffsetAt(textElement, range.endContainer, range.endOffset);
    return end > start ? { line, start, end } : null;
  }

  function filteredTextElement(node: Node): HTMLElement | null {
    const element =
      node.nodeType === Node.ELEMENT_NODE ? (node as HTMLElement) : node.parentElement;
    return element?.closest<HTMLElement>('.filtered-text') ?? null;
  }

  /**
   * True when the press landed inside this view's native text selection. The
   * desktop keeps a portion selection on such a right press and replaces it
   * with the whole line otherwise.
   */
  function pointerInsideTextSelection(event: MouseEvent): boolean {
    const domSelection = window.getSelection();
    if (!domSelection || domSelection.rangeCount === 0 || domSelection.isCollapsed) {
      return false;
    }
    const caret = caretRangeAt(event.clientX, event.clientY);
    if (!caret) {
      return false;
    }
    try {
      const range = domSelection.getRangeAt(0);
      return range.comparePoint(caret.startContainer, caret.startOffset) === 0;
    } catch {
      return false;
    }
  }

  function textOffsetAt(textElement: HTMLElement, container: Node, offset: number): number {
    const before = document.createRange();
    before.selectNodeContents(textElement);
    before.setEnd(container, offset);
    return before.toString().length;
  }

  // A within-row drag selection highlights its matches on release, like a
  // double click on a word does.
  async function applyTextSelectionWord(): Promise<void> {
    if (selection?.currentRange) {
      return;
    }
    const picked = inlineTextSelection();
    if (!picked) {
      return;
    }
    const row = rows.find((item) => item.num === picked.line);
    const word = row ? selectionWord(row.text.slice(picked.start, picked.end)) : '';
    if (!word) {
      return;
    }
    if (word !== highlightWord) {
      dispatch('wordHighlight', word);
    }
    await restoreTextSelection(picked.line, picked.start, picked.end);
  }

  async function selectWordAt(event: MouseEvent, row: { num: number; text: string }): Promise<void> {
    if (event.button !== 0) {
      return;
    }
    // Pointer capture retargets mouse events to the scroll container, so the
    // event target is not necessarily the text span; hit-test the coordinates.
    const target = event.target as HTMLElement | null;
    const textElement =
      (document.elementFromPoint(event.clientX, event.clientY) as HTMLElement | null)?.closest<HTMLElement>(
        '.filtered-text',
      ) ?? target?.closest<HTMLElement>('.filtered-text');
    if (!textElement) {
      return;
    }
    const caretRange = caretRangeAt(event.clientX, event.clientY);
    if (!caretRange || !textElement.contains(caretRange.startContainer)) {
      return;
    }

    const beforeCaret = document.createRange();
    beforeCaret.selectNodeContents(textElement);
    beforeCaret.setEnd(caretRange.startContainer, caretRange.startOffset);
    const clickIndex = beforeCaret.toString().length;
    const clickChar = row.text.slice(clickIndex).match(/^./u)?.[0] ?? '';
    if (!clickChar || !/[\p{L}\p{N}\p{Pc}]/u.test(clickChar)) {
      dispatch('wordHighlight', '');
      return;
    }

    const startMatch = row.text.slice(0, clickIndex).match(/[\p{L}\p{N}\p{Pc}]+$/u);
    const endMatch = row.text.slice(clickIndex).match(/^[\p{L}\p{N}\p{Pc}]+/u);
    const start = startMatch ? clickIndex - startMatch[0].length : clickIndex;
    const end = clickIndex + (endMatch?.[0].length ?? 0);
    if (end <= start) {
      dispatch('wordHighlight', '');
      return;
    }

    event.preventDefault();
    dispatch('wordHighlight', row.text.slice(start, end));
    await restoreTextSelection(row.num, start, end);
  }

  // A press inside an existing text selection starts a browser drag of it
  // instead of a new selection, which would make the row gesture below dead
  // until the next press. A log view has nothing to drag to.
  function preventTextDrag(event: DragEvent): void {
    event.preventDefault();
  }

  onMount(() => {
    resizeObserver = new ResizeObserver(() => {
      if (!scrollEl) {
        return;
      }
      const previousHeight = lastScrollClientHeight;
      lastScrollClientHeight = scrollEl.clientHeight;
      viewportHeight = scrollEl.clientHeight;
      updateColumns();
      // A horizontal scrollbar that appears with newly loaded rows shrinks the
      // client height after the scroll was anchored; when the end was in view
      // before the shrink, keep the tail row in view.
      if (
        previousHeight > 0 &&
        scrollEl.clientHeight < previousHeight &&
        lastPageLoaded &&
        scrollEl.scrollTop + previousHeight >= scrollEl.scrollHeight - 2
      ) {
        scrollToEnd();
      }
    });
    if (scrollEl) {
      resizeObserver.observe(scrollEl);
      scrollEl.addEventListener('wheel', onWheel, { passive: false });
      scrollEl.addEventListener('dragstart', preventTextDrag);
      viewportHeight = scrollEl.clientHeight;
      updateColumns();
    }
    selection = createSelection();
    selection?.attach();
    return () => {
      resizeObserver?.disconnect();
      resizeObserver = null;
      selection?.detach();
      selection = null;
      scrollEl?.removeEventListener('wheel', onWheel);
      scrollEl?.removeEventListener('dragstart', preventTextDrag);
      if (scrollTimer) {
        window.clearTimeout(scrollTimer);
      }
    };
  });

  $: {
    void fileId;
    void searchId;
    void visibility;
    void projectionRevision;
    void displayLimit;
    void loadCount();
  }
</script>

<div class="filtered-view">
  <!-- The bottom padding gives the range one extra row, like the desktop scroll
       range (totalLines - visibleLines + 1): the last row can be scrolled up. -->
  <div
    class="filtered-scroll"
    bind:this={scrollEl}
    tabindex="-1"
    style={`font-size:${Math.max(10, fontSize - 1)}px; font-family:${fontFamily}; font-weight:${boldFont ? 700 : 400}; padding-bottom:${ROW_HEIGHT}px`}
    on:scroll={onScroll}
  >
    {#if error}
      <div class="filtered-empty error">{error}</div>
    {:else if loading && rows.length === 0}
      <div class="filtered-empty">{$t('common.loading')}</div>
    {:else if total === 0}
      <div class="filtered-empty">
        {searchId === null
          ? visibility.includes('marks')
            ? $t('filter.noMarks')
            : $t('filter.noSearch')
          : $t('filter.noMatches')}
      </div>
    {:else}
      <div style={`height:${topSpacer}px`}></div>
      {#each rows as row, index (row.num)}
        <div
          class="filtered-row"
          role="row"
          tabindex="-1"
          data-line={row.num}
          data-index={index}
          class:wrapped={wrap}
          class:marked={(row.flags & LineFlagMark) !== 0}
          class:selected={rowIsSelected(index)}
          class:current={selectedLine === row.num}
          style={wrap
            ? `height:${rowHeightFor(row)}px;line-height:${ROW_HEIGHT}px`
            : `height:${ROW_HEIGHT}px;line-height:${ROW_HEIGHT}px`}
          on:contextmenu={(event) => openContextMenu(event, index, row)}
          title={$t('filter.lineTitle', { line: row.num + 1 })}
        >
          <span class="filtered-flags">
            {#if row.flags & LineFlagMark}<span class="flag mark">●</span>{/if}
            {#if row.flags & LineFlagMatch}<span class="flag match">●</span>{/if}
            {#if row.flags & LineFlagEvent}<span class="flag event">▮</span>{/if}
          </span>
          {#if showLineNumbers}
            <span class="filtered-num">{row.num + 1}</span>
          {/if}
          <span class="filtered-text">{@html highlight(row.text, highlightWord, quickWords, quickDefs)}</span>
        </div>
      {/each}
      <div style={`height:${bottomSpacer}px`}></div>
    {/if}
  </div>
</div>
