<script lang="ts">
  import { createEventDispatcher, onMount, tick } from 'svelte';
  import { t } from '../lib/i18n';
  import type { SearchChip } from '../lib/types';
  import SearchHistoryPanel from './SearchHistoryPanel.svelte';

  export let visibility = 'marks-matches';
  export let pattern = '';
  export let regex = true;
  export let caseSensitive = false;
  export let invert = false;
  export let running = false;
  // A live refresh must not flash the progress indicator while the previous
  // result set is still on screen.
  export let quiet = false;
  export let progress = 0;
  export let totalMatches = 0;
  export let disabled = false;
  export let history: string[] = [];
  export let chipMode = false;
  export let chips: SearchChip[] = [];
  export let liveSearch = true;

  const dispatch = createEventDispatcher<{
    run: void;
    stop: void;
    clear: void;
    visibilityChange: string;
    historyClear: void;
    historyRemove: string;
    historyPick: string;
    chipModeChange: boolean;
    liveSearchChange: boolean;
    chipAdd: { text: string; negate: boolean };
    chipRemove: number;
    chipEdit: { index: number; text: string };
    prevMatch: void;
    nextMatch: void;
  }>();

  let historyOpen = false;
  let historyStyle = '';
  let chipInput = '';
  let chipNegate = false;
  let editingIndex = -1;
  let editingValue = '';
  let editInputEl: HTMLInputElement | null = null;
  let comboEl: HTMLDivElement;
  let searchbarEl: HTMLDivElement;

  function onDocumentPointerDown(event: PointerEvent): void {
    if (comboEl && !comboEl.contains(event.target as Node)) {
      historyOpen = false;
    }
  }

  function placeHistory(): void {
    const combo = comboEl?.getBoundingClientRect();
    const bar = searchbarEl?.getBoundingClientRect() ?? combo;
    if (!combo || !bar) {
      return;
    }
    const width = Math.max(260, combo.width);
    const left = Math.max(8, Math.round(combo.left - (width - combo.width) / 2));
    const top = bar.bottom + 4;
    const maxHeight = Math.max(120, window.innerHeight - top - 8);
    historyStyle = `left:${left}px;width:${width}px;top:${top}px;max-height:${maxHeight}px;`;
  }

  function toggleHistory(): void {
    if (historyOpen) {
      historyOpen = false;
      return;
    }
    historyOpen = true;
    placeHistory();
  }

  function onViewportResize(): void {
    if (historyOpen) {
      placeHistory();
    }
  }

  onMount(() => {
    window.addEventListener('pointerdown', onDocumentPointerDown, true);
    window.addEventListener('resize', onViewportResize);
    return () => {
      window.removeEventListener('pointerdown', onDocumentPointerDown, true);
      window.removeEventListener('resize', onViewportResize);
    };
  });

  $: infoText = running && !quiet
    ? $t('search.searching', { percent: Math.round(progress * 100) })
    : totalMatches > 0
      ? $t('search.matches', { count: totalMatches.toLocaleString() })
      : '';

  $: canRun = chipMode ? chips.length > 0 : !!pattern.trim();

  function changeVisibility(): void {
    dispatch('visibilityChange', visibility);
  }

  function pickHistory(value: string): void {
    historyOpen = false;
    dispatch('historyPick', value);
  }

  function chipText(chip: SearchChip): string {
    if (chip.type === 'raw') {
      return chip.terms.join('');
    }
    if (chip.type === 'not') {
      return `NOT ${chip.terms[0] ?? ''}`;
    }
    if (chip.type === 'notAnd') {
      return `NOT (${chip.terms.join(' & ')})`;
    }
    return chip.terms.join(' & ');
  }

  /**
   * Text the inline editor is seeded with: the terms only. The NOT prefix is
   * part of the chip type, so editing it would turn the prefix into a term.
   */
  function chipEditText(chip: SearchChip): string {
    return chip.terms.join(' & ');
  }

  async function beginChipEdit(index: number, chip: SearchChip): Promise<void> {
    if (disabled) {
      return;
    }
    editingIndex = index;
    editingValue = chipEditText(chip);
    await tick();
    editInputEl?.focus();
    editInputEl?.select();
  }

  function commitChipEdit(): void {
    if (editingIndex < 0) {
      return;
    }
    const index = editingIndex;
    editingIndex = -1;
    dispatch('chipEdit', { index, text: editingValue });
  }

  function cancelChipEdit(): void {
    editingIndex = -1;
  }

  function submitChip(): void {
    const text = chipInput.trim();
    if (!text) {
      return;
    }
    dispatch('chipAdd', { text, negate: chipNegate });
    chipInput = '';
    chipNegate = false;
  }
</script>

