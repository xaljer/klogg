import { createDragAutoScroll, type DragAutoScroll } from '../drag-autoscroll.ts';
import type { SelectionSurface } from './selection-surface.ts';
import {
  orderedRange,
  rangeContains,
  type SelectionKey,
  type SelectionPhase,
  type SelectionRange,
  type SelectionSnapshot,
} from './types.ts';

export interface RowSelectionOptions {
  surface: SelectionSurface;
  onRangeChange?: (range: SelectionRange | null, snapshot: SelectionSnapshot | null) => void;
  /** Movement in pixels before a press becomes a drag. */
  clickThresholdPx?: number;
}

interface PinnedEndpoints {
  start: SelectionKey;
  end: SelectionKey;
  startText: string;
  endText: string;
}

/**
 * One pointer/ranges/autoscroll state machine for both log views.
 *
 * The controller never touches xterm, DOM rows or the API: the surface maps
 * pointer coordinates to keys, performs scrolling and renders the range.
 * Main view and filtered view therefore share thresholds, Shift handling,
 * click suppression, edge autoscroll and release-to-edge behavior.
 */
export class RowSelectionController {
  private readonly surface: SelectionSurface;
  private readonly onRangeChange?: RowSelectionOptions['onRangeChange'];
  private readonly clickThresholdPx: number;
  private readonly autoScroll: DragAutoScroll;

  private phase: SelectionPhase = 'idle';
  private range: SelectionRange | null = null;
  private pointerId: number | null = null;
  private pressX = 0;
  private pressY = 0;
  private dragStartKey: SelectionKey | null = null;
  private baseKey: SelectionKey | null = null;
  private focusKey: SelectionKey | null = null;
  private rowMode = false;
  private moved = false;
  private suppressClick = false;
  private pinned: PinnedEndpoints | null = null;
  private attached = false;

  constructor(options: RowSelectionOptions) {
    this.surface = options.surface;
    this.onRangeChange = options.onRangeChange;
    this.clickThresholdPx = options.clickThresholdPx ?? 4;
    this.autoScroll = createDragAutoScroll({
      isActive: () => this.phase === 'dragging' && this.rowMode,
      onStep: (direction, step) => this.stepScroll(direction, step),
    });
  }

  get currentRange(): SelectionRange | null {
    return this.range;
  }

  get dragging(): boolean {
    return this.phase === 'dragging';
  }

  get rowDrag(): boolean {
    return this.rowMode;
  }

  attach(): void {
    if (this.attached) {
      return;
    }
    const element = this.surface.element;
    element.addEventListener('pointerdown', this.handlePointerDown, true);    element.addEventListener('click', this.handleClick, true);
    element.addEventListener('dblclick', this.handleDoubleClick, true);
    element.addEventListener('lostpointercapture', this.handleLostPointerCapture);
    window.addEventListener('pointermove', this.handlePointerMove, true);
    window.addEventListener('pointerup', this.handlePointerUp, true);
    window.addEventListener('pointercancel', this.handlePointerCancel, true);
    window.addEventListener('blur', this.handleBlur);
    this.attached = true;
  }

  detach(): void {
    if (!this.attached) {
      return;
    }
    const element = this.surface.element;
    element.removeEventListener('pointerdown', this.handlePointerDown, true);
    element.removeEventListener('click', this.handleClick, true);
    element.removeEventListener('dblclick', this.handleDoubleClick, true);
    element.removeEventListener('lostpointercapture', this.handleLostPointerCapture);
    window.removeEventListener('pointermove', this.handlePointerMove, true);
    window.removeEventListener('pointerup', this.handlePointerUp, true);
    window.removeEventListener('pointercancel', this.handlePointerCancel, true);
    window.removeEventListener('blur', this.handleBlur);
    this.attached = false;
    this.reset();
  }

  reset(): void {
    this.autoScroll.stop();
    this.phase = 'idle';
    this.pointerId = null;
    this.dragStartKey = null;
    this.baseKey = null;
    this.focusKey = null;
    this.rowMode = false;
    this.moved = false;
    this.suppressClick = false;
    this.pinned = null;
    this.setRange(null);
  }

  contains(key: SelectionKey): boolean {
    return this.range !== null && rangeContains(this.range, key);
  }

  /** Programmatic selection (context menu, select-all, navigation). */
  selectKey(key: SelectionKey, extend = false): void {
    if (extend && this.range) {
      this.baseKey = this.range.base;
    } else {
      this.baseKey = key;
    }
    this.focusKey = key;
    this.commit();
  }

  selectRange(start: SelectionKey, end: SelectionKey): void {
    this.baseKey = start;
    this.focusKey = end;
    this.commit();
  }

  /** Drop the row range without ending a pointer gesture. */
  clear(): void {
    this.baseKey = null;
    this.focusKey = null;
    this.setRange(null);
  }

