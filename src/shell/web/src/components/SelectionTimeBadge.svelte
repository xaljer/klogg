<script lang="ts">
  import { t } from '../lib/i18n';
  import {
    DURATION_UNIT_LABEL,
    formatDurationParts,
    type SelectionTimeDelta,
  } from '../lib/time-delta';

  export let delta: SelectionTimeDelta | null = null;

  $: label = delta ? formatDurationParts(delta.parts) : '';
  // data-* attributes expose the smallest shown unit for the UI smoke tests.
  $: smallest = delta && delta.parts.length > 0 ? delta.parts[delta.parts.length - 1] : null;
</script>

{#if delta && smallest}
  <div
    class="selection-time"
    data-label={label}
    data-value={smallest.value}
    data-unit={smallest.unit}
    aria-label={$t('timeDelta.aria', { count: delta.lineCount, value: label })}
  >
    <span class="selection-time-mark">Δ</span>
    {#each delta.parts as part (part.unit)}
      <span class="selection-time-value">{part.value}</span>
      <span class="selection-time-unit">{DURATION_UNIT_LABEL[part.unit]}</span>
    {/each}
  </div>
{/if}
