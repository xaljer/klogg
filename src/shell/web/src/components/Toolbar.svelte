<script lang="ts">
  import { createEventDispatcher } from 'svelte';
  import { locale, t } from '../lib/i18n';
  import type { FileInfo } from '../lib/types';

  export let fileInfo: FileInfo | null = null;
  export let busy = false;
  export let follow = false;
  export let wrap = false;
  export let marksCount = 0;
  export let firstLine = 0;
  export let currentLine: number | null = null;
  export let selectionRange = '';
  export let message = '';
  export let encodings: string[] = [];
  export let indexing = false;
  export let favorite = false;
  export let recorderBound = false;
  export let recorderRunning = false;
  export let recorderBusy = false;
  $: recorderTargetDisabled = !fileInfo || fileInfo.path.includes('://');
  $: recorderTitle = recorderRunning
    ? $t('toolbar.recorderStop')
    : recorderBound
      ? $t('toolbar.recorderStart')
      : $t('toolbar.recorderBind');

  const dispatch = createEventDispatcher<{
    reload: void;
    followChange: boolean;
    wrapChange: boolean;
    goToLine: number;
    nextMark: void;
    prevMark: void;
    closeFile: void;
    top: void;
    fileContextMenu: { x: number; y: number };
    encodingChange: string;
    stopLoading: void;
    favoriteToggle: void;
    recorderAction: { x: number; y: number; menu: boolean };
  }>();

  let goLine = '';

  $: sizeText = fileInfo ? formatBytes(fileInfo.sizeBytes) : '';
  $: dateText = fileInfo?.lastModifiedMs ? formatDate(fileInfo.lastModifiedMs, $locale) : '';
  function formatBytes(bytes: number): string {
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KiB`;
    if (bytes < 1024 * 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MiB`;
    return `${(bytes / 1024 / 1024 / 1024).toFixed(2)} GiB`;
  }

  function formatDate(ms: number, localeName: string): string {
    try {
      return new Date(ms).toLocaleString(localeName, {
        year: 'numeric',
        month: '2-digit',
        day: '2-digit',
        hour: '2-digit',
        minute: '2-digit',
      });
    } catch {
      return '';
    }
  }

  function submitGo(): void {
    const line = Number.parseInt(goLine, 10);
    if (Number.isFinite(line)) {
      dispatch('goToLine', Math.max(0, line - 1));
      goLine = '';
    }
  }
</script>