  snapshot(): SelectionSnapshot | null {
    if (!this.range) {
      return null;
    }
    const { start, end } = orderedRange(this.range);
    const pinned = this.pinned;
    return {
      start,
      end,
      count: end - start + 1,
      startText:
        pinned?.start === start ? pinned.startText : this.surface.endpointText(start),
      endText: pinned?.end === end ? pinned.endText : this.surface.endpointText(end),
    };
  }

  /**
   * Called after the view rendered a new frame or loaded a different page.
   * Main-view scrolling is asynchronous, so its edgeKey is stale until the
   * frame arrives; this is where the focus catches up.
   */
  handleSurfaceFrame(): void {
    if (this.phase !== 'dragging' || !this.rowMode) {
      this.refreshEndpoints();
      return;
    }
    const direction = this.autoScroll.direction;
    if (direction === 0) {
      this.refreshEndpoints();
      return;
    }
    this.extendToEdge(direction);
    this.refreshEndpoints();
  }

  /** Re-read endpoint texts after a virtual window loaded new rows. */
  refreshEndpoints(): void {
    if (!this.range) {
      return;
    }
    const before = this.pinned
      ? { start: this.pinned.start, end: this.pinned.end, startText: this.pinned.startText, endText: this.pinned.endText }
      : null;
    this.pinEndpoints(this.range);
    if (
      before &&
      this.pinned &&
      (before.start !== this.pinned.start ||
        before.end !== this.pinned.end ||
        before.startText !== this.pinned.startText ||
        before.endText !== this.pinned.endText)
    ) {
      this.onRangeChange?.(this.range, this.snapshot());
    }
  }

  private handlePointerDown = (event: PointerEvent): void => {
    if (event.button !== 0 || this.phase !== 'idle') {
      return;
    }
    const key = this.surface.keyAt(event.clientX, event.clientY);
    if (key === null && !this.surface.allowEmptyStart) {
      return;
    }

    this.phase = 'pressed';
    this.pointerId = event.pointerId;
    this.pressX = event.clientX;
    this.pressY = event.clientY;
    this.dragStartKey = key;
    this.moved = false;
    this.rowMode = this.surface.mode === 'rows-only';
    this.suppressClick = false;

    if (event.shiftKey && this.range) {
      this.baseKey = this.range.base;
      this.focusKey = key ?? this.range.focus;
    } else {
      this.baseKey = key;
      this.focusKey = key;
    }

    if (this.surface.mode === 'rows-or-native' && !event.shiftKey) {
      // A new native/column gesture must not keep the previous row range on
      // screen (xterm will own the selection until the gesture becomes a row
      // drag or the click handler anchors a line).
      this.setRange(null);
    }

    if (key !== null) {
      this.surface.onCurrentKeyChange?.(key);
    }

    if (this.rowMode) {
      event.preventDefault();
      this.surface.clearNativeSelection?.();
      this.capturePointer();
      this.commit();
    }
  };

  private handlePointerMove = (event: PointerEvent): void => {
    if (this.phase === 'idle' || event.pointerId !== this.pointerId) {
      return;
    }
    if ((event.buttons & 1) === 0) {
      this.finish();
      return;
    }

    const distance = Math.hypot(event.clientX - this.pressX, event.clientY - this.pressY);
    if (!this.moved && distance <= this.clickThresholdPx) {
      return;
    }
    if (!this.moved) {
      this.moved = true;
      this.phase = 'dragging';
    }

    const key = this.surface.keyAt(event.clientX, event.clientY);
    const rect = this.surface.viewportRect();
    const outsideY = event.clientY < rect.top || event.clientY > rect.bottom;

    if (!this.rowMode && this.surface.mode === 'rows-or-native') {
      // Keep the main view's native within-line (column) selection: only a
      // different line, or leaving the viewport vertically, starts a row drag.
      const changedLine =
        key !== null && (this.dragStartKey === null || key !== this.dragStartKey);
      if (changedLine || (key === null && outsideY)) {
        this.rowMode = true;
        this.surface.clearNativeSelection?.();
        event.preventDefault();
        event.stopPropagation();
        this.capturePointer();
      }
    }

    if (this.rowMode) {
      if (this.baseKey === null && key !== null) {
        this.baseKey = key;
      }
      if (key !== null) {
        this.focusKey = key;
      }
      this.commit();
      event.preventDefault();
      this.autoScroll.update(event.clientY, rect);
    }
  };

  private handlePointerUp = (event: PointerEvent): void => {
    if (this.phase === 'idle' || event.pointerId !== this.pointerId) {
      return;
    }
    if (this.rowMode && this.autoScroll.direction !== 0) {
      this.extendToEdge(this.autoScroll.direction);
    }
    const moved = this.moved;
    if (moved) {
      this.suppressClick = true;
    }
    this.finish();
    this.surface.onGestureEnd?.(moved);
  };

