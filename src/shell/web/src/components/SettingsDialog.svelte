<script lang="ts">
  import { createEventDispatcher } from 'svelte';
  import { languageOptions, t } from '../lib/i18n';
  import type { ClientInfo, HighlightRule, HighlighterSet, QuickHighlighterDef } from '../lib/types';
  import type { StreamStatus } from '../lib/sse';
  import { themeList } from '../lib/theme';
  import type { WebSettings, RecorderCommand } from '../lib/settings';
  import {
    DEFAULT_SHORTCUTS,
    FONT_SIZE_MAX,
    FONT_SIZE_MIN,
    SHORTCUT_ACTIONS,
    SHORTCUT_SLOTS,
    eventToShortcut,
  } from '../lib/settings';

  // Placeholder of each editable shortcut slot, in slot order.
  const SHORTCUT_SLOT_LABELS = [
    'settings.shortcutPrimary',
    'settings.shortcutSecondary',
  ];
  import {
    createHighlightRule,
    createHighlighterSet,
    exportHighlighterStore,
    importHighlighterStore,
    patternError,
    toApiColor,
    toHtmlColor,
  } from '../lib/highlighters';
  import ColorField from './ColorField.svelte';

  export let settings: WebSettings;
  export let sets: HighlighterSet[] = [];
  export let activeSetId = '';
  export let quickDefs: QuickHighlighterDef[] = [];
  export let recorderCommands: RecorderCommand[] = [];
  export let initialPanel:
    | 'general'
    | 'view'
    | 'search'
    | 'file'
    | 'recorder'
    | 'shortcuts'
    | 'highlighters'
    | 'labels'
    | 'about' = 'general';
  export let clientInfo: ClientInfo | null = null;
  export let connection: StreamStatus = 'connecting';
  export let availableEncodings: string[] = [];

  const FONT_OPTIONS: { label: string; value: string }[] = [
    { label: 'Cascadia Mono', value: '"Cascadia Mono", Consolas, monospace' },
    { label: 'Cascadia Code', value: '"Cascadia Code", Consolas, monospace' },
    { label: 'Consolas', value: 'Consolas, "Courier New", monospace' },
    { label: 'Courier New', value: '"Courier New", Consolas, monospace' },
    { label: 'Lucida Console', value: '"Lucida Console", Consolas, monospace' },
    { label: 'JetBrains Mono', value: '"JetBrains Mono", Consolas, monospace' },
    { label: 'Fira Code', value: '"Fira Code", Consolas, monospace' },
    { label: 'Source Code Pro', value: '"Source Code Pro", Consolas, monospace' },
    { label: 'IBM Plex Mono', value: '"IBM Plex Mono", Consolas, monospace' },
    { label: 'Roboto Mono', value: '"Roboto Mono", Consolas, monospace' },
    { label: 'Noto Sans Mono', value: '"Noto Sans Mono", Consolas, monospace' },
    { label: 'DejaVu Sans Mono', value: '"DejaVu Sans Mono", Consolas, monospace' },
    { label: 'Liberation Mono', value: '"Liberation Mono", Consolas, monospace' },
    { label: 'Menlo', value: 'Menlo, Monaco, Consolas, monospace' },
    { label: 'Monaco', value: 'Monaco, Menlo, Consolas, monospace' },
    { label: 'Ubuntu Mono', value: '"Ubuntu Mono", Consolas, monospace' },
  ];


  const dispatch = createEventDispatcher<{
    close: void;
    save: {
      settings: WebSettings;
      highlightStore: { sets: HighlighterSet[]; activeSetId: string };
      quickDefs: QuickHighlighterDef[];
      recorderCommands: RecorderCommand[];
    };
  }>();

  type Panel =
    | 'general'
    | 'view'
    | 'search'
    | 'file'
    | 'recorder'
    | 'shortcuts'
    | 'highlighters'
    | 'labels'
    | 'about';
  let panel: Panel = initialPanel;
  let localSettings: WebSettings = {
    ...settings,
    shortcuts: Object.fromEntries(
      Object.entries(settings.shortcuts).map(([action, values]) => [action, [...values]]),
    ),
  };
  let localSets: HighlighterSet[] = sets.map((set) => ({
    ...set,
    rules: set.rules.map((rule) => ({ ...rule })),
  }));
  let localActiveSetId = activeSetId || localSets[0]?.id || '';
  let importMessage = '';
  // Derived here, not in the template: a function call inside an each block is
  // not re-evaluated when the arrays it reads are reassigned.
  $: activeRuleList = localSets.find((set) => set.id === localActiveSetId)?.rules ?? [];
  let localQuickDefs: QuickHighlighterDef[] = quickDefs.map((def) => ({ ...def }));
  let localRecorderCommands: RecorderCommand[] = recorderCommands.map((item) => ({ ...item }));
  let customFont = !FONT_OPTIONS.some((option) => option.value === localSettings.fontFamily);

  function selectFont(value: string): void {
    if (value === '__custom__') {
      customFont = true;
      return;
    }
    customFont = false;
    localSettings = { ...localSettings, fontFamily: value };
  }

  $: connectionText =
    connection === 'online'
      ? $t('toolbar.connected')
      : connection === 'reconnecting'
        ? $t('toolbar.reconnecting')
        : connection === 'closed'
          ? $t('toolbar.offline')
          : $t('toolbar.connecting');

  function shortcutLabel(action: { id: string; labelKey: string }): string {
    if (action.id.startsWith('colorLabel')) {
      return `${$t(action.labelKey)} ${action.id.slice('colorLabel'.length)}`;
    }
    return $t(action.labelKey);
  }

  function captureShortcut(action: string, index: number, event: KeyboardEvent): void {
    event.preventDefault();
    if (event.key === 'Escape' || event.key === 'Tab') {
      return;
    }
    const values = [...(localSettings.shortcuts[action] ?? Array<string>(SHORTCUT_SLOTS).fill(''))];
    values[index] = eventToShortcut(event);
    localSettings = {
      ...localSettings,
      shortcuts: { ...localSettings.shortcuts, [action]: values },
    };
  }

  function resetShortcuts(): void {
    localSettings = {
      ...localSettings,
      shortcuts: Object.fromEntries(
        Object.entries(DEFAULT_SHORTCUTS).map(([action, values]) => [action, [...values]]),
      ),
    };
  }

  function addRule(): void {
    activeRuleList.push(createHighlightRule({ color: '#ff89b4fa', caseSensitive: true }));
    localSets = localSets;
  }

  function removeRule(index: number): void {
    activeRuleList.splice(index, 1);
    localSets = localSets;
  }

  function moveRule(index: number, delta: number): void {
    const target = index + delta;
    if (target < 0 || target >= activeRuleList.length) {
      return;
    }
    const [rule] = activeRuleList.splice(index, 1);
    activeRuleList.splice(target, 0, rule);
    localSets = localSets;
  }

  function selectSet(id: string): void {
    localActiveSetId = id;
  }

  function addSet(): void {
    const set = createHighlighterSet($t('settings.newSetName'));
    localSets = [...localSets, set];
    localActiveSetId = set.id;
  }

  function removeSet(id: string): void {
    if (localSets.length <= 1) {
      return;
    }
    localSets = localSets.filter((set) => set.id !== id);
    if (localActiveSetId === id) {
      localActiveSetId = localSets[0].id;
    }
  }

  function moveSet(index: number, delta: number): void {
    const target = index + delta;
    if (target < 0 || target >= localSets.length) {
      return;
    }
    const next = [...localSets];
    const [set] = next.splice(index, 1);
    next.splice(target, 0, set);
    localSets = next;
  }

  function exportSets(): void {
    const text = exportHighlighterStore({ sets: localSets, activeSetId: localActiveSetId });
    const blob = new Blob([text], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = 'klogg-highlighters.json';
    anchor.click();
    URL.revokeObjectURL(url);
  }

  async function importSets(event: Event): Promise<void> {
    const input = event.currentTarget as HTMLInputElement;
    const file = input.files?.[0];
    input.value = '';
    if (!file) {
      return;
    }
    try {
      const result = importHighlighterStore(
        { sets: localSets, activeSetId: localActiveSetId },
        await file.text(),
      );
      localSets = result.store.sets;
      localActiveSetId = result.store.activeSetId;
      importMessage = $t('settings.importedSets', {
        imported: result.imported,
        skipped: result.skipped,
      });
    } catch {
      importMessage = $t('settings.importFailed');
    }
    window.setTimeout(() => (importMessage = ''), 4000);
  }

  function addRecorder(): void {
    localRecorderCommands = [...localRecorderCommands, { name: '', command: '' }];
  }

  function removeRecorder(index: number): void {
    localRecorderCommands = localRecorderCommands.filter((_, item) => item !== index);
  }

  function setRuleColor(rule: HighlightRule, key: 'color' | 'backColor', value: string): void {
    rule[key] = toApiColor(value);
    localSets = localSets;
  }

  function setQuickColor(def: QuickHighlighterDef, key: 'color' | 'backColor', value: string): void {
    def[key] = toApiColor(value);
    localQuickDefs = localQuickDefs;
  }

  function quickLabelName(def: QuickHighlighterDef, index: number): string {
    const match = /^Color label (\d+)$/i.exec(def.name?.trim() ?? '');
    return match ? $t('crawler.defaultColorLabel', { n: match[1] }) : def.name;
  }

  function save(): void {
    const bounded = (value: number, min: number, max: number, fallback: number) =>
      Number.isFinite(value) ? Math.min(max, Math.max(min, Math.round(value))) : fallback;
    const normalized: WebSettings = {
      ...localSettings,
      fontFamily: localSettings.fontFamily.trim() || '"Cascadia Mono", Consolas, monospace',
      fontSize: bounded(localSettings.fontSize, FONT_SIZE_MIN, FONT_SIZE_MAX, 13),
      filePollIntervalMs: bounded(localSettings.filePollIntervalMs, 500, 10000, 1000),
      recentLimit: bounded(localSettings.recentLimit, 4, 20, 15),
      searchHistoryLimit: bounded(localSettings.searchHistoryLimit, 5, 200, 60),
      defaultEncoding: localSettings.defaultEncoding.trim(),
    };
    dispatch('save', {
      settings: normalized,
      highlightStore: {
        activeSetId: localActiveSetId,
        sets: localSets.map((set) => ({
          ...set,
          name: set.name.trim() || $t('settings.newSetName'),
          rules: set.rules
            .filter((rule) => rule.pattern.trim().length > 0)
            .map((rule) => ({ ...rule, name: rule.name.trim() })),
        })),
      },
      quickDefs: localQuickDefs,
      recorderCommands: localRecorderCommands
        .map((item) => ({ name: item.name.trim(), command: item.command.trim() }))
        .filter((item) => item.name && item.command),
    });
  }
</script>

<div class="modal-backdrop" role="presentation" on:click={() => dispatch('close')}>
  <div
    class="settings-dialog"
    class:wide-panel={panel === 'highlighters'}
    role="dialog"
    aria-modal="true"
    aria-label={$t('settings.title')}
    tabindex="-1"
    on:click|stopPropagation
    on:keydown|stopPropagation
  >
    <div class="settings-header">
      <strong>{$t('settings.title')}</strong>
      <button class="ghost compact" on:click={() => dispatch('close')}>×</button>
    </div>

    <div class="settings-body">
      <nav class="settings-tabs">
        <button class:active={panel === 'general'} on:click={() => (panel = 'general')}>
          {$t('settings.tab.general')}
        </button>
        <button class:active={panel === 'view'} on:click={() => (panel = 'view')}>
          {$t('settings.tab.view')}
        </button>
        <button class:active={panel === 'search'} on:click={() => (panel = 'search')}>
          {$t('settings.tab.search')}
        </button>
        <button class:active={panel === 'file'} on:click={() => (panel = 'file')}>
          {$t('settings.tab.file')}
        </button>
        <button class:active={panel === 'recorder'} on:click={() => (panel = 'recorder')}>
          {$t('settings.tab.recorder')}
        </button>
        <button class:active={panel === 'shortcuts'} on:click={() => (panel = 'shortcuts')}>
          {$t('settings.tab.shortcuts')}
        </button>
        <button class:active={panel === 'highlighters'} on:click={() => (panel = 'highlighters')}>
          {$t('settings.tab.highlighters')}
        </button>
        <button class:active={panel === 'labels'} on:click={() => (panel = 'labels')}>
          {$t('settings.tab.labels')}
        </button>
        <button class:active={panel === 'about'} on:click={() => (panel = 'about')}>
          {$t('settings.tab.about')}
        </button>
      </nav>

      <section class="settings-content">
        {#if panel === 'general'}
          <div class="settings-section">
            <label class="settings-row">
              <span>{$t('settings.restoreSession')}</span>
              <input type="checkbox" bind:checked={localSettings.restoreSession} />
            </label>
            <label class="settings-row">
              <span>{$t('settings.language')}</span>
              <select bind:value={localSettings.language}>
                {#each languageOptions as option}
                  <option value={option.value}>{$t(option.labelKey)}</option>
                {/each}
              </select>
            </label>
            <label class="settings-row">
              <span>{$t('settings.theme')}</span>
              <select bind:value={localSettings.theme}>
                <option value="system">{$t('settings.themeSystem')}</option>
                {#each themeList as theme}
                  <option value={theme.id}>{$t(theme.labelKey)}</option>
                {/each}
              </select>
            </label>
          </div>
          <p class="settings-hint">{$t('settings.generalHint')}</p>
        {:else if panel === 'view'}
          <div class="settings-section">
            <div class="settings-row">
              <span>{$t('settings.fontFamily')}</span>
              <div class="settings-control">
                <select
                  value={customFont ? '__custom__' : localSettings.fontFamily}
                  on:change={(event) => selectFont(event.currentTarget.value)}
                >
                  {#each FONT_OPTIONS as option (option.value)}
                    <option value={option.value}>{option.label}</option>
                  {/each}
                  <option value="__custom__">{$t('settings.fontCustom')}</option>
                </select>
                {#if customFont}
                  <input class="settings-text" bind:value={localSettings.fontFamily} />
                {/if}
              </div>
            </div>
            <p class="settings-hint">{$t('settings.fontHint')}</p>
            <label class="settings-row">
              <span>{$t('settings.fontSize')}</span>
              <input
                class="settings-number"
                type="number"
                min={FONT_SIZE_MIN}
                max={FONT_SIZE_MAX}
                bind:value={localSettings.fontSize}
              />
            </label>
            <label class="settings-row">
              <span>{$t('settings.boldFont')}</span>
              <input type="checkbox" bind:checked={localSettings.boldFont} />
            </label>
            <label class="settings-row">
              <span>{$t('settings.autoWrap')}</span>
              <input type="checkbox" bind:checked={localSettings.autoWrap} />
            </label>
            <label class="settings-row">
              <span>{$t('settings.showMainLineNumbers')}</span>
              <input type="checkbox" bind:checked={localSettings.showMainLineNumbers} />
            </label>
            <label class="settings-row">
              <span>{$t('settings.showFilteredLineNumbers')}</span>
              <input type="checkbox" bind:checked={localSettings.showFilteredLineNumbers} />
            </label>
            <label class="settings-row">
              <span>{$t('settings.wordHighlightBackground')}</span>
              <ColorField
                value={localSettings.wordHighlightBackground}
                title={$t('settings.wordHighlightBackground')}
                hexTitle={$t('settings.colorHex')}
                on:change={(event) => {
                  localSettings = { ...localSettings, wordHighlightBackground: event.detail };
                }}
              />
            </label>
            <label class="settings-row">
              <span>{$t('settings.wordHighlightForeground')}</span>
              <ColorField
                value={localSettings.wordHighlightForeground}
                title={$t('settings.wordHighlightForeground')}
                hexTitle={$t('settings.colorHex')}
                on:change={(event) => {
                  localSettings = { ...localSettings, wordHighlightForeground: event.detail };
                }}
              />
            </label>
          </div>
        {:else if panel === 'search'}
          <div class="settings-section">
            <label class="settings-row">
              <span>{$t('settings.defaultSearchRegex')}</span>
              <input type="checkbox" bind:checked={localSettings.defaultSearchRegex} />
            </label>
            <label class="settings-row">
              <span>{$t('settings.defaultSearchCaseSensitive')}</span>
              <input type="checkbox" bind:checked={localSettings.defaultSearchCaseSensitive} />
            </label>
            <label class="settings-row">
              <span>{$t('settings.autoRunSearch')}</span>
              <input type="checkbox" bind:checked={localSettings.autoRunSearch} />
            </label>
            <label class="settings-row">
              <span>{$t('settings.liveSearch')}</span>
              <input type="checkbox" bind:checked={localSettings.liveSearch} />
            </label>
            <label class="settings-row">
              <span>{$t('settings.searchHistoryLimit')}</span>
              <input
                class="settings-number"
                type="number"
                min="5"
                max="200"
                bind:value={localSettings.searchHistoryLimit}
              />
            </label>
            <label class="settings-row">
              <span>{$t('settings.searchMatchCustom')}</span>
              <input type="checkbox" bind:checked={localSettings.customSearchMatchColors} />
            </label>
            <label class="settings-row">
              <span>{$t('settings.searchMatchBackground')}</span>
              <ColorField
                value={localSettings.searchMatchBackground}
                disabled={!localSettings.customSearchMatchColors}
                title={$t('settings.searchMatchBackground')}
                hexTitle={$t('settings.colorHex')}
                on:change={(event) => {
                  localSettings = { ...localSettings, searchMatchBackground: event.detail };
                }}
              />
            </label>
            <label class="settings-row">
              <span>{$t('settings.searchMatchForeground')}</span>
              <ColorField
                value={localSettings.searchMatchForeground}
                disabled={!localSettings.customSearchMatchColors}
                title={$t('settings.searchMatchForeground')}
                hexTitle={$t('settings.colorHex')}
                on:change={(event) => {
                  localSettings = { ...localSettings, searchMatchForeground: event.detail };
                }}
              />
            </label>
            <p class="settings-hint">{$t('settings.searchMatchHint')}</p>
          </div>
          <p class="settings-hint">{$t('settings.searchDefaultsHint')}</p>
        {:else if panel === 'file'}
          <div class="settings-section">
            <label class="settings-row">
              <span>{$t('settings.autoFollow')}</span>
              <input type="checkbox" bind:checked={localSettings.autoFollow} />
            </label>
            <label class="settings-row">
              <span>{$t('settings.defaultEncoding')}</span>
              <select bind:value={localSettings.defaultEncoding}>
                <option value="">{$t('settings.encodingAuto')}</option>
                {#if localSettings.defaultEncoding && !availableEncodings.includes(localSettings.defaultEncoding)}
                  <option value={localSettings.defaultEncoding}>{localSettings.defaultEncoding}</option>
                {/if}
                {#each availableEncodings as encoding (encoding)}
                  <option value={encoding}>{encoding}</option>
                {/each}
              </select>
            </label>
            <label class="settings-row">
              <span>{$t('settings.filePollInterval')}</span>
              <input
                class="settings-number"
                type="number"
                min="500"
                max="10000"
                step="500"
                bind:value={localSettings.filePollIntervalMs}
              />
            </label>
            <label class="settings-row">
              <span>{$t('settings.recentLimit')}</span>
              <input
                class="settings-number"
                type="number"
                min="4"
                max="20"
                bind:value={localSettings.recentLimit}
              />
            </label>
          </div>
          <p class="settings-hint">{$t('settings.fileHint')}</p>
        {:else if panel === 'recorder'}
          <div class="settings-section-title">
            <span>{$t('settings.recorderCommands')}</span>
            <button class="ghost compact" on:click={addRecorder}>{$t('settings.addRecorder')}</button>
          </div>
          {#if localRecorderCommands.length === 0}
            <p class="settings-hint">{$t('settings.noRecorder')}</p>
          {/if}
          {#each localRecorderCommands as item, index (index)}
            <div class="recorder-card">
              <div class="recorder-card-head">
                <input
                  class="recorder-name"
                  placeholder={$t('settings.recorderName')}
                  bind:value={item.name}
                />
                <button
                  class="ghost compact"
                  title={$t('settings.removeRecorder')}
                  on:click={() => removeRecorder(index)}
                >×</button>
              </div>
              <input
                class="recorder-command"
                placeholder={$t('settings.recorderCommandPlaceholder')}
                bind:value={item.command}
              />
            </div>
          {/each}
          <p class="settings-hint">{$t('settings.recorderHint')}</p>
        {:else if panel === 'shortcuts'}
          <div class="settings-section-title">
            <span>{$t('settings.shortcuts')}</span>
            <button class="ghost compact" on:click={resetShortcuts}>{$t('settings.reset')}</button>
          </div>
          <div class="shortcut-list">
            {#each SHORTCUT_ACTIONS as action}
              <div class="shortcut-row">
                <span>{shortcutLabel(action)}</span>
                <div class="shortcut-inputs">
                  {#each SHORTCUT_SLOT_LABELS as labelKey, slot}
                    <input
                      class="shortcut-input"
                      readonly
                      value={localSettings.shortcuts[action.id]?.[slot] ?? ''}
                      placeholder={$t(labelKey)}
                      on:keydown={(event) => captureShortcut(action.id, slot, event)}
                    />
                  {/each}
                </div>
              </div>
            {/each}
          </div>
          <p class="settings-hint">{$t('settings.shortcutsHint')}</p>
        {:else if panel === 'highlighters'}
          <div class="highlighter-layout">
            <div class="highlighter-sets">
              <div class="settings-section-title">
                <span>{$t('settings.highlighterSets')}</span>
                <button class="ghost compact" on:click={addSet} title={$t('settings.addSet')}>+</button>
              </div>
              {#each localSets as set, index (set.id)}
                <div class="set-row" class:active={set.id === localActiveSetId}>
                  {#if set.id === localActiveSetId}
                    <input
                      class="set-input"
                      placeholder={$t('settings.setName')}
                      bind:value={set.name}
                    />
                  {:else}
                    <button
                      class="set-name"
                      title={$t('settings.activateSet')}
                      on:click={() => selectSet(set.id)}
                    >{set.name}</button>
                  {/if}
                  <button
                    class="ghost compact"
                    title={$t('settings.moveUp')}
                    disabled={index === 0}
                    on:click={() => moveSet(index, -1)}
                  >↑</button>
                  <button
                    class="ghost compact"
                    title={$t('settings.moveDown')}
                    disabled={index === localSets.length - 1}
                    on:click={() => moveSet(index, 1)}
                  >↓</button>
                  <button
                    class="ghost compact"
                    title={$t('common.delete')}
                    disabled={localSets.length <= 1}
                    on:click={() => removeSet(set.id)}
                  >×</button>
                </div>
              {/each}
              <div class="set-actions">
                <button class="ghost compact" on:click={exportSets}>{$t('settings.exportSets')}</button>
                <label class="ghost compact import-button">
                  {$t('settings.importSets')}
                  <input type="file" accept="application/json,.json" on:change={importSets} />
                </label>
              </div>
              {#if importMessage}
                <p class="settings-hint">{importMessage}</p>
              {/if}
            </div>

            <div class="highlighter-rules">
              <div class="settings-section-title">
                <span>{$t('settings.highlighterRules')}</span>
                <button class="ghost compact" on:click={addRule}>{$t('settings.addRule')}</button>
              </div>
              {#if activeRuleList.length === 0}
                <p class="settings-hint">{$t('settings.noRules')}</p>
              {/if}
              {#each activeRuleList as rule, index (index)}
                <div class="rule-card" class:invalid={rule.pattern.trim() !== '' && patternError(rule) !== ''}>
                  <div class="rule-line">
                    <input type="checkbox" bind:checked={rule.enabled} title={$t('common.enabled')} />
                    <input
                      class="rule-pattern"
                      placeholder={$t('settings.pattern')}
                      bind:value={rule.pattern}
                      on:input={() => (localSets = localSets)}
                      title={rule.pattern.trim() === '' || patternError(rule) === ''
                        ? $t('settings.pattern')
                        : patternError(rule)}
                    />
                    <ColorField
                      value={toHtmlColor(rule.color)}
                      title={$t('common.foreground')}
                      hexTitle={$t('settings.colorHex')}
                      on:change={(event) => setRuleColor(rule, 'color', event.detail)}
                    />
                    <ColorField
                      value={toHtmlColor(rule.backColor || '#000000')}
                      disabled={rule.useThemeBack}
                      title={$t('common.background')}
                      hexTitle={$t('settings.colorHex')}
                      on:change={(event) => setRuleColor(rule, 'backColor', event.detail)}
                    />
                    <button
                      class="ghost compact"
                      title={$t('settings.moveUp')}
                      disabled={index === 0}
                      on:click={() => moveRule(index, -1)}
                    >↑</button>
                    <button
                      class="ghost compact"
                      title={$t('settings.moveDown')}
                      disabled={index === activeRuleList.length - 1}
                      on:click={() => moveRule(index, 1)}
                    >↓</button>
                    <button class="ghost compact" title={$t('common.delete')} on:click={() => removeRule(index)}>×</button>
                  </div>
                  <div class="rule-line options">
                    <input class="rule-name" placeholder={$t('settings.nameOptional')} bind:value={rule.name} />
                    <label><input type="checkbox" bind:checked={rule.useRegex} /> {$t('common.regex')}</label>
                    <label><input type="checkbox" bind:checked={rule.caseSensitive} /> {$t('common.caseSensitive')}</label>
                    <label><input type="checkbox" bind:checked={rule.onlyMatch} /> {$t('common.onlyMatch')}</label>
                    <label><input type="checkbox" bind:checked={rule.useThemeBack} /> {$t('settings.useThemeBack')}</label>
                    <label><input type="checkbox" bind:checked={rule.variateColors} /> {$t('common.varyColors')}</label>
                    {#if rule.variateColors}
                      <label class="variance">
                        {$t('settings.colorVariance')}
                        <input type="number" min="0" max="100" bind:value={rule.colorVariance} />
                      </label>
                    {/if}
                  </div>
                  {#if rule.pattern.trim() !== '' && patternError(rule) !== ''}
                    <p class="rule-error">{$t('settings.invalidPattern', { error: patternError(rule) })}</p>
                  {/if}
                </div>
              {/each}
              <p class="settings-hint">{$t('settings.highlighterOrderHint')}</p>
            </div>
          </div>
        {:else if panel === 'labels'}
          <div class="settings-section-title">
            <span>{$t('settings.colorLabels')}</span>
          </div>
          <p class="settings-hint">{$t('settings.colorLabelsHint')}</p>
          {#each localQuickDefs as def, index (index)}
            <div class="rule-line">
              <span class="label-index">{index + 1}</span>
              <input
                class="rule-name"
                placeholder={$t('settings.name')}
                value={quickLabelName(def, index)}
                on:input={(event) => {
                  def.name = event.currentTarget.value;
                  localQuickDefs = localQuickDefs;
                }}
              />
              <ColorField
                value={toHtmlColor(def.color)}
                title={$t('common.foreground')}
                hexTitle={$t('settings.colorHex')}
                on:change={(event) => setQuickColor(def, 'color', event.detail)}
              />
              <ColorField
                value={toHtmlColor(def.backColor)}
                title={$t('common.background')}
                hexTitle={$t('settings.colorHex')}
                on:change={(event) => setQuickColor(def, 'backColor', event.detail)}
              />
              <label><input type="checkbox" bind:checked={def.useInCycle} /> {$t('common.cycle')}</label>
            </div>
          {/each}
        {:else}
          <div class="settings-section-title">
            <span>{$t('settings.about')}</span>
          </div>
          <div class="about-grid">
            <span>{$t('settings.aboutClient')}</span>
            <code>{clientInfo?.clientId ?? '-'}</code>
            <span>{$t('settings.aboutServer')}</span>
            <code>{clientInfo?.serverVersion ?? '-'}</code>
            <span>{$t('settings.aboutApi')}</span>
            <code>{clientInfo?.apiVersion ?? '-'}</code>
            <span>{$t('settings.aboutConnection')}</span>
            <code>{connectionText}</code>
            <span>{$t('settings.aboutCapabilities')}</span>
            <code>{clientInfo?.capabilities.join(', ') ?? '-'}</code>
          </div>
          <p class="settings-hint">{$t('settings.aboutHint')}</p>
        {/if}
      </section>
    </div>

    <div class="settings-footer">
      <button class="ghost" on:click={() => dispatch('close')}>{$t('common.cancel')}</button>
      <button class="primary" on:click={save}>{$t('common.save')}</button>
    </div>
  </div>
</div>
