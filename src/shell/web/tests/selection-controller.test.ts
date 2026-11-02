import assert from 'node:assert/strict';
import test from 'node:test';

import { RowSelectionController } from '../src/lib/selection/selection-controller.ts';
import type { SelectionSurface } from '../src/lib/selection/selection-surface.ts';
import type { SelectionRange } from '../src/lib/selection/types.ts';

class FakeTarget {
  listeners = new Map<string, ((event: any) => void)[]>();
  captured = new Set<number>();

  addEventListener(type: string, listener: (event: any) => void): void {
    const list = this.listeners.get(type) ?? [];
    list.push(listener);
    this.listeners.set(type, list);
  }

  removeEventListener(type: string, listener: (event: any) => void): void {
    const list = this.listeners.get(type) ?? [];
    this.listeners.set(
      type,
      list.filter((candidate) => candidate !== listener),
    );
  }

  fire(type: string, event: any): void {
    for (const listener of [...(this.listeners.get(type) ?? [])]) {
      listener(event);
    }
  }

  setPointerCapture(pointerId: number): void {
    this.captured.add(pointerId);
  }

  hasPointerCapture(pointerId: number): boolean {
    return this.captured.has(pointerId);
  }

  releasePointerCapture(pointerId: number): void {
    this.captured.delete(pointerId);
  }
}

function pointerEvent(overrides: Record<string, unknown> = {}): any {
  const event = {
    button: 0,
    buttons: 1,
    pointerId: 1,
    clientX: 10,
    clientY: 10,
    shiftKey: false,
    defaultPrevented: false,
    propagationStopped: false,
    preventDefault() {
      event.defaultPrevented = true;
    },
    stopPropagation() {
      event.propagationStopped = true;
    },
    ...overrides,
  };
  return event;
}

function mouseEvent(overrides: Record<string, unknown> = {}): any {
  return pointerEvent(overrides);
}

function createHarness(mode: 'rows-only' | 'rows-or-native') {
  const element = new FakeTarget();
  const windowTarget = new FakeTarget();
  const applied: (SelectionRange | null)[] = [];
  const clicked: number[] = [];
  const doubleClicked: number[] = [];
  let hookCalls = 0;
  let scroll = 0;

  const surface: SelectionSurface = {
    mode,
    element: element as unknown as HTMLElement,
    allowEmptyStart: mode === 'rows-or-native',
    keyAt: (_x, y) => (y < 0 || y >= 400 ? null : Math.floor(y / 20)),
    viewportRect: () => ({ top: 0, bottom: 400, left: 0, right: 100 }) as DOMRect,
    scrollBy: (_direction, rows) => {
      scroll += rows;
      return true;
    },
    edgeKey: (direction) => (direction > 0 ? 19 : 0),
    endpointText: (key) => `line-${key}`,
    applyRange: (range) => applied.push(range),
    onClick: (key) => clicked.push(key),
    onDoubleClick: (key) => doubleClicked.push(key),
    onRowGestureEnd: () => {
      hookCalls += 1;
    },
  };

  const previousWindow = (globalThis as any).window;
  (globalThis as any).window = windowTarget;

  const controller = new RowSelectionController({
    surface,
    onRangeChange: (range) => {
      applied.push(range);
    },
  });
  controller.attach();

  return {
    element,
    windowTarget,
    controller,
    applied,
    clicked,
    doubleClicked,
    get hookCalls() {
      return hookCalls;
    },
    get scroll() {
      return scroll;
    },
    down(overrides: Record<string, unknown> = {}) {
      const event = pointerEvent(overrides);
      element.fire('pointerdown', event);
      return event;
    },
    move(overrides: Record<string, unknown> = {}) {
      const event = pointerEvent(overrides);
      windowTarget.fire('pointermove', event);
      return event;
    },
    up(overrides: Record<string, unknown> = {}) {
      const event = pointerEvent({ buttons: 0, ...overrides });
      windowTarget.fire('pointerup', event);
      return event;
    },
    click(overrides: Record<string, unknown> = {}) {
      const event = mouseEvent(overrides);
      element.fire('click', event);
      return event;
    },
    doubleClick(overrides: Record<string, unknown> = {}) {
      const event = mouseEvent(overrides);
      element.fire('dblclick', event);
      return event;
    },
    cleanup() {
      controller.detach();
      (globalThis as any).window = previousWindow;
    },
  };
}

