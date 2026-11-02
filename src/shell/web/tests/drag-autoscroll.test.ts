import assert from 'node:assert/strict';
import test from 'node:test';

import {
  DRAG_AUTOSCROLL_EDGE,
  autoScrollStep,
  createDragAutoScroll,
  edgeScrollFor,
} from '../src/lib/drag-autoscroll.ts';
import { createLineTextCache } from '../src/lib/line-text-cache.ts';

const zone = { top: 100, bottom: 400 };

test('edgeScrollFor reports direction and distance past the edge', () => {
  assert.deepEqual(edgeScrollFor(250, zone), { direction: 0, depth: 0 });
  // Just inside the bottom edge already scrolls, one pixel past it has depth 1.
  assert.deepEqual(edgeScrollFor(zone.bottom - DRAG_AUTOSCROLL_EDGE + 1, zone), {
    direction: 1,
    depth: 1,
  });
  assert.deepEqual(edgeScrollFor(zone.bottom + 5, zone), { direction: 1, depth: 23 });
  assert.deepEqual(edgeScrollFor(zone.top + DRAG_AUTOSCROLL_EDGE - 1, zone), {
    direction: -1,
    depth: 1,
  });
});

test('autoScrollStep grows with the distance and caps out', () => {
  assert.equal(autoScrollStep(0), 1);
  assert.equal(autoScrollStep(9), 1);
  assert.equal(autoScrollStep(10), 2);
  assert.equal(autoScrollStep(25), 3);
  assert.equal(autoScrollStep(1000), 5);
});

function withTimers(run: (clock: { advance: (ms: number) => void }) => void): void {
  const timers = new Map<number, () => void>();
  let nextId = 1;
  const originalSet = globalThis.setInterval;
  const originalClear = globalThis.clearInterval;
  let now = 0;
  (globalThis as any).setInterval = (handler: () => void) => {
    const id = nextId++;
    timers.set(id, handler);
    return id;
  };
  (globalThis as any).clearInterval = (id: number) => {
    timers.delete(id);
  };
  try {
    run({
      advance(ms: number) {
        now += ms;
        for (const [id, handler] of [...timers]) {
          if (timers.has(id)) {
            handler();
          }
        }
      },
    });
    assert.ok(now >= 0);
  } finally {
    (globalThis as any).setInterval = originalSet;
    (globalThis as any).clearInterval = originalClear;
  }
}

test('createDragAutoScroll ticks only near an edge and stops on request', () => {
  withTimers((clock) => {
    const steps: number[] = [];
    let active = true;
    const scroller = createDragAutoScroll({
      isActive: () => active,
      onStep: (direction, step) => {
        steps.push(direction * step);
        return true;
      },
    });

    scroller.update(250, zone);
    clock.advance(200);
    assert.deepEqual(steps, [], 'a pointer inside the list must not scroll');

    // 15px past the bottom edge: one extra step per 10px, so two rows per tick.
    scroller.update(zone.bottom - DRAG_AUTOSCROLL_EDGE + 15, zone);
    clock.advance(200);
    clock.advance(200);
    assert.deepEqual(steps, [2, 2], 'each tick moves by the distance-based step');

    scroller.stop();
    clock.advance(200);
    assert.deepEqual(steps, [2, 2], 'stop cancels the timer');

    scroller.update(zone.top + DRAG_AUTOSCROLL_EDGE - 5, zone);
    clock.advance(200);
    assert.deepEqual(steps, [2, 2, -1], 'the top edge scrolls upwards');

    active = false;
    clock.advance(200);
    assert.deepEqual(steps, [2, 2, -1], 'an inactive pointer ends the gesture');
    assert.equal(scroller.direction, 0);
  });
});

test('createDragAutoScroll stops when onStep reports the end of the list', () => {
  withTimers((clock) => {
    let calls = 0;
    const scroller = createDragAutoScroll({
      isActive: () => true,
      onStep: () => {
        calls++;
        return false;
      },
    });
    scroller.update(zone.bottom + 1, zone);
    clock.advance(500);
    assert.equal(calls, 1, 'a refused step stops the loop');
    assert.equal(scroller.direction, 0);
  });
});

test('line text cache remembers windows and drops old entries at its limit', () => {
  const cache = createLineTextCache(4);
  cache.setWindow(10, ['a', 'b']);
  assert.equal(cache.get(10), 'a');
  assert.equal(cache.get(11), 'b');
  assert.equal(cache.get(12), '', 'unknown keys report an empty line');

  cache.setWindow(20, ['c', 'd', 'e']);
  assert.ok(cache.size <= 4, 'the cache stays bounded');
  assert.equal(cache.get(10), '', 'oldest entries are dropped once full');
  assert.equal(cache.get(22), 'e');

  cache.clear();
  assert.equal(cache.get(22), '', 'clear forgets everything');
});
