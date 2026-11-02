<script lang="ts">
  import { createEventDispatcher, onMount } from 'svelte';
  import { t } from '../lib/i18n';
  import type { ApiClient } from '../lib/api';
  import { ApiError } from '../lib/api';
  import type { RecorderCommand } from '../lib/settings';
  import type { BrowseEntry } from '../lib/types';
  
  export let api: ApiClient;
  export let startPath = '';
  export let favorites: string[] = [];
  export let recentFiles: string[] = [];
  export let recorderCommands: RecorderCommand[] = [];
  export let recorderTargetCommand = '';

  type Source = 'all' | 'latest' | 'favorites' | 'directory' | 'url' | 'recorder';

  const sources: { id: Source; label: string }[] = [
    { id: 'all', label: 'app.welcome.tab.all' },
    { id: 'latest', label: 'app.welcome.tab.latest' },
    { id: 'favorites', label: 'app.welcome.tab.favorites' },
    { id: 'directory', label: 'app.welcome.tab.directory' },
    { id: 'url', label: 'app.welcome.tab.url' },
    { id: 'recorder', label: 'app.welcome.tab.recorder' },
  ];

  const dispatch = createEventDispatcher<{
    close: void;
    open: string;
    openWithRecorder: { path: string; command: string };
    manageRecorder: void;
  }>();

  let source: Source = 'all';
  let path = '';
  let parent: string | null = null;
  let roots: { name: string; path: string }[] = [];
  let entries: BrowseEntry[] = [];
  let selected = '';
  let filter = '';
  let loading = false;
  let error = '';
  let truncated = false;
  let pathInput = '';
  let urlInput = '';
  let sourceFilter = '';
  let actionError = '';
  let selectedRecorderCommand = '';

  $: visibleEntries = entries.filter((entry) =>
    entry.name.toLowerCase().includes(filter.trim().toLowerCase()),
  );
  $: selectedEntry = entries.find((entry) => entry.path === selected) ?? null;
  $: activeRecorderCommand =
    recorderCommands.find((command) => command.name === selectedRecorderCommand) ?? null;
  $: normalizedSourceFilter = sourceFilter.trim().toLowerCase();
  $: filteredFavorites = favorites.filter((favorite) =>
    matchesQuery(favorite, normalizedSourceFilter),
  );
  $: filteredRecent = recentFiles.filter((recent) =>
    matchesQuery(recent, normalizedSourceFilter),
  );
  $: filteredRecorderCommands = recorderCommands.filter(
    (command) =>
      matchesQuery(command.name, normalizedSourceFilter) ||
      matchesQuery(command.command, normalizedSourceFilter),
  );

  async function load(target: string): Promise<void> {
    loading = true;
    error = '';
    selected = '';
    try {
      const result = await api.listDirectory(target);
      path = result.path;
      pathInput = result.path;
      parent = result.parent;
      roots = result.roots;
      entries = result.entries;
      truncated = result.truncated;

      // If the caller opened the dialog from a file path, preselect it.
      if (startPath) {
        const match = entries.find((entry) => entry.path === startPath && !entry.directory);
        if (match) {
          selected = match.path;
        }
      }
    } catch (loadError) {
      error =
        loadError instanceof ApiError
          ? `${loadError.code}: ${loadError.message}`
          : String(loadError);
      entries = [];
      parent = null;
      truncated = false;
    } finally {
      loading = false;
    }
  }

  function openFile(target: string): void {
    const filePath = target.trim();
    if (!filePath) {
      return;
    }
    if (selectedRecorderCommand && /^[a-z][a-z0-9+.-]*:\/\//i.test(filePath)) {
      actionError = $t('fileBrowser.recorderLocalOnly');
      return;
    }
    actionError = '';
    if (selectedRecorderCommand) {
      dispatch('openWithRecorder', { path: filePath, command: selectedRecorderCommand });
    } else {
      dispatch('open', filePath);
    }
  }

  function activate(entry: BrowseEntry): void {
    if (entry.directory) {
      void load(entry.path);
    } else {
      selected = entry.path;
      openFile(entry.path);
    }
  }

  function select(entry: BrowseEntry): void {
    if (entry.directory) {
      selected = '';
    } else {
      selected = entry.path;
    }
  }

  function submitPath(): void {
    if (pathInput.trim()) {
      void load(pathInput.trim());
    }
  }

  function openSelected(): void {
    if (selected) {
      openFile(selected);
    }
  }

  function selectSource(next: Source): void {
    source = next;
    sourceFilter = '';
    actionError = '';
  }

  function chooseRecorder(name: string): void {
    selectedRecorderCommand = name;
    source = 'directory';
    actionError = '';
  }

  function clearRecorderTarget(): void {
    selectedRecorderCommand = '';
    source = 'recorder';
    actionError = '';
  }

  function openUrl(): void {
    openFile(urlInput);
  }

  function matchesQuery(value: string, query: string): boolean {
    if (!query) {
      return true;
    }
    return value.toLowerCase().includes(query) || basename(value).toLowerCase().includes(query);
  }

  function basename(filePath: string): string {
    return filePath.split(/[\\/]/).pop() || filePath;
  }

  function formatSize(bytes: number): string {
    if (!bytes) return '';
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KiB`;
    if (bytes < 1024 * 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MiB`;
    return `${(bytes / 1024 / 1024 / 1024).toFixed(2)} GiB`;
  }

  function onKeydown(event: KeyboardEvent): void {
    if (event.key === 'Escape') {
      event.preventDefault();
      dispatch('close');
    }
  }

  onMount(() => {
    if (recorderTargetCommand) {
      selectedRecorderCommand = recorderTargetCommand;
      source = 'directory';
    }
    void load(startPath || '.');
    window.addEventListener('keydown', onKeydown);
    return () => window.removeEventListener('keydown', onKeydown);
  });