test('rows-only drag commits a range and extends it across rows', () => {
  const harness = createHarness('rows-only');
  try {
    harness.down({ clientY: 20 });
    assert.deepEqual(harness.controller.currentRange, { base: 1, focus: 1 });
    assert.equal(harness.controller.snapshot()?.count, 1);

    harness.move({ clientY: 100 });
    assert.deepEqual(harness.controller.currentRange, { base: 1, focus: 5 });
    assert.equal(harness.controller.snapshot()?.count, 5);
    assert.equal(harness.controller.snapshot()?.startText, 'line-1');
    assert.equal(harness.controller.snapshot()?.endText, 'line-5');
    assert.ok(harness.controller.contains(3));

    const up = harness.up();
    assert.equal(up.defaultPrevented, false);
    assert.equal(harness.hookCalls, 1);
  } finally {
    harness.cleanup();
  }
});

test('a drag suppresses the following click, a plain click is delivered', () => {
  const harness = createHarness('rows-only');
  try {
    harness.down({ clientY: 20 });
    harness.move({ clientY: 100 });
    harness.up();

    const suppressed = harness.click({ clientY: 100 });
    assert.equal(suppressed.defaultPrevented, true);
    assert.equal(suppressed.propagationStopped, true);
    assert.deepEqual(harness.clicked, []);

    harness.down({ clientY: 40 });
    harness.up({ clientY: 40 });
    const click = harness.click({ clientY: 40 });
    assert.equal(click.defaultPrevented, false);
    assert.deepEqual(harness.clicked, [2]);
    assert.deepEqual(harness.controller.currentRange, { base: 2, focus: 2 });
  } finally {
    harness.cleanup();
  }
});

test('shift extends from the existing anchor instead of replacing it', () => {
  const harness = createHarness('rows-only');
  try {
    harness.down({ clientY: 20 });
    harness.up();
    harness.click({ clientY: 20 });
    assert.deepEqual(harness.controller.currentRange, { base: 1, focus: 1 });

    harness.down({ clientY: 100, shiftKey: true });
    assert.deepEqual(harness.controller.currentRange, { base: 1, focus: 5 });

    harness.move({ clientY: 160, shiftKey: true });
    assert.deepEqual(harness.controller.currentRange, { base: 1, focus: 8 });
    harness.up();
  } finally {
    harness.cleanup();
  }
});

test('rows-or-native keeps a within-line drag out of row mode', () => {
  const harness = createHarness('rows-or-native');
  try {
    harness.down({ clientY: 20, clientX: 10 });
    harness.move({ clientY: 20, clientX: 40 });
    assert.equal(harness.controller.rowDrag, false);
    assert.equal(harness.controller.currentRange, null);
    assert.ok(harness.applied.every((range) => range === null));
    harness.up();
  } finally {
    harness.cleanup();
  }
});

test('rows-or-native switches to a row drag on another line', () => {
  const harness = createHarness('rows-or-native');
  try {
    harness.down({ clientY: 20 });
    harness.move({ clientY: 60 });
    assert.equal(harness.controller.rowDrag, true);
    assert.deepEqual(harness.controller.currentRange, { base: 1, focus: 3 });
    harness.up();
  } finally {
    harness.cleanup();
  }
});

test('double click clears the row range before word selection', () => {
  const harness = createHarness('rows-only');
  try {
    harness.down({ clientY: 20 });
    harness.up();
    assert.notEqual(harness.controller.currentRange, null);

    harness.doubleClick({ clientY: 20 });
    assert.equal(harness.controller.currentRange, null);
    assert.deepEqual(harness.doubleClicked, [1]);
  } finally {
    harness.cleanup();
  }
});

test('reset clears the range and the applied highlight', () => {
  const harness = createHarness('rows-only');
  try {
    harness.down({ clientY: 20 });
    harness.move({ clientY: 100 });
    harness.up();
    harness.controller.reset();
    assert.equal(harness.controller.currentRange, null);
    assert.equal(harness.controller.snapshot(), null);
    assert.equal(harness.applied.at(-1), null);
  } finally {
    harness.cleanup();
  }
});
