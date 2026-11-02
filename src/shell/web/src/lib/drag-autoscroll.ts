// Drag-to-scroll for row selections: holding the pointer near the top or bottom
// edge of a list keeps it scrolling, so a selection can grow past the rows that
// were visible when the drag started.

export const DRAG_AUTOSCROLL_EDGE = 18;
export const DRAG_AUTOSCROLL_INTERVAL = 70;
const MAX_STEP = 5;
const PIXELS_PER_EXTRA_STEP = 10;

export interface EdgeZone {
  top: number;
  bottom: number;
}

export interface EdgeScroll {
  direction: -1 | 0 | 1;
  /** How far past the edge the pointer is, in pixels. */
  depth: number;
}

export function edgeScrollFor(
  clientY: number,
  zone: EdgeZone,
  edge: number = DRAG_AUTOSCROLL_EDGE,
): EdgeScroll {
  if (clientY > zone.bottom - edge) {
    return { direction: 1, depth: clientY - (zone.bottom - edge) };
  }
  if (clientY < zone.top + edge) {
    return { direction: -1, depth: zone.top + edge - clientY };
  }
  return { direction: 0, depth: 0 };
}

/** Rows (or row heights) to move per tick for a given distance past the edge. */
export function autoScrollStep(depth: number): number {
  return Math.min(MAX_STEP, Math.max(1, 1 + Math.floor(depth / PIXELS_PER_EXTRA_STEP)));
}

export interface DragAutoScroll {
  readonly direction: -1 | 0 | 1;
  /** Re-evaluates the pointer position; call on every pointer move. */
  update(clientY: number, zone: EdgeZone): void;
  stop(): void;
}

/**
 * Owns the scroll timer and the current direction. `onStep` performs one scroll
 * step and returns false when the list cannot move any further, which ends the
 * gesture without dropping the selection.
 */
export function createDragAutoScroll(options: {
  isActive: () => boolean;
  onStep: (direction: -1 | 1, step: number) => boolean;
  intervalMs?: number;
}): DragAutoScroll {
  const intervalMs = options.intervalMs ?? DRAG_AUTOSCROLL_INTERVAL;
  let direction: -1 | 0 | 1 = 0;
  let depth = 0;
  let timer = 0;

  function stop(): void {
    if (timer) {
      clearInterval(timer);
      timer = 0;
    }
    direction = 0;
    depth = 0;
  }

  function tick(): void {
    if (direction === 0 || !options.isActive()) {
      stop();
      return;
    }
    if (!options.onStep(direction, autoScrollStep(depth))) {
      stop();
    }
  }

  return {
    get direction() {
      return direction;
    },
    update(clientY: number, zone: EdgeZone): void {
      const next = edgeScrollFor(clientY, zone);
      depth = next.depth;
      if (next.direction === direction) {
        return;
      }
      if (timer) {
        clearInterval(timer);
        timer = 0;
      }
      direction = next.direction;
      if (direction !== 0) {
        timer = setInterval(tick, intervalMs);
      }
    },
    stop,
  };
}