<div class="toolbar">
  <div class="toolbar-group">
    <button
      class="icon favorite-toggle"
      class:active={favorite}
      title={favorite ? $t('favorites.remove') : $t('favorites.add')}
      on:click={() => dispatch('favoriteToggle')}
      disabled={!fileInfo}
    >{favorite ? '★' : '☆'}</button>
    <button class="icon" title={$t('toolbar.reload')} on:click={() => dispatch('reload')} disabled={!fileInfo || busy}>
      <svg viewBox="0 0 16 16" width="14" height="14" fill="none" stroke="currentColor" stroke-width="1.4">
        <path d="M13 8a5 5 0 1 1-1.5-3.6" />
        <path d="M13 2.5v3h-3" />
      </svg>
    </button>
    {#if busy || indexing}
      <button class="icon stop-loading" title={$t('toolbar.stopIndexing')} on:click={() => dispatch('stopLoading')}>■</button>
    {/if}
    <button
      class="icon follow-toggle"
      data-action="follow"
      class:active={follow}
      title={$t('toolbar.follow')}
      on:click={() => dispatch('followChange', !follow)}
      disabled={!fileInfo}
    >
      <svg viewBox="0 0 16 16" width="14" height="14" fill="none" stroke="currentColor" stroke-width="1.4">
        <path d="M8 2v8" />
        <path d="m5 7 3 3 3-3" />
        <path d="M3 13h10" />
      </svg>
    </button>
    <button class="icon" title={$t('toolbar.top')} on:click={() => dispatch('top')} disabled={!fileInfo}>
      <svg viewBox="0 0 16 16" width="14" height="14" fill="none" stroke="currentColor" stroke-width="1.4">
        <path d="M8 14V4" />
        <path d="m5 7 3-3 3 3" />
        <path d="M3 2h10" />
      </svg>
    </button>
    <button
      class="icon"
      class:active={wrap}
      title={$t('toolbar.wrap')}
      on:click={() => dispatch('wrapChange', !wrap)}
      disabled={!fileInfo}
    >
      <svg viewBox="0 0 16 16" width="14" height="14" fill="none" stroke="currentColor" stroke-width="1.4">
        <path d="M2 4h8a3 3 0 0 1 0 6H6" />
        <path d="m8 7-2 3 2 3" />
      </svg>
    </button>
    {#if fileInfo}
      <button
        class="icon recorder-action"
        class:recording={recorderRunning}
        class:bound={recorderBound && !recorderRunning}
        title={recorderTitle}
        aria-label={recorderTitle}
        on:click={(event) => {
          const rect = (event.currentTarget as HTMLElement).getBoundingClientRect();
          dispatch('recorderAction', { x: rect.left, y: rect.bottom + 4, menu: false });
        }}
        on:contextmenu={(event) => {
          event.preventDefault();
          const rect = (event.currentTarget as HTMLElement).getBoundingClientRect();
          dispatch('recorderAction', { x: rect.left, y: rect.bottom + 4, menu: true });
        }}
        disabled={recorderBusy || recorderTargetDisabled}
      >
        {#if recorderRunning}
          <svg viewBox="0 0 16 16" width="14" height="14" aria-hidden="true">
            <rect x="4.5" y="4.5" width="7" height="7" rx="1.2" fill="currentColor" />
          </svg>
        {:else if recorderBound}
          <svg viewBox="0 0 16 16" width="14" height="14" aria-hidden="true">
            <path d="M5 3.4 12.6 8 5 12.6Z" fill="currentColor" />
          </svg>
        {:else}
          <svg
            viewBox="0 0 16 16"
            width="14"
            height="14"
            fill="none"
            stroke="currentColor"
            stroke-width="1.4"
            stroke-linecap="round"
            aria-hidden="true"
          >
            <path d="M6.1 9.9 9.9 6.1" />
            <path d="M7.2 5.2 8.6 3.8a2.4 2.4 0 0 1 3.4 3.4l-1.4 1.4" />
            <path d="m8.8 10.8-1.4 1.4a2.4 2.4 0 0 1-3.4-3.4l1.4-1.4" />
          </svg>
        {/if}
      </button>
    {/if}
  </div>

  <div class="toolbar-file">
    {#if fileInfo}
      <!-- svelte-ignore a11y_no_static_element_interactions -->
      <span
        class="file-chip"
        title={fileInfo.path}
        on:contextmenu={(event) => {
          event.preventDefault();
          dispatch('fileContextMenu', { x: event.clientX, y: event.clientY });
        }}
      >
        <span class="file-chip-name">{fileInfo.path}</span>
        <button class="chip-close" title={$t('toolbar.closeFile')} on:click={() => dispatch('closeFile')}>×</button>
      </span>
      <span class="toolbar-info size-info">{sizeText}</span>
      {#if dateText}
        <span class="toolbar-info date-info" title={$t('toolbar.modifiedAt', { date: dateText })}>
          {$t('toolbar.modifiedAt', { date: dateText })}
        </span>
      {/if}
      {#if encodings.length}
        <select
          class="encoding-select"
          title={$t('toolbar.encoding')}
          value={fileInfo.encoding.name}
          on:change={(event) => dispatch('encodingChange', event.currentTarget.value)}
          disabled={busy}
        >
          {#if !encodings.includes(fileInfo.encoding.name)}
            <option value={fileInfo.encoding.name}>{fileInfo.encoding.name}</option>
          {/if}
          {#each encodings as encoding}
            <option value={encoding}>{encoding}</option>
          {/each}
        </select>
      {:else}
        <span class="toolbar-info encoding-info">{fileInfo.encoding.name}</span>
      {/if}
    {:else}
      <span class="toolbar-info muted">{$t('toolbar.noFile')}</span>
    {/if}
  </div>

  <div class="toolbar-right">
    {#if fileInfo}
      <span class="toolbar-position">
        <span class="toolbar-info">
          {$t('toolbar.line', {
            line: currentLine !== null ? currentLine + 1 : firstLine + 1,
            total: fileInfo.totalLines.toLocaleString($locale),
          })}
        </span>
        <span class="toolbar-info">{$t('toolbar.marks', { count: marksCount })}</span>
      </span>
      <button class="icon tiny" title={$t('toolbar.prevMark')} on:click={() => dispatch('prevMark')}>↑</button>
      <button class="icon tiny" title={$t('toolbar.nextMark')} on:click={() => dispatch('nextMark')}>↓</button>
      <input
        class="go-line"
        type="text"
        inputmode="numeric"
        placeholder={$t('toolbar.goLine')}
        bind:value={goLine}
        on:keydown={(event) => event.key === 'Enter' && submitGo()}
      />
      <button class="ghost compact" on:click={submitGo} disabled={!goLine.trim()}>{$t('toolbar.go')}</button>
    {/if}
    {#if selectionRange}
      <span class="toolbar-info selection">{selectionRange}</span>
    {/if}
    {#if message}
      <span class="toolbar-message" title={message}>{message}</span>
    {/if}
  </div>
</div>
