<script lang="ts">
  import { createEventDispatcher, onMount } from 'svelte';
  import { t } from '../lib/i18n';
  import type { Palette } from '../lib/theme';
  import type { ApiClient } from '../lib/api';
  import { LineFlagEvent, LineFlagMark, LineFlagMatch } from '../lib/types';

  export let api: ApiClient;
  export let fileId: number | null = null;
  export let totalLines = 0;
  export let searchId: number | null = null;
  export let firstLine = 0;
  export let rows = 30;
  // Bumped when the match/mark density must be recomputed.
  export let frameRevision = 0;
  export let palette: Palette;

  const dispatch = createEventDispatcher<{ seek: number }>();

  let canvas: HTMLCanvasElement;
  let flags: number[] = [];
  let sampleLines: number[] = [];
  let requestSeq = 0;
  let dragging = false;
  let resizeObserver: ResizeObserver | null = null;

  const SAMPLE_COUNT = 1200;

  function scheduleLoad(): void {
    if (!api || fileId === null || totalLines <= 0) {
      flags = [];
      sampleLines = [];
      draw();
      return;
    }
    void load();
  }

  async function load(): Promise<void> {
    if (fileId === null || totalLines <= 0) {
      return;
    }
    const seq = ++requestSeq;
    const count = Math.min(SAMPLE_COUNT, totalLines);
    const lines: number[] = [];
    for (let i = 0; i < count; i++) {
      lines.push(Math.min(totalLines - 1, Math.round((i * (totalLines - 1)) / Math.max(1, count - 1))));
    }
    const unique = [...new Set(lines)];
    try {
      const result = await api.call<{ flags: number[] }>('flags.getBatch', {
        fileId,
        searchId,
        lines: unique,
      });
      if (seq !== requestSeq) {
        return;
      }
      sampleLines = unique;
      flags = result.flags;
      draw();
    } catch {
      if (seq === requestSeq) {
        flags = [];
        sampleLines = [];
        draw();
      }
    }
  }

  function draw(): void {
    if (!canvas) {
      return;
    }
    const dpr = window.devicePixelRatio || 1;
    const width = canvas.clientWidth || 18;
    const height = canvas.clientHeight || 100;
    if (canvas.width !== Math.round(width * dpr) || canvas.height !== Math.round(height * dpr)) {
      canvas.width = Math.round(width * dpr);
      canvas.height = Math.round(height * dpr);
    }
    const ctx = canvas.getContext('2d');
    if (!ctx) {
      return;
    }
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, width, height);
    ctx.fillStyle = palette.base;
    ctx.fillRect(0, 0, width, height);

    if (totalLines > 0) {
      for (let i = 0; i < flags.length; i++) {
        const flag = flags[i];
        if (!flag) {
          continue;
        }
        const line = sampleLines[i] ?? 0;
        const y = (line / totalLines) * height;
        const color =
          flag & LineFlagEvent
            ? palette.event
            : flag & LineFlagMark
              ? palette.mark
              : flag & LineFlagMatch
                ? palette.matchLine
                : '';
        if (!color) {
          continue;
        }
        ctx.fillStyle = color;
        ctx.fillRect(2, Math.floor(y), width - 4, 1);
      }

      const top = (firstLine / totalLines) * height;
      const viewHeight = Math.max(3, (rows / totalLines) * height);
      ctx.globalAlpha = 0.2;
      ctx.fillStyle = palette.selectionBack;
      ctx.fillRect(0, top, width, viewHeight);
      ctx.globalAlpha = 1;
      ctx.strokeStyle = palette.selectionBack;
      ctx.strokeRect(0.5, top + 0.5, width - 1, Math.max(2, viewHeight - 1));
    }
  }

  $: if (canvas) {
    void palette;
    draw();
  }

  function seek(event: { clientY: number }): void {
    if (!canvas || totalLines <= 0) {
      return;
    }
    const rect = canvas.getBoundingClientRect();
    const ratio = Math.min(1, Math.max(0, (event.clientY - rect.top) / rect.height));
    dispatch('seek', Math.min(totalLines - 1, Math.floor(ratio * totalLines)));
  }

  function onPointerDown(event: PointerEvent): void {
    dragging = true;
    try {
      canvas.setPointerCapture(event.pointerId);
    } catch {
      // synthetic pointers in tests have no active capture
    }
    seek(event);
  }

  function onPointerMove(event: PointerEvent): void {
    if (dragging) {
      seek(event);
    }
  }

  function onPointerUp(event: PointerEvent): void {
    dragging = false;
    if (canvas.hasPointerCapture(event.pointerId)) {
      canvas.releasePointerCapture(event.pointerId);
    }
  }

  onMount(() => {
    resizeObserver = new ResizeObserver(() => draw());
    resizeObserver.observe(canvas);
    draw();
    return () => {
      resizeObserver?.disconnect();
      resizeObserver = null;
    };
  });

  $: {
    void fileId;
    void totalLines;
    void searchId;
    void frameRevision;
    scheduleLoad();
  }

  $: {
    void firstLine;
    void rows;
    draw();
  }
</script>

<canvas
  class="overview"
  bind:this={canvas}
  title={$t('overview.seek')}
  on:pointerdown={onPointerDown}
  on:pointermove={onPointerMove}
  on:pointerup={onPointerUp}
  on:pointercancel={onPointerUp}
  on:click={seek}
></canvas>
