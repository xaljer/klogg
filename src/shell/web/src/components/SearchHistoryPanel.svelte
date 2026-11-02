<script lang="ts">
  import { createEventDispatcher, onMount, tick } from 'svelte';
  import { fuzzyFilter } from '../lib/fuzzy';
  import { t } from '../lib/i18n';

  export let history: string[] = [];
  export let panelStyle = '';

  const dispatch = createEventDispatcher<{
    pick: string;
    remove: string;
    clear: void;
    close: void;
  }>();

  let query = '';
  let listEl: HTMLDivElement;
  let searchEl: HTMLInputElement;
  let scrollable = false;
  let thumbTop = 0;
  let thumbHeight = 0;
  let drag: { startY: number; startScrollTop: number; maxTop: number; maxScroll: number } | null =
    null;

  $: filteredHistory = fuzzyFilter(history, query, (item) => item);

  function updateScrollbar(): void {
    if (!listEl) {
      scrollable = false;
      return;
    }
    scrollable = listEl.scrollHeight > listEl.clientHeight + 1;
    if (!scrollable) {
      thumbTop = 0;
      thumbHeight = 0;
      return;
    }
    const trackHeight = Math.max(0, listEl.clientHeight - 12);
    const nextThumbHeight = Math.max(
      28,
      Math.round((trackHeight * listEl.clientHeight) / listEl.scrollHeight),
    );
    const maxTop = Math.max(0, trackHeight - nextThumbHeight);
    const maxScroll = Math.max(1, listEl.scrollHeight - listEl.clientHeight);
    thumbHeight = nextThumbHeight;
    thumbTop = Math.round((listEl.scrollTop / maxScroll) * maxTop);
  }

  function startDrag(event: PointerEvent): void {
    if (!listEl) {
      return;
    }
    event.preventDefault();
    const trackHeight = Math.max(1, listEl.clientHeight - 12);
    drag = {
      startY: event.clientY,
      startScrollTop: listEl.scrollTop,
      maxTop: Math.max(1, trackHeight - thumbHeight),
      maxScroll: Math.max(1, listEl.scrollHeight - listEl.clientHeight),
    };
    window.addEventListener('pointermove', onDrag);
    window.addEventListener('pointerup', endDrag, { once: true });
  }

  function onDrag(event: PointerEvent): void {
    if (!drag || !listEl) {
      return;
    }
    const delta = event.clientY - drag.startY;
    listEl.scrollTop = drag.startScrollTop + (delta / drag.maxTop) * drag.maxScroll;
  }

  function endDrag(): void {
    window.removeEventListener('pointermove', onDrag);
    window.removeEventListener('pointerup', endDrag);
    drag = null;
  }

  function pick(value: string): void {
    dispatch('pick', value);
  }

  onMount(() => {
    searchEl?.focus();
    void tick().then(updateScrollbar);
    return () => {
      window.removeEventListener('pointermove', onDrag);
      window.removeEventListener('pointerup', endDrag);
    };
  });

  $: {
    void filteredHistory;
    void tick().then(updateScrollbar);
  }
</script>

<div class="search-popover history-popover history-window" style={panelStyle}>
  <div class="history-scroll-wrap">
    <div class="history-list" bind:this={listEl} on:scroll={updateScrollbar}>
      {#if filteredHistory.length}
        {#each filteredHistory as item (item)}
          <div class="history-entry">
            <button
              class="dropdown-item history-entry-pick"
              title={item}
              on:click={() => pick(item)}
            >{item}</button>
            <button
              class="history-entry-remove"
              title={$t('search.historyRemove')}
              on:click|stopPropagation={() => dispatch('remove', item)}
            >×</button>
          </div>
        {/each}
      {:else}
        <div class="history-empty">{$t('search.noHistoryMatches')}</div>
      {/if}
    </div>
    {#if scrollable}
      <div class="history-scrollbar" aria-hidden="true">
        <button
          type="button"
          class="history-scrollbar-thumb"
          aria-label={$t('search.history')}
          style={`top:${thumbTop}px;height:${thumbHeight}px`}
          on:pointerdown={startDrag}
        ></button>
      </div>
    {/if}
  </div>

  <div class="history-footer">
    <span class="history-footer-count">
      {$t('search.historyCount', { count: filteredHistory.length.toLocaleString() })}
    </span>
    <div class="history-footer-search">
      <input
        class="history-search-input"
        type="text"
        spellcheck="false"
        placeholder={$t('search.historySearch')}
        bind:this={searchEl}
        bind:value={query}
        on:keydown={(event) => {
          if (event.key === 'Escape') {
            event.stopPropagation();
            dispatch('close');
          } else if (event.key === 'Enter' && filteredHistory[0]) {
            pick(filteredHistory[0]);
          }
        }}
      />
      {#if query}
        <button
          class="history-search-clear"
          title={$t('common.clear')}
          on:click={() => {
            query = '';
            searchEl?.focus();
          }}
        >×</button>
      {/if}
    </div>
    <button
      class="dropdown-item history-clear"
      disabled={history.length === 0}
      on:click={() => dispatch('clear')}
    >{$t('search.clearHistory')}</button>
  </div>
</div>