</script>

<div class="modal-backdrop" role="presentation" on:click|self={() => dispatch('close')}>
  <div class="file-dialog" role="dialog" aria-modal="true" aria-label={$t('fileBrowser.openLog')}>
    <div class="file-dialog-header">
      <div>
        <strong>{$t('fileBrowser.openLog')}</strong>
        <span class="muted">{$t('fileBrowser.sources')}</span>
      </div>
      <button class="ghost icon" on:click={() => dispatch('close')} title={$t('common.close')}>×</button>
    </div>

    <div class="source-tabs file-source-tabs">
      {#each sources as item (item.id)}
        <button class:active={source === item.id} on:click={() => selectSource(item.id)}>
          {$t(item.label)}
        </button>
      {/each}
    </div>

    {#if source === 'all'}
      <div class="file-source-body">
        <p class="file-source-hint">{$t('fileBrowser.allHint')}</p>
        {#if favorites.length || recentFiles.length || recorderCommands.length}
          <div class="file-source-search">
            <input
              class="file-filter source-filter"
              type="text"
              spellcheck="false"
              placeholder={$t('fileBrowser.sourceSearch')}
              bind:value={sourceFilter}
            />
          </div>

          {#if filteredFavorites.length}
            <div class="file-source-section">
              <div class="file-source-title">{$t('favorites.title')}</div>
              {#each filteredFavorites as favorite (favorite)}
                <button class="file-source-row" title={favorite} on:click={() => openFile(favorite)}>
                  <span class="file-source-primary">★ {basename(favorite)}</span>
                  <span class="file-source-secondary">{favorite}</span>
                </button>
              {/each}
            </div>
          {/if}

          {#if filteredRecent.length}
            <div class="file-source-section">
              <div class="file-source-title">{$t('app.welcome.recent')}</div>
              {#each filteredRecent as recent (recent)}
                <button class="file-source-row" title={recent} on:click={() => openFile(recent)}>
                  <span class="file-source-primary">{basename(recent)}</span>
                  <span class="file-source-secondary">{recent}</span>
                </button>
              {/each}
            </div>
          {/if}

          {#if filteredRecorderCommands.length}
            <div class="file-source-section">
              <div class="file-source-title">{$t('app.welcome.tab.recorder')}</div>
              {#each filteredRecorderCommands as command (command.name)}
                <button class="file-source-row" on:click={() => chooseRecorder(command.name)}>
                  <span class="file-source-primary">{command.name}</span>
                  <span class="file-source-secondary file-source-command">{command.command}</span>
                </button>
              {/each}
            </div>
          {/if}

          {#if !filteredFavorites.length && !filteredRecent.length && !filteredRecorderCommands.length}
            <div class="file-empty">{$t('fileBrowser.noMatches')}</div>
          {/if}
        {:else}
          <div class="file-empty">{$t('fileBrowser.noSources')}</div>
        {/if}
      </div>
    {:else if source === 'latest'}
      <div class="file-source-body">
        {#if recentFiles.length}
          <div class="file-source-search">
            <input
              class="file-filter source-filter"
              type="text"
              spellcheck="false"
              placeholder={$t('fileBrowser.sourceSearch')}
              bind:value={sourceFilter}
            />
          </div>
          {#if filteredRecent.length}
            {#each filteredRecent as recent (recent)}
              <button class="file-source-row" title={recent} on:click={() => openFile(recent)}>
                <span class="file-source-primary">{basename(recent)}</span>
                <span class="file-source-secondary">{recent}</span>
              </button>
            {/each}
          {:else}
            <div class="file-empty">{$t('fileBrowser.noMatches')}</div>
          {/if}
        {:else}
          <div class="file-empty">{$t('app.welcome.noRecent')}</div>
        {/if}
      </div>
    {:else if source === 'favorites'}
      <div class="file-source-body">
        {#if favorites.length}
          <div class="file-source-search">
            <input
              class="file-filter source-filter"
              type="text"
              spellcheck="false"
              placeholder={$t('fileBrowser.sourceSearch')}
              bind:value={sourceFilter}
            />
          </div>
          {#if filteredFavorites.length}
            {#each filteredFavorites as favorite (favorite)}
              <button class="file-source-row" title={favorite} on:click={() => openFile(favorite)}>
                <span class="file-source-primary">★ {basename(favorite)}</span>
                <span class="file-source-secondary">{favorite}</span>
              </button>
            {/each}
          {:else}
            <div class="file-empty">{$t('fileBrowser.noMatches')}</div>
          {/if}
        {:else}
          <div class="file-empty">{$t('app.welcome.noFavorites')}</div>
        {/if}
      </div>
    {:else if source === 'directory'}
      {#if selectedRecorderCommand}
        <div class="recorder-target-banner">
          <span>
            {$t('fileBrowser.recorderTarget', {
              name: activeRecorderCommand?.name ?? selectedRecorderCommand,
            })}
          </span>
          <button class="ghost" on:click={clearRecorderTarget}>
            {$t('fileBrowser.cancelRecorderTarget')}
          </button>
        </div>
      {/if}

      <div class="file-toolbar">
        <button class="ghost" on:click={() => parent && load(parent)} disabled={!parent}>
          {$t('fileBrowser.parent')}
        </button>
        <div class="roots">
          {#each roots as root}
            <button class="ghost root-chip" on:click={() => load(root.path)} title={root.path}>
              {root.name}
            </button>
          {/each}
        </div>
      </div>

      <div class="file-toolbar">
        <input
          class="file-path"
          type="text"
          spellcheck="false"
          bind:value={pathInput}
          on:keydown={(event) => event.key === 'Enter' && submitPath()}
        />
        <button on:click={submitPath} disabled={!pathInput.trim()}>{$t('fileBrowser.go')}</button>
        <input
          class="file-filter"
          type="text"
          spellcheck="false"
          placeholder={$t('fileBrowser.filter')}
          bind:value={filter}
        />
      </div>

      <div class="file-list">
        {#if loading}
          <div class="file-empty">{$t('common.loading')}</div>
        {:else if error}
          <div class="file-empty error">{error}</div>
        {:else if actionError}
          <div class="file-empty error">{actionError}</div>
        {:else if visibleEntries.length === 0}
          <div class="file-empty">{$t('fileBrowser.noFiles')}</div>
        {:else}
          {#each visibleEntries as entry (entry.path)}
            <button
              class="file-row"
              class:selected={selected === entry.path}
              title={entry.path}
              on:click={() => select(entry)}
              on:dblclick={() => activate(entry)}
            >
              <span class="file-icon">{entry.directory ? '📁' : '📄'}</span>
              <span class="file-name">{entry.name}</span>
              <span class="file-size">{entry.directory ? '' : formatSize(entry.sizeBytes)}</span>
            </button>
          {/each}
        {/if}
        {#if truncated}
          <div class="file-empty">{$t('fileBrowser.tooMany')}</div>
        {/if}
      </div>
    {:else if source === 'url'}
      <div class="file-source-body">
        <p class="file-source-hint">{$t('fileBrowser.urlHint')}</p>
        <div class="file-url-row">
          <input
            class="file-path"
            type="text"
            spellcheck="false"
            placeholder={$t('fileBrowser.urlPlaceholder')}
            bind:value={urlInput}
            on:keydown={(event) => event.key === 'Enter' && openUrl()}
          />
          <button class="primary" on:click={openUrl} disabled={!urlInput.trim()}>
            {$t('fileBrowser.openUrl')}
          </button>
        </div>
        {#if actionError}
          <div class="file-empty error">{actionError}</div>
        {/if}
      </div>
    {:else}
      <div class="file-source-body">
        <p class="file-source-hint">{$t('fileBrowser.recorderHint')}</p>
        {#if selectedRecorderCommand}
          <div class="recorder-target-banner">
            <span>
              {$t('fileBrowser.recorderSelected', {
                name: activeRecorderCommand?.name ?? selectedRecorderCommand,
              })}
            </span>
            <button class="primary" on:click={() => selectSource('directory')}>
              {$t('fileBrowser.chooseRecorderTarget')}
            </button>
            <button class="ghost" on:click={clearRecorderTarget}>
              {$t('fileBrowser.cancelRecorderTarget')}
            </button>
          </div>
        {/if}
        {#if recorderCommands.length}
          {#each recorderCommands as command (command.name)}
            <button
              class="file-source-row"
              class:selected={command.name === selectedRecorderCommand}
              on:click={() => chooseRecorder(command.name)}
            >
              <span class="file-source-primary">{command.name}</span>
              <span class="file-source-secondary file-source-command">{command.command}</span>
            </button>
          {/each}
        {:else}
          <div class="file-empty">{$t('app.welcome.noRecorderCommands')}</div>
        {/if}
        <div class="file-source-actions">
          <button class="ghost" on:click={() => dispatch('manageRecorder')}>
            {$t('app.welcome.manageRecorder')}
          </button>
        </div>
      </div>
    {/if}

    <div class="file-dialog-footer">
      {#if source === 'directory'}
        <span class="selected-path" title={selected}>{selected || path}</span>
        <button on:click={() => dispatch('close')}>{$t('common.cancel')}</button>
        <button class="primary" on:click={openSelected} disabled={!selectedEntry}>
          {selectedRecorderCommand ? $t('fileBrowser.openAndRecord') : $t('common.open')}
        </button>
      {:else}
        <span class="selected-path"></span>
        <button on:click={() => dispatch('close')}>{$t('common.close')}</button>
      {/if}
    </div>
  </div>
</div>