  private handlePointerCancel = (event: PointerEvent): void => {
    if (this.phase === 'idle' || event.pointerId !== this.pointerId) {
      return;
    }
    this.finish();
  };

  private handleBlur = (): void => {
    if (this.phase !== 'idle') {
      this.finish();
    }
  };

  private handleLostPointerCapture = (): void => {
    if (this.phase !== 'idle') {
      this.finish();
    }
  };

  private handleClick = (event: MouseEvent): void => {
    if (this.suppressClick) {
      this.suppressClick = false;
      event.preventDefault();
      event.stopPropagation();
      return;
    }
    const key = this.surface.keyAt(event.clientX, event.clientY);
    if (key === null) {
      return;
    }
    if (event.shiftKey && this.range) {
      this.baseKey = this.range.base;
      this.focusKey = key;
    } else {
      this.baseKey = key;
      this.focusKey = key;
    }
    this.commit();
    this.surface.onClick?.(key, event);
  };

  private handleDoubleClick = (event: MouseEvent): void => {
    const key = this.surface.keyAt(event.clientX, event.clientY);
    if (key === null) {
      return;
    }
    // A word/column selection is not line anchored.
    this.baseKey = null;
    this.focusKey = null;
    this.setRange(null);
    this.surface.onDoubleClick?.(key, event);
  };

  private stepScroll(direction: -1 | 1, step: number): boolean {
    if (!this.surface.scrollBy(direction, step)) {
      return false;
    }
    // Filtered view: rows/edge are current right away. Main view: the frame
    // is still being rendered, so this is a no-op until handleSurfaceFrame.
    this.extendToEdge(direction);
    return true;
  }

  private extendToEdge(direction: -1 | 1): void {
    const edge = this.surface.edgeKey(direction);
    if (edge === null || this.focusKey === null) {
      return;
    }
    const extendsFocus = direction > 0 ? edge > this.focusKey : edge < this.focusKey;
    // A stale frame can report an edge behind the current focus; never shrink
    // a selection just because the release happened before the frame landed.
    if (!extendsFocus) {
      return;
    }
    this.focusKey = edge;
    this.commit();
  }

  private capturePointer(): void {
    if (this.pointerId === null) {
      return;
    }
    try {
      this.surface.element.setPointerCapture(this.pointerId);
    } catch {
      // Pointer capture is best-effort (e.g. synthetic events in tests).
    }
  }

  private releasePointer(): void {
    if (this.pointerId === null) {
      return;
    }
    try {
      if (this.surface.element.hasPointerCapture(this.pointerId)) {
        this.surface.element.releasePointerCapture(this.pointerId);
      }
    } catch {
      // The browser may already have released it.
    }
  }

  private finish(): void {
    const wasRowDrag = this.rowMode;
    const finishedRange = this.range;
    this.autoScroll.stop();
    this.phase = 'idle';
    this.releasePointer();
    this.pointerId = null;
    this.dragStartKey = null;
    this.baseKey = null;
    this.focusKey = null;
    this.rowMode = false;
    this.moved = false;
    if (wasRowDrag) {
      this.surface.onRowGestureEnd?.(finishedRange);
    }
  }

  private commit(): void {
    if (this.baseKey === null || this.focusKey === null) {
      this.setRange(null);
      return;
    }
    const next = { base: this.baseKey, focus: this.focusKey };
    const before = this.pinned
      ? { startText: this.pinned.startText, endText: this.pinned.endText }
      : null;
    this.pinEndpoints(next);
    const unchanged =
      this.range &&
      this.range.base === next.base &&
      this.range.focus === next.focus;
    if (unchanged) {
      if (
        before &&
        this.pinned &&
        (before.startText !== this.pinned.startText || before.endText !== this.pinned.endText)
      ) {
        this.onRangeChange?.(this.range, this.snapshot());
      }
      return;
    }
    this.range = next;
    this.surface.applyRange(next);
    this.onRangeChange?.(next, this.snapshot());
  }

  private setRange(range: SelectionRange | null): void {
    this.range = range;
    this.pinned = null;
    if (range) {
      this.pinEndpoints(range);
    }
    this.surface.applyRange(range);
    this.onRangeChange?.(range, this.snapshot());
  }

  private pinEndpoints(range: SelectionRange): void {
    const { start, end } = orderedRange(range);
    if (!this.pinned || this.pinned.start !== start) {
      this.pinned = {
        start,
        end,
        startText: this.surface.endpointText(start),
        endText: this.surface.endpointText(end),
      };
      return;
    }
    if (this.pinned.end !== end) {
      this.pinned.end = end;
      this.pinned.endText = this.surface.endpointText(end);
    }
    // Endpoint rows can load after the selection was made (virtual list).
    if (!this.pinned.startText) {
      this.pinned.startText = this.surface.endpointText(start);
    }
    if (!this.pinned.endText) {
      this.pinned.endText = this.surface.endpointText(end);
    }
  }
}