<div class="searchbar" bind:this={searchbarEl}>
  <select
    class="visibility"
    bind:value={visibility}
    on:change={changeVisibility}
    disabled={disabled}
    title={$t('search.visibilityTitle')}
  >
    <option value="marks-matches">{$t('search.marksMatches')}</option>
    <option value="marks-matches-events">{$t('search.marksMatchesEvents')}</option>
    <option value="marks">{$t('search.marks')}</option>
    <option value="matches">{$t('search.matchesOnly')}</option>
    <option value="events">{$t('search.events')}</option>
  </select>

  <label class="toggle" class:active={caseSensitive} title={$t('search.case')}>
    <input type="checkbox" bind:checked={caseSensitive} disabled={disabled} />
    Aa
  </label>
  <label class="toggle" class:active={regex} title={$t('search.regex')}>
    <input type="checkbox" bind:checked={regex} disabled={disabled} />
    .*
  </label>
  <label class="toggle" class:active={invert} title={$t('search.invert')}>
    <input type="checkbox" bind:checked={invert} disabled={disabled} />
    !
  </label>

  <button
    class="ghost compact chip-toggle"
    class:active={chipMode}
    title={$t('search.chipModeTitle')}
    disabled={disabled}
    on:click={() => dispatch('chipModeChange', !chipMode)}
  >Chip</button>

  <div class="search-combo" bind:this={comboEl}>
    {#if chipMode}
      <div class="chip-entry">
        <button
          class="chip-not"
          class:active={chipNegate}
          title={$t('search.chipNotTitle')}
          disabled={disabled}
          on:click={() => (chipNegate = !chipNegate)}
        >NOT</button>
        <div class="chip-list">
          {#each chips as chip, index (chip.id)}
            <span class="search-chip {chip.type}" title={chipText(chip)}>
              {#if editingIndex === index}
                <input
                  class="search-chip-edit"
                  type="text"
                  spellcheck="false"
                  bind:value={editingValue}
                  bind:this={editInputEl}
                  on:keydown={(event) => {
                    if (event.key === 'Enter') {
                      event.preventDefault();
                      commitChipEdit();
                    } else if (event.key === 'Escape') {
                      event.preventDefault();
                      cancelChipEdit();
                    }
                  }}
                  on:blur={cancelChipEdit}
                />
              {:else}
                <button
                  type="button"
                  class="search-chip-label"
                  title={$t('search.chipEdit')}
                  on:click={() => void beginChipEdit(index, chip)}
                >{chipText(chip)}</button>
              {/if}
              <button
                class="search-chip-remove"
                title={$t('search.chipRemove')}
                disabled={disabled}
                on:click={() => dispatch('chipRemove', index)}
              >×</button>
            </span>
          {/each}
          <input
            class="chip-input"
            type="text"
            spellcheck="false"
            placeholder={$t('search.chipPlaceholder')}
            bind:value={chipInput}
            disabled={disabled}
            on:keydown={(event) => event.key === 'Enter' && submitChip()}
          />
        </div>
        {#if chips.length}
          <button
            class="chip-clear"
            title={$t('search.clearChips')}
            on:click={() => dispatch('clear')}
          >×</button>
        {/if}
      </div>
    {:else}
      <div class="search-entry">
        <input
          class="search-input"
          type="text"
          spellcheck="false"
          placeholder={$t('search.placeholder')}
          bind:value={pattern}
          disabled={disabled}
          on:keydown={(event) => event.key === 'Enter' && dispatch('run')}
        />
        {#if pattern}
          <button class="search-clear" title={$t('search.clear')} on:click={() => dispatch('clear')}>×</button>
        {/if}
      </div>
    {/if}

    <button
      class="search-history-toggle"
      class:active={historyOpen}
      title={$t('search.history')}
      disabled={disabled || history.length === 0}
      aria-expanded={historyOpen}
      on:click={toggleHistory}
    >▾</button>

    {#if historyOpen}
      <SearchHistoryPanel
        {history}
        panelStyle={historyStyle}
        on:pick={(event) => pickHistory(event.detail)}
        on:remove={(event) => dispatch('historyRemove', event.detail)}
        on:clear={() => {
          historyOpen = false;
          dispatch('historyClear');
        }}
        on:close={() => (historyOpen = false)}
      />
    {/if}
  </div>

  <button
    class="ghost compact chip-toggle live-toggle"
    class:active={liveSearch}
    title={$t('search.liveTitle')}
    aria-pressed={liveSearch}
    disabled={disabled}
    on:click={() => dispatch('liveSearchChange', !liveSearch)}
  >⟳</button>

  {#if running && !quiet}
    <button class="icon search-action" title={$t('search.stop')} on:click={() => dispatch('stop')}>■</button>
  {:else}
    <button
      class="icon search-action"
      title={$t('search.run')}
      on:click={() => dispatch('run')}
      disabled={disabled || !canRun}
    >
      <svg viewBox="0 0 16 16" width="14" height="14" fill="none" stroke="currentColor" stroke-width="1.5">
        <circle cx="7" cy="7" r="4.2" />
        <path d="M10.2 10.2 14 14" />
      </svg>
    </button>
  {/if}

  {#if running && !quiet}
    <div class="progress" title={`${Math.round(progress * 100)}%`}>
      <span style={`width:${Math.round(progress * 100)}%`}></span>
    </div>
  {/if}
  <div class="search-match-nav">
    <button
      class="icon tiny"
      title={$t('filter.prevMatch')}
      on:click={() => dispatch('prevMatch')}
      disabled={disabled || totalMatches === 0}
    >↑</button>
    <button
      class="icon tiny"
      title={$t('filter.nextMatch')}
      on:click={() => dispatch('nextMatch')}
      disabled={disabled || totalMatches === 0}
    >↓</button>
    <span class="search-info">{infoText}</span>
  </div>
</div>
