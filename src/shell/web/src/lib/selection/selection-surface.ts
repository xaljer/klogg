import type { SelectionKey, SelectionMode, SelectionRange } from './types.ts';

/**
 * View-specific adapter for the shared row-selection controller.
 *
 * The controller owns the pointer state machine, autoscroll timer and
 * selection range; an adapter only maps between the view's coordinates and
 * the controller's keys, and applies the resulting range to its renderer.
 * `key` is a source line number in the main view and an absolute filtered
 * result index in the search view.
 */
export interface SelectionSurface {
  readonly mode: SelectionMode;
  readonly element: HTMLElement;
  /** Main view: a drag may start in the empty area below the last line. */
  readonly allowEmptyStart?: boolean;

  /** Row/line under the pointer, or null outside the selectable rows. */
  keyAt(clientX: number, clientY: number): SelectionKey | null;
  /** Visible scroll viewport in client coordinates; used for edge autoscroll. */
  viewportRect(): DOMRect;

  /**
   * One autoscroll tick. `rows` is measured in the surface's own row unit
   * (logical lines in the main view, rendered result rows in the filtered
   * view). Returns false when the list cannot move any further.
   */
  scrollBy(direction: -1 | 1, rows: number): boolean;
  /** Key at the top or bottom edge of what is currently visible. */
  edgeKey(direction: -1 | 1): SelectionKey | null;

  /** Text of a selection endpoint, from cache or API. May be empty. */
  endpointText(key: SelectionKey): string;
  /** Apply the range to the view's renderer; null clears the highlight. */
  applyRange(range: SelectionRange | null): void;

  /**
   * A row gesture took over from the view's native selection (a drag that left
   * the row, or a press in a rows-only view): drop the native highlight so the
   * two models never show at once.
   */
  clearNativeSelection?(): void;

  onCurrentKeyChange?(key: SelectionKey): void;
  onClick?(key: SelectionKey, event: MouseEvent): void;
  onDoubleClick?(key: SelectionKey, event: MouseEvent): void;
  /** A row drag ended; the main view uses this to reset xterm's drag state. */
  onRowGestureEnd?(range: SelectionRange | null): void;
  /**
   * The pointer was released after a left-button gesture. `moved` is false when
   * the pointer stayed inside the click threshold, i.e. this was a click, not a
   * drag. Views derive the word highlight from a finished within-row drag here;
   * it runs after `onRowGestureEnd`, so a row drag has already dropped the
   * native selection.
   */
  onGestureEnd?(moved: boolean): void;
}
