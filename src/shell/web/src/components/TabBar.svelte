<script lang="ts">
  import { createEventDispatcher } from 'svelte';
  import { t } from '../lib/i18n';
  import type { StreamStatus } from '../lib/sse';
  import type { TabMeta } from '../lib/types';
  import ConnectionBadge from './ConnectionBadge.svelte';
  import FileTab from './FileTab.svelte';

  export let tabs: TabMeta[] = [];
  export let activeId = '';
  export let connection: StreamStatus = 'connecting';
  /** Polarity of the theme the quick toggle switches to, not the current one. */
  export let nextDark = false;
  export let themeHint = '';

  $: pinnedTabs = tabs.filter((tab) => tab.pinned);
  $: unpinnedTabs = tabs.filter((tab) => !tab.pinned);

  const dispatch = createEventDispatcher<{
    select: string;
    close: string;
    newTab: void;
    settings: void;
    toggleTheme: void;
    contextMenu: { tabId: string; x: number; y: number };
    reorder: { id: string; targetId: string; after: boolean };
  }>();

  // The desktop tab bar is movable; dragging a tab over another one reorders
  // them. Pinned and unpinned tabs are separate groups, so a drag that crosses
  // the group boundary is rejected instead of silently repinning.
  let draggedId: string | null = null;
  let dropTarget: { id: string; after: boolean } | null = null;

  function draggedTabElement(event: DragEvent): HTMLElement | null {
    const target = event.target as HTMLElement | null;
    return target?.closest<HTMLElement>('.file-tab') ?? null;
  }

  function handleDragStart(event: DragEvent): void {
    const element = draggedTabElement(event);
    const id = element?.dataset.tabId ?? '';
    if (!id) {
      return;
    }
    draggedId = id;
    dropTarget = null;
    if (event.dataTransfer) {
      event.dataTransfer.effectAllowed = 'move';
      event.dataTransfer.setData('text/plain', id);
    }
  }

  function handleDragOver(event: DragEvent): void {
    if (!draggedId) {
      return;
    }
    const element = draggedTabElement(event);
    if (!element) {
      dropTarget = null;
      return;
    }
    const source = tabs.find((tab) => tab.id === draggedId);
    const target = tabs.find((tab) => tab.id === element.dataset.tabId);
    if (!source || !target || source.pinned !== target.pinned || source.id === target.id) {
      dropTarget = null;
      return;
    }
    event.preventDefault();
    if (event.dataTransfer) {
      event.dataTransfer.dropEffect = 'move';
    }
    const rect = element.getBoundingClientRect();
    dropTarget = { id: target.id, after: event.clientX > rect.left + rect.width / 2 };
  }

  function handleDrop(event: DragEvent): void {
    event.preventDefault();
    const target = dropTarget;
    const id = draggedId;
    draggedId = null;
    dropTarget = null;
    if (id && target) {
      dispatch('reorder', { id, targetId: target.id, after: target.after });
    }
  }

  function handleDragEnd(): void {
    draggedId = null;
    dropTarget = null;
  }

  function sideFor(tab: TabMeta): 'before' | 'after' | null {
    if (!dropTarget || dropTarget.id !== tab.id || draggedId === tab.id) {
      return null;
    }
    return dropTarget.after ? 'after' : 'before';
  }
</script>

<div class="tabbar" role="tablist">
  {#if pinnedTabs.length}
    <div
      class="tabbar-pinned"
      role="presentation"
      on:dragstart={handleDragStart}
      on:dragover={handleDragOver}
      on:drop={handleDrop}
      on:dragend={handleDragEnd}
    >
      {#each pinnedTabs as tab (tab.id)}
        <FileTab
          {tab}
          active={tab.id === activeId}
          dropSide={sideFor(tab)}
          on:select={(event) => dispatch('select', event.detail)}
          on:close={(event) => dispatch('close', event.detail)}
          on:contextMenu={(event) => dispatch('contextMenu', event.detail)}
        />
      {/each}
    </div>
    <div class="tabbar-divider" role="presentation"></div>
  {/if}
  <div
    class="tabbar-scroll"
    role="presentation"
    on:dragstart={handleDragStart}
    on:dragover={handleDragOver}
    on:drop={handleDrop}
    on:dragend={handleDragEnd}
  >
    {#each unpinnedTabs as tab (tab.id)}
      <FileTab
        {tab}
        active={tab.id === activeId}
        dropSide={sideFor(tab)}
        on:select={(event) => dispatch('select', event.detail)}
        on:close={(event) => dispatch('close', event.detail)}
        on:contextMenu={(event) => dispatch('contextMenu', event.detail)}
      />
    {/each}
  </div>
  <button class="file-tab-new" title={$t('tab.openFile')} on:click={() => dispatch('newTab')}>＋</button>
  <span class="tabbar-spacer"></span>
  <ConnectionBadge {connection} />
  <button
    class="settings-button theme-toggle"
    title={themeHint}
    aria-label={themeHint}
    on:click={() => dispatch('toggleTheme')}
  >
    {#if nextDark}
      <svg viewBox="0 0 16 16" width="14" height="14" fill="none" stroke="currentColor" stroke-width="1.4" aria-hidden="true">
        <path d="M13.2 9.6A5.6 5.6 0 0 1 6.4 2.8a5.6 5.6 0 1 0 6.8 6.8Z" />
      </svg>
    {:else}
      <svg viewBox="0 0 16 16" width="14" height="14" fill="none" stroke="currentColor" stroke-width="1.4" aria-hidden="true">
        <circle cx="8" cy="8" r="3.2" />
        <path d="M8 1.5v2M8 12.5v2M1.5 8h2M12.5 8h2M3.4 3.4l1.4 1.4M11.2 11.2l1.4 1.4M12.6 3.4l-1.4 1.4M4.8 11.2l-1.4 1.4" />
      </svg>
    {/if}
  </button>
  <button class="settings-button" title={$t('tab.settings')} on:click={() => dispatch('settings')}>
    <span class="settings-icon">⚙</span>
    <span>{$t('tab.settings')}</span>
  </button>
</div>
