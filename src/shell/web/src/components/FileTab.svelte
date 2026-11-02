<script lang="ts">
  import { createEventDispatcher } from 'svelte';
  import { t } from '../lib/i18n';
  import type { TabMeta } from '../lib/types';

  export let tab: TabMeta;
  export let active = false;
  // Where a tab being dragged would land relative to this one.
  export let dropSide: 'before' | 'after' | null = null;

  const dispatch = createEventDispatcher<{
    select: string;
    close: string;
    contextMenu: { tabId: string; x: number; y: number };
  }>();

  function titleOf(value: TabMeta): string {
    return value.title || value.path.split(/[\\/]/).pop() || value.path;
  }
</script>

<div
  class="file-tab"
  class:active
  class:pinned={tab.pinned}
  class:drop-before={dropSide === 'before'}
  class:drop-after={dropSide === 'after'}
  role="tab"
  aria-selected={active}
  tabindex="0"
  title={tab.path}
  data-tab-id={tab.id}
  draggable="true"
  on:click={() => dispatch('select', tab.id)}
  on:keydown={(event) => {
    if (event.key === 'Enter' || event.key === ' ') {
      event.preventDefault();
      dispatch('select', tab.id);
    }
  }}
  on:auxclick={(event) => {
    if (event.button === 1) {
      dispatch('close', tab.id);
    }
  }}
  on:contextmenu={(event) => {
    event.preventDefault();
    dispatch('contextMenu', { tabId: tab.id, x: event.clientX, y: event.clientY });
  }}
>
  {#if tab.pinned}
    <span class="file-tab-pin" title={$t('app.tab.pinned')} aria-label={$t('app.tab.pinned')}>
      <svg
        viewBox="0 0 16 16"
        width="14"
        height="14"
        fill="none"
        stroke="currentColor"
        stroke-width="1.4"
        stroke-linecap="round"
        stroke-linejoin="round"
        aria-hidden="true"
      >
        <path d="M9.8 2.1 13.9 6.2" />
        <path d="M11.2 3.5 9.3 5.4 6.2 6.1 9.9 9.8 10.6 6.7 12.5 4.8Z" />
        <path d="M6.3 9.7 2.4 13.6" />
      </svg>
    </span>
  {/if}
  <span class="file-tab-title">{titleOf(tab)}</span>
  <button
    class="file-tab-close"
    title={$t('tab.close')}
    on:click|stopPropagation={() => dispatch('close', tab.id)}
  >×</button>
</div>
