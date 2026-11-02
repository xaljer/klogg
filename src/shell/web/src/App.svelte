<script lang="ts">
  import { onDestroy, onMount } from 'svelte';

  import { ApiClient, ApiError } from './lib/api';
  import { EventStream, type StreamStatus } from './lib/sse';
  import type { ClientInfo, FileInfo, HighlightRule, QuickHighlighterDef, ServerEvent } from './lib/types';
  import { lastPath, loadRecent, rememberRecent } from './lib/storage';
  import { desktopBridge, dialogDirectory } from './lib/desktop';
  import {
    activeRules,
    loadHighlighterStore,
    loadQuickDefs,
    saveHighlighterStore,
    saveQuickDefs,
    type HighlighterStore,
  } from './lib/highlighters';
  import { applyLocale, t } from './lib/i18n';
  import { isFavorite, loadFavorites, removeFavorite, toggleFavorite } from './lib/favorites';
  import { loadSession, saveSession } from './lib/session';
  import { loadTabNames, setTabName } from './lib/tab-names';
  import { reorderTabList } from './lib/tab-order';
  import {
    applyTheme,
    nextTheme,
    onSystemThemeChange,
    resolveTheme,
    themes,
    type ThemeDefinition,
  } from './lib/theme';
  import { resolveSearchMatchColors } from './lib/search-colors';
  import {
    FONT_SIZE_MAX,
    FONT_SIZE_MIN,
    loadSettings,
    matchesShortcut,
    saveSettings,
    type RecorderCommand,
    type WebSettings,
  } from './lib/settings';

  import ConnectionBadge from './components/ConnectionBadge.svelte';
  import ContextMenu from './components/ContextMenu.svelte';
  import CrawlerPane from './components/CrawlerPane.svelte';
  import FileBrowser from './components/FileBrowser.svelte';
  import OpenLocationDialog from './components/OpenLocationDialog.svelte';
  import SettingsDialog from './components/SettingsDialog.svelte';
  import TabBar from './components/TabBar.svelte';

  interface MenuItem {
    id: string;
    label: string;
    shortcut?: string;
    disabled?: boolean;
    separator?: boolean;
    checked?: boolean;
    swatch?: string;
    children?: MenuItem[];
  }

  interface TabDescriptor {
    id: string;
    path: string;
    title: string;
    pane: any;
    restoreState?: Record<string, unknown> | null;
    recorderCommand?: string;
    pinned: boolean;
  }

  const authToken = new URLSearchParams(window.location.search).get('token') ?? '';
  const api = new ApiClient(authToken);
  const desktop = desktopBridge();

  let clientInfo: ClientInfo | null = null;
  let stream: EventStream | null = null;
  let connection: StreamStatus = 'connecting';

  let tabs: TabDescriptor[] = [];
  let activeId = '';
  let nextTabId = 1;

  let browserOpen = false;
  let browserStartPath = '';
  let locationOpen = false;
  let locationInitial = '';
  let settingsOpen = false;
  let openSource:
    | 'all'
    | 'latest'
    | 'favorites'
    | 'directory'
    | 'url'
    | 'recorder' = 'all';
  let pendingRecorderCommand: string | null = null;
  let settingsInitialPanel:
    | 'general'
    | 'view'
    | 'search'
    | 'file'
    | 'recorder'
    | 'shortcuts'
    | 'highlighters'
    | 'labels'
    | 'about' = 'general';
  let settings: WebSettings = loadSettings();
  let recorderCommands: RecorderCommand[] = [];
  let recorderBindings: Record<string, string> = {};
  applyLocale(settings.language);
  let currentTheme: ThemeDefinition = resolveTheme(settings.theme);
  applyTheme(currentTheme);
  // The tab bar toggle advertises and switches to this theme, so its tooltip and
  // icon always describe the destination rather than the current polarity.
  $: nextThemeDef = themes[nextTheme(currentTheme.id)];

  function applySearchMatchColors(colors: { cssFore: string; cssBack: string }): void {
    if (typeof document === 'undefined') {
      return;
    }
    document.documentElement.style.setProperty('--search-match-bg', colors.cssBack);
    document.documentElement.style.setProperty('--search-match-fg', colors.cssFore);
  }

  // One pair drives the server-rendered viewport and the browser-rendered
  // filtered view, so a match looks identical in both panes.
  $: searchMatchColors = resolveSearchMatchColors(currentTheme, {
    custom: settings.customSearchMatchColors,
    background: settings.searchMatchBackground,
    foreground: settings.searchMatchForeground,
  });
  $: applySearchMatchColors(searchMatchColors);

  function applyWordHighlight(background: string, foreground: string): void {
    if (typeof document === 'undefined') {
      return;
    }
    document.documentElement.style.setProperty('--word-highlight-bg', background);
    document.documentElement.style.setProperty('--word-highlight-fg', foreground);
  }

  $: applyWordHighlight(settings.wordHighlightBackground, settings.wordHighlightForeground);
  $: paneTheme = {
    ...currentTheme,
    palette: {
      ...currentTheme.palette,
      matchFore: searchMatchColors.apiFore,
      matchBack: searchMatchColors.apiBack,
    },
    xterm: {
      ...currentTheme.xterm,
      selectionBackground: settings.wordHighlightBackground,
      selectionForeground: settings.wordHighlightForeground,
      selectionInactiveBackground: settings.wordHighlightBackground,
    },
  };
  let highlightStore: HighlighterStore = loadHighlighterStore();
  $: highlightRules = activeRules(highlightStore);
  let quickDefs: QuickHighlighterDef[] = loadQuickDefs();
  let recentFiles = loadRecent(settings.recentLimit);
  let favorites = loadFavorites();
  let tabNames = loadTabNames();
  let availableEncodings: string[] = [];
  let shellError = '';
  let sessionTimer: number | null = null;

  let contextMenu:
    | {
        x: number;
        y: number;
        items: MenuItem[];
        tabId: string;
      }
    | null = null;

  $: activeTab = tabs.find((tab) => tab.id === activeId) ?? null;
  $: activePane = activeTab?.pane ?? null;

  function messageOf(error: unknown): string {
    return error instanceof ApiError ? `${error.code}: ${error.message}` : String(error);
  }

  function basename(path: string): string {
    const parts = path.split(/[\\/]/);
    return parts[parts.length - 1] || path;
  }

  function normPath(path: string): string {
    const text = path.trim().replace(/\//g, '\\').replace(/\\+$/, '');
    return text.toLowerCase();
  }

  function displayName(path: string): string {
    return tabNames[path] ?? basename(path);
  }

  function setTabMeta(id: string, path: string, title?: string): void {
    tabs = tabs.map((tab) =>
      tab.id === id ? { ...tab, path, title: title ?? displayName(path) } : tab,
    );
  }

  function renameTab(tabId: string): void {
    const tab = tabs.find((item) => item.id === tabId);
    if (!tab) {
      return;
    }
    const name = window.prompt($t('app.tab.renamePrompt'), tab.title);
    if (name === null) {
      return;
    }
    tabNames = setTabName(tab.path, name, tabNames);
    setTabMeta(tab.id, tab.path, displayName(tab.path));
  }

  function resetTabName(tabId: string): void {
    const tab = tabs.find((item) => item.id === tabId);
    if (!tab) {
      return;
    }
    tabNames = setTabName(tab.path, '', tabNames);
    setTabMeta(tab.id, tab.path, basename(tab.path));
  }

  function parentPath(path: string): string {
    if (/^https?:\/\//i.test(path)) {
      return '';
    }
    const normalized = path.replace(/[\\/]+$/, '');
    const index = Math.max(normalized.lastIndexOf('\\'), normalized.lastIndexOf('/'));
    if (index <= 0) {
      return normalized;
    }
    const head = normalized.slice(0, index);
    return /^[A-Za-z]:$/.test(head) ? `${head}\\` : head;
  }

  function openInNewWindow(path: string): void {
    const url = new URL(window.location.href);
    url.searchParams.set('file', path);
    window.open(url.toString(), '_blank', 'noopener');
  }

  function updateUrl(path: string | null): void {
    try {
      const url = new URL(window.location.href);
      if (path) {
        url.searchParams.set('file', path);
      } else {
        url.searchParams.delete('file');
      }
      window.history.replaceState(null, '', url.toString());
    } catch {
      // history may be unavailable in sandboxed frames
    }
  }

  function toggleCurrentFavorite(path: string): void {
    favorites = toggleFavorite(path, favorites);
  }

  function removeFavoritePath(path: string): void {
    favorites = removeFavorite(path, favorites);
  }

  function selectTab(id: string): void {
    if (tabs.some((tab) => tab.id === id)) {
      activeId = id;
      updateUrl(tabs.find((tab) => tab.id === id)?.path ?? null);
      persistSession();
    }
  }

  function openPath(
    path: string,
    restoreState: Record<string, unknown> | null = null,
    recorderCommand?: string,
    trackRecent = true,
    pinned = false,
  ): void {
    const trimmed = path.trim();
    if (!trimmed) {
      return;
    }

    const existing = tabs.find((tab) => normPath(tab.path) === normPath(trimmed));
    if (existing) {
      selectTab(existing.id);
      if (recorderCommand) {
        void existing.pane?.startRecorderByName?.(recorderCommand);
      }
      return;
    }

    const id = `tab-${nextTabId++}`;
    const tab: TabDescriptor = {
      id,
      path: trimmed,
      title: displayName(trimmed),
      pane: null,
      restoreState,
      recorderCommand,
      pinned,
    };
    if (pinned) {
      const next = [...tabs];
      const pinnedCount = next.filter((item) => item.pinned).length;
      next.splice(pinnedCount, 0, tab);
      tabs = next;
    } else {
      tabs = [...tabs, tab];
    }
    activeId = id;
    if (trackRecent) {
      recentFiles = rememberRecent(trimmed, settings.recentLimit);
    }
    updateUrl(trimmed);
    persistSession();
  }

  async function closeTab(id: string): Promise<void> {
    const tab = tabs.find((item) => item.id === id);
    if (!tab) {
      return;
    }
    const index = tabs.indexOf(tab);
    try {
      await tab.pane?.close();
    } catch {
      // pane close is best effort; the server also drops files on disconnect
    }
    tabs = tabs.filter((item) => item.id !== id);
    if (activeId === id) {
      const next = tabs[Math.min(index, tabs.length - 1)];
      activeId = next?.id ?? '';
      updateUrl(next?.path ?? null);
    }
    persistSession();
  }

  async function closeOthers(keepId: string): Promise<void> {
    const keep = tabs.find((tab) => tab.id === keepId);
    for (const tab of [...tabs]) {
      if (tab.id !== keepId && !tab.pinned) {
        await closeTab(tab.id);
      }
    }
    if (keep) {
      selectTab(keep.id);
    }
  }

  async function closeMany(ids: string[]): Promise<void> {
    for (const id of ids) {
      await closeTab(id);
    }
  }

  function closeLeft(tabId: string): void {
    const index = tabs.findIndex((tab) => tab.id === tabId);
    if (index > 0) {
      void closeMany(
        tabs
          .slice(0, index)
          .filter((tab) => !tab.pinned)
          .map((tab) => tab.id),
      );
    }
  }

  function closeRight(tabId: string): void {
    const index = tabs.findIndex((tab) => tab.id === tabId);
    if (index >= 0 && index < tabs.length - 1) {
      void closeMany(
        tabs
          .slice(index + 1)
          .filter((tab) => !tab.pinned)
          .map((tab) => tab.id),
      );
    }
  }

  function closeAll(): void {
    void closeMany(tabs.map((tab) => tab.id));
  }

  function toggleTabPinned(tabId: string): void {
    const tab = tabs.find((item) => item.id === tabId);
    if (!tab) {
      return;
    }
    tab.pinned = !tab.pinned;
    const rest = tabs.filter((item) => item.id !== tabId);
    const pinnedCount = rest.filter((item) => item.pinned).length;
    rest.splice(pinnedCount, 0, tab);
    tabs = rest;
    persistSession();
  }

  // Drag and drop reorder of the tab bar; the dragged tab keeps its pinned
  // state, so only moves inside the same group are accepted.
  function reorderTabs(detail: { id: string; targetId: string; after: boolean }): void {
    const next = reorderTabList(tabs, detail);
    if (next !== tabs) {
      tabs = next;
      persistSession();
    }
  }

  async function copyText(text: string): Promise<void> {
    if (!text) {
      return;
    }
    try {
      await navigator.clipboard.writeText(text);
    } catch {
      const area = document.createElement('textarea');
      area.value = text;
      area.style.position = 'fixed';
      area.style.opacity = '0';
      document.body.appendChild(area);
      area.select();
      try {
        document.execCommand('copy');
      } catch {
        shellError = $t('app.copyFailed');
      }
      area.remove();
    }
  }

  function showBrowser(startPath = ''): void {
    const activePath = activeTab?.path ?? '';
    const candidate = startPath || activePath;
    browserStartPath = /^https?:\/\//i.test(candidate) ? '' : candidate;
    browserOpen = true;
  }

  // The desktop shell uses the OS file manager (Explorer/Finder/portal); the
  // browser build keeps the server-side file browser.
  function requestOpenFile(): void {
    if (!desktop) {
      showBrowser();
      return;
    }
    void chooseNativeFiles();
  }

  async function chooseNativeFiles(): Promise<void> {
    if (!desktop) {
      return;
    }
    try {
      const files = await desktop.openFiles({
        title: $t('app.dialog.openFileTitle'),
        defaultPath: dialogDirectory(activeTab?.path ?? lastPath()),
      });
      for (const file of files) {
        openPath(file);
      }
    } catch (error) {
      shellError = messageOf(error);
    }
  }

  function chooseRecorderTarget(commandName: string): void {
    pendingRecorderCommand = commandName;
    showBrowser();
  }

  function openLocation(initial = ''): void {
    locationInitial = initial;
    locationOpen = true;
  }

  async function applyHighlighters(): Promise<void> {
    try {
      // Read the store directly: `highlightRules` is a reactive statement, so it
      // still holds the previous value right after saveSettingsFromDialog()
      // assigned highlightStore, and the freshly picked colours would be lost.
      await api.setHighlighters(activeRules(highlightStore));
      await api.setQuickHighlighters(quickDefs);
      for (const tab of tabs) {
        tab.pane?.refreshHighlights?.();
      }
    } catch (error) {
      shellError = messageOf(error);
    }
  }

  function openSettings(panel: typeof settingsInitialPanel = 'general'): void {
    settingsInitialPanel = panel;
    settingsOpen = true;
  }

  function toggleTheme(): void {
    const next = nextTheme(currentTheme.id);
    settings = { ...settings, theme: next };
    currentTheme = resolveTheme(next);
    applyTheme(currentTheme);
    saveSettings(settings);
  }

  function changeFontSize(size: number): void {
    const bounded = Math.max(FONT_SIZE_MIN, Math.min(FONT_SIZE_MAX, Math.round(size)));
    if (bounded === settings.fontSize) {
      return;
    }
    settings = { ...settings, fontSize: bounded };
    saveSettings(settings);
  }

  async function saveRecorderBinding(filePath: string, commandName: string): Promise<void> {
    try {
      await api.setRecorderBinding(filePath, commandName);
      const next = { ...recorderBindings };
      if (commandName) {
        next[filePath] = commandName;
      } else {
        delete next[filePath];
      }
      recorderBindings = next;
    } catch (error) {
      shellError = messageOf(error);
    }
  }

  async function saveSettingsFromDialog(detail: {
    settings: WebSettings;
    highlightStore: HighlighterStore;
    quickDefs: QuickHighlighterDef[];
    recorderCommands: RecorderCommand[];
  }): Promise<void> {
    // Recorder commands live on the shell; a failed round trip must not keep the
    // dialog open or throw the rest of the edited settings away.
    try {
      await api.setRecorderCommands(detail.recorderCommands);
      recorderCommands = detail.recorderCommands;
    } catch (error) {
      shellError = messageOf(error);
    }
    settings = detail.settings;
    highlightStore = detail.highlightStore;
    quickDefs = detail.quickDefs;
    applyLocale(settings.language);
    currentTheme = resolveTheme(settings.theme);
    applyTheme(currentTheme);
    recentFiles = loadRecent(settings.recentLimit);
    for (const tab of tabs) {
      tab.pane?.clearMessages?.();
    }
    saveSettings(settings);
    saveHighlighterStore(highlightStore);
    saveQuickDefs(quickDefs);
    settingsOpen = false;
    await applyHighlighters();
  }

  // Clipboard text is log content: it becomes a temporary file on the server.
  // In a browser the Clipboard API is often denied, so Ctrl+V is served by the
  // native paste event instead (it carries the text without a prompt).
  let clipboardSink: HTMLTextAreaElement | null = null;
  let clipboardPasteArmed = false;
  let clipboardFocusBack: HTMLElement | null = null;
  let clipboardPasteTimer: number | null = null;

  function disarmClipboardPaste(restoreFocus = true): void {
    if (clipboardPasteTimer !== null) {
      window.clearTimeout(clipboardPasteTimer);
      clipboardPasteTimer = null;
    }
    const back = clipboardFocusBack;
    clipboardFocusBack = null;
    clipboardPasteArmed = false;
    if (clipboardSink) {
      clipboardSink.value = '';
    }
    if (restoreFocus) {
      if (back && back !== document.body && typeof back.focus === 'function') {
        back.focus();
      } else if (document.activeElement === clipboardSink) {
        // Never leave the hidden sink focused: it would swallow the log view
        // shortcuts as ordinary text editing.
        clipboardSink?.blur();
      }
    }
  }

  async function applyClipboardText(text: string): Promise<void> {
    try {
      const { path } = await api.saveTempText(text);
      openPath(path);
    } catch (error) {
      shellError = $t('app.clipboardFailed', { reason: messageOf(error) });
    }
  }

  async function openClipboard(): Promise<void> {
    try {
      const text = desktop?.readClipboard
        ? await desktop.readClipboard()
        : await navigator.clipboard.readText();
      if (!text || !text.trim()) {
        shellError = $t('app.clipboardEmpty');
        return;
      }
      await applyClipboardText(text);
    } catch (error) {
      shellError = $t('app.clipboardFailed', { reason: messageOf(error) });
    }
  }

  // No preventDefault here: the browser has to perform the actual paste so the
  // text arrives through the paste event.
  function startClipboardPaste(): void {
    if (clipboardPasteArmed) {
      return;
    }
    const active = document.activeElement as HTMLElement | null;
    if (active?.closest?.('.terminal-host')) {
      // xterm's helper textarea already receives the native paste.
      return;
    }
    clipboardFocusBack = active;
    clipboardPasteArmed = true;
    clipboardSink?.focus();
    clipboardSink?.select();
    clipboardPasteTimer = window.setTimeout(() => {
      disarmClipboardPaste();
      void openClipboard();
    }, 400);
  }

  function onGlobalPaste(event: ClipboardEvent): void {
    const target = event.target as HTMLElement | null;
    const targetIsSink = target === clipboardSink;
    const inLogView = !!target?.closest?.('.terminal-host');
    const editable =
      !!target &&
      (target.tagName === 'INPUT' ||
        target.tagName === 'TEXTAREA' ||
        target.tagName === 'SELECT' ||
        target.isContentEditable);
    if (!targetIsSink && !inLogView) {
      // Let regular paste into a text box behave normally; only our armed sink
      // or a paste over the log view turns into an open action.
      if (editable || !clipboardPasteArmed) {
        return;
      }
    }
    const text = event.clipboardData?.getData('text/plain') ?? '';
    event.preventDefault();
    disarmClipboardPaste();
    if (!text.trim()) {
      shellError = $t('app.clipboardEmpty');
      return;
    }
    void applyClipboardText(text);
  }

  function onOpened(tabId: string, info: FileInfo): void {
    setTabMeta(tabId, info.path, displayName(info.path));
    if (activeId === tabId) {
      updateUrl(info.path);
    }
  }

  function openTabMenu(tabId: string, x: number, y: number): void {
    const tab = tabs.find((item) => item.id === tabId);
    if (!tab) {
      return;
    }
    const index = tabs.indexOf(tab);
    const isUrl = /^https?:\/\//i.test(tab.path);
    const closableLeft = tabs.slice(0, index).some((item) => !item.pinned);
    const closableRight = tabs.slice(index + 1).some((item) => !item.pinned);
    const closableOthers = tabs.some((item) => item.id !== tabId && !item.pinned);
    const items: MenuItem[] = [
      { id: 'tab-pin', label: tab.pinned ? $t('app.tab.unpin') : $t('app.tab.pin') },
      { id: 'tab-sep-pin', label: '', separator: true },
      {
        id: 'tab-close',
        label: $t('app.tab.close'),
        shortcut: settings.shortcuts.closeTab?.[0] ?? 'Ctrl+W',
      },
      {
        id: 'tab-close-others',
        label: $t('app.tab.closeOthers'),
        disabled: !closableOthers,
      },
      { id: 'tab-close-left', label: $t('app.tab.closeLeft'), disabled: !closableLeft },
      {
        id: 'tab-close-right',
        label: $t('app.tab.closeRight'),
        disabled: !closableRight,
      },
      { id: 'tab-close-all', label: $t('app.tab.closeAll'), disabled: tabs.length === 0 },
      { id: 'tab-sep-1', label: '', separator: true },
      { id: 'tab-open-window', label: $t('app.tab.openWindow') },
      { id: 'tab-sep-2', label: '', separator: true },
      { id: 'tab-copy-path', label: $t('app.tab.copyPath') },
      { id: 'tab-open-folder', label: $t('app.tab.openFolder'), disabled: isUrl },
      { id: 'tab-sep-3', label: '', separator: true },
      { id: 'tab-rename', label: $t('app.tab.rename') },
      {
        id: 'tab-reset-name',
        label: $t('app.tab.resetName'),
        disabled: !tabNames[tab.path],
      },
    ];
    const width = 240;
    const height = items.length * 28 + 12;
    contextMenu = {
      x: Math.max(8, Math.min(x, window.innerWidth - width - 8)),
      y: Math.max(8, Math.min(y, window.innerHeight - height - 8)),
      items,
      tabId,
    };
  }

  function onMenuSelect(id: string): void {
    const menu = contextMenu;
    contextMenu = null;
    if (!menu) {
      return;
    }
    const tab = tabs.find((item) => item.id === menu.tabId);
    switch (id) {
      case 'tab-pin':
        toggleTabPinned(menu.tabId);
        break;
      case 'tab-close':
        void closeTab(menu.tabId);
        break;
      case 'tab-close-others':
        void closeOthers(menu.tabId);
        break;
      case 'tab-close-left':
        closeLeft(menu.tabId);
        break;
      case 'tab-close-right':
        closeRight(menu.tabId);
        break;
      case 'tab-close-all':
        closeAll();
        break;
      case 'tab-open-window':
        if (tab) {
          openInNewWindow(tab.path);
        }
        break;
      case 'tab-copy-path':
        if (tab) {
          void copyText(tab.path);
        }
        break;
      case 'tab-open-folder':
        if (tab) {
          if (desktop) {
            void desktop.revealItem(tab.path);
          } else {
            showBrowser(parentPath(tab.path));
          }
        }
        break;
      case 'tab-rename':
        renameTab(menu.tabId);
        break;
      case 'tab-reset-name':
        resetTabName(menu.tabId);
        break;
      default:
        break;
    }
  }

  function handleEvent(event: ServerEvent): void {
    for (const tab of tabs) {
      tab.pane?.handleEvent?.(event);
    }
  }

  function onGlobalKeyDown(event: KeyboardEvent): void {
    if (event.key === 'Escape') {
      contextMenu = null;
      if (settingsOpen) {
        settingsOpen = false;
      }
      if (locationOpen) {
        locationOpen = false;
      }
      return;
    }
    const target = event.target as HTMLElement | null;
    // xterm keeps keyboard focus in a hidden helper textarea; single-letter
    // log-view shortcuts (M, N, A, ...) must still reach the app there.
    const inLogView = !!target?.closest('.terminal-host');
    const editing =
      !!target &&
      !inLogView &&
      (target.tagName === 'INPUT' ||
        target.tagName === 'TEXTAREA' ||
        target.tagName === 'SELECT' ||
        target.isContentEditable);
    if (editing) {
      return;
    }

    const matches = (action: string): boolean =>
      (settings.shortcuts[action] ?? []).some(
        (combo) => combo.length > 0 && matchesShortcut(event, combo),
      );

    if (matches('nextMatch')) {
      event.preventDefault();
      activePane?.nextMatch?.();
      return;
    }
    if (matches('prevMatch')) {
      event.preventDefault();
      activePane?.prevMatch?.();
      return;
    }
    if (matches('search')) {
      event.preventDefault();
      activePane?.focusSearch?.();
      return;
    }
    if (matches('selectAll')) {
      event.preventDefault();
      activePane?.selectAll?.();
      return;
    }
    if (matches('openFile')) {
      event.preventDefault();
      requestOpenFile();
      return;
    }
    if (matches('openClipboard')) {
      if (desktop?.readClipboard) {
        event.preventDefault();
        void openClipboard();
        return;
      }
      startClipboardPaste();
      return;
    }
    if (matches('jumpToLine')) {
      event.preventDefault();
      activePane?.focusGoLine?.();
      return;
    }
    if (matches('openLocation')) {
      event.preventDefault();
      openLocation();
      return;
    }
    if (matches('goTop')) {
      event.preventDefault();
      activePane?.gotoLine?.(0);
      return;
    }
    if (matches('goBottom')) {
      event.preventDefault();
      activePane?.scrollToBottomPublic?.();
      return;
    }
    if (matches('wrap')) {
      event.preventDefault();
      activePane?.toggleWrap?.();
      return;
    }
    if (matches('selectionUp')) {
      event.preventDefault();
      activePane?.moveCurrentLine?.(-1);
      return;
    }
    if (matches('selectionDown')) {
      event.preventDefault();
      activePane?.moveCurrentLine?.(1);
      return;
    }
    if (matches('closeTab')) {
      event.preventDefault();
      if (activeId) {
        void closeTab(activeId);
      }
      return;
    }
    if (matches('settings')) {
      event.preventDefault();
      openSettings();
      return;
    }
    if (matches('mark')) {
      event.preventDefault();
      activePane?.toggleCurrentMark?.();
      return;
    }
    if (matches('nextMark')) {
      event.preventDefault();
      activePane?.nextMark?.();
      return;
    }
    if (matches('prevMark')) {
      event.preventDefault();
      activePane?.prevMark?.();
      return;
    }
    if (matches('addSearch')) {
      event.preventDefault();
      activePane?.addSelectionToSearch?.();
      return;
    }
    if (matches('excludeSearch')) {
      event.preventDefault();
      activePane?.excludeSelectionFromSearch?.();
      return;
    }
    if (matches('replaceSearch')) {
      event.preventDefault();
      activePane?.replaceSearchWithSelection?.();
      return;
    }
    if (matches('nextColorLabel')) {
      event.preventDefault();
      activePane?.nextColorLabel?.();
      return;
    }
    if (matches('followFile')) {
      event.preventDefault();
      activePane?.toggleFollowShortcut?.();
      return;
    }
    if (matches('clearColorLabels')) {
      event.preventDefault();
      activePane?.clearColorLabels?.();
      return;
    }
    if (matches('copyWithColor')) {
      event.preventDefault();
      activePane?.copySelectionWithColor?.();
      return;
    }
    for (let index = 1; index <= 9; index++) {
      if (matches(`colorLabel${index}`)) {
        event.preventDefault();
        activePane?.applyColorLabel?.(index - 1);
        return;
      }
    }
  }

  function persistSession(): void {
    const saved = {
      tabs: tabs
        .filter((tab) => !!tab.path)
        .map((tab) => {
          const paneState = (tab.pane?.getSession?.() ?? null) as Record<string, any> | null;
          const state = paneState?.opened ? paneState : (tab.restoreState ?? paneState);
          return {
            path: tab.path,
            state: state ?? undefined,
            pinned: tab.pinned,
          };
        }),
      activeIndex: Math.max(0, tabs.findIndex((tab) => tab.id === activeId)),
    };
    saveSession(saved);
  }

  function restoreTabs(): boolean {
    if (!settings.restoreSession) {
      return false;
    }
    const session = loadSession();
    if (!session || session.tabs.length === 0) {
      return false;
    }
    for (const item of session.tabs) {
      openPath(item.path, item.state ?? null, undefined, false, item.pinned === true);
    }
    const target = tabs[Math.min(session.activeIndex, tabs.length - 1)];
    if (target) {
      selectTab(target.id);
    }
    return true;
  }

  onMount(() => {
    return onSystemThemeChange(() => {
      if (settings.theme !== 'system') {
        return;
      }
      currentTheme = resolveTheme('system');
      applyTheme(currentTheme);
    });
  });

  onMount(async () => {
    // Capture before xterm's helper textarea consumes keys such as M/N/F3.
    window.addEventListener('keydown', onGlobalKeyDown, true);
    window.addEventListener('paste', onGlobalPaste, true);
    try {
      clientInfo = await api.connect('web', 'human');
      connection = 'online';
      api
        .supportedEncodings()
        .then((result) => {
          availableEncodings = result.encodings;
        })
        .catch(() => {});
      stream = new EventStream(clientInfo.clientId, authToken);
      stream.onStatus = (status) => {
        connection = status;
      };
      stream.onEvent = handleEvent;
      stream.open();
      void applyHighlighters();

      try {
        const config = await api.recorderConfig();
        recorderCommands = config.commands;
        recorderBindings = Object.fromEntries(
          config.bindings.map((binding) => [binding.file, binding.command]),
        );
      } catch {
        recorderCommands = [];
        recorderBindings = {};
      }

      const requested = new URLSearchParams(window.location.search).get('file');
      if (requested) {
        openPath(requested);
      } else if (!restoreTabs()) {
        const initial = lastPath();
        if (initial) {
          openPath(initial);
        }
      }

      sessionTimer = window.setInterval(persistSession, 2000);
      window.addEventListener('beforeunload', persistSession);
    } catch (error) {
      connection = 'closed';
      shellError = messageOf(error);
    }
  });

  onDestroy(() => {
    persistSession();
    if (sessionTimer !== null) {
      window.clearInterval(sessionTimer);
    }
    window.removeEventListener('beforeunload', persistSession);
    window.removeEventListener('keydown', onGlobalKeyDown, true);
    window.removeEventListener('paste', onGlobalPaste, true);
    disarmClipboardPaste(false);
    stream?.close();
    delete (window as any).__klogg;
  });

  (window as any).__klogg = {
    get state() {
      const active = activePane?.getState?.() ?? {};
      return {
        ...active,
        clientId: api.clientId,
        connection,
        activeTabId: activeId,
        tabs: tabs.map((tab) => ({ id: tab.id, path: tab.path, title: tab.title })),
        browserOpen,
        shellError,
      };
    },
    get tabs() {
      return tabs.map((tab) => ({
        id: tab.id,
        path: tab.path,
        title: tab.title,
        restoreState: tab.restoreState,
      }));
    },
    openFile: openPath,
    openPath,
    showBrowser,
    openLocation,
    openClipboard,
    openSettings,
    closeTab,
    selectTab,
    persistSession,
    applyHighlighters,
    get activePane() {
      return activePane;
    },
    api,
  };
</script>

<div class="app">
  <!-- Receives the native paste when Ctrl+V is pressed outside an editable
       element, so the text can be turned into a temporary log file. -->
  <textarea
    class="clipboard-sink"
    bind:this={clipboardSink}
    tabindex="-1"
    aria-hidden="true"
    spellcheck="false"
  ></textarea>
  {#if tabs.length}
    <TabBar
      tabs={tabs.map((tab) => ({
        id: tab.id,
        title: tab.title,
        path: tab.path,
        pinned: tab.pinned,
      }))}
      {activeId}
      {connection}
      nextDark={nextThemeDef.dark}
      themeHint={$t('tab.themeSwitch', { name: $t(nextThemeDef.labelKey) })}
      on:select={(event) => selectTab(event.detail)}
      on:close={(event) => void closeTab(event.detail)}
      on:newTab={() => showBrowser()}
      on:settings={() => openSettings()}
      on:toggleTheme={() => toggleTheme()}
      on:contextMenu={(event) => openTabMenu(event.detail.tabId, event.detail.x, event.detail.y)}
      on:reorder={(event) => reorderTabs(event.detail)}
    />
  {/if}

  {#if shellError}
    <div class="error-banner">
      <span>{shellError}</span>
      <button class="ghost" on:click={() => (shellError = '')}>×</button>
    </div>
  {/if}

  {#if tabs.length}
    {#each tabs as tab (tab.id)}
      <div class="pane-slot" class:active={tab.id === activeId}>
        <CrawlerPane
          bind:this={tab.pane}
          {api}
          path={tab.path}
          restoreState={tab.restoreState ?? null}
          defaultWrap={settings.autoWrap}
          defaultFollow={settings.autoFollow}
          theme={paneTheme}
          shortcuts={settings.shortcuts}
          favorite={isFavorite(tab.path, favorites)}
          fontSize={settings.fontSize}
          fontFamily={settings.fontFamily}
          boldFont={settings.boldFont}
          showMainLineNumbers={settings.showMainLineNumbers}
          showFilteredLineNumbers={settings.showFilteredLineNumbers}
          defaultSearchRegex={settings.defaultSearchRegex}
          defaultSearchCaseSensitive={settings.defaultSearchCaseSensitive}
          defaultEncoding={settings.defaultEncoding}
          filePollIntervalMs={settings.filePollIntervalMs}
          searchHistoryLimit={settings.searchHistoryLimit}
          autoRunSearch={settings.autoRunSearch}
          defaultLiveSearch={settings.liveSearch}
          recorderCommands={recorderCommands}
          recorderBindings={recorderBindings}
          {highlightRules}
          initialRecorderCommand={tab.recorderCommand ?? ''}
          on:bindRecorder={(event) => void saveRecorderBinding(event.detail.path, event.detail.name)}
          on:manageRecorder={() => openSettings('recorder')}
          {quickDefs}
          on:opened={(event) => onOpened(tab.id, event.detail)}
          on:requestOpen={() => showBrowser(tab.path)}
          on:requestClose={() => void closeTab(tab.id)}
          on:favoriteToggle={(event) => toggleCurrentFavorite(event.detail)}
          on:fontSizeChange={(event) => changeFontSize(event.detail)}
          on:error={(event) => {
            if (tabs.length === 1) {
              shellError = event.detail;
            }
          }}
        />
      </div>
    {/each}
  {/if}

  {#if tabs.length === 0}
    <div class="empty-main welcome">
      <div class="empty-logo">klogg</div>
      <h2>{$t('app.welcome.title')}</h2>

      <div class="source-tabs">
        <button class:active={openSource === 'all'} on:click={() => (openSource = 'all')}>
          {$t('app.welcome.tab.all')}
        </button>
        <button class:active={openSource === 'latest'} on:click={() => (openSource = 'latest')}>
          {$t('app.welcome.tab.latest')}
        </button>
        <button
          class:active={openSource === 'favorites'}
          on:click={() => (openSource = 'favorites')}
        >
          {$t('app.welcome.tab.favorites')}
        </button>
        <button
          class:active={openSource === 'directory'}
          on:click={() => (openSource = 'directory')}
        >
          {$t('app.welcome.tab.directory')}
        </button>
        <button class:active={openSource === 'url'} on:click={() => (openSource = 'url')}>
          {$t('app.welcome.tab.url')}
        </button>
        <button
          class:active={openSource === 'recorder'}
          on:click={() => (openSource = 'recorder')}
        >
          {$t('app.welcome.tab.recorder')}
        </button>
      </div>

      {#if openSource === 'all'}
        <div class="welcome-actions">
          <button class="primary" on:click={() => requestOpenFile()}>{$t(desktop ? 'app.welcome.openFileLocal' : 'app.welcome.browse')}</button>
          <button class="ghost" on:click={() => void openClipboard()}>{$t('app.welcome.clipboard')}</button>
          <button class="ghost" on:click={() => openSettings()}>{$t('app.welcome.settings')}</button>
          <ConnectionBadge {connection} />
        </div>
        {#if favorites.length}
          <div class="empty-recent">
            <div class="empty-recent-title">{$t('favorites.title')}</div>
            {#each favorites as favorite (favorite)}
              <div class="favorite-row">
                <button class="recent-item favorite-item" on:click={() => openPath(favorite)} title={favorite}>
                  <span class="favorite-star">★</span>
                  <span class="favorite-labels">
                    <span class="favorite-name">{basename(favorite)}</span>
                    <span class="favorite-path">{favorite}</span>
                  </span>
                </button>
                <button
                  class="favorite-remove"
                  title={$t('favorites.remove')}
                  on:click={() => removeFavoritePath(favorite)}
                >×</button>
              </div>
            {/each}
          </div>
        {/if}
        {#if recentFiles.length}
          <div class="empty-recent">
            <div class="empty-recent-title">{$t('app.welcome.recent')}</div>
            {#each recentFiles as recent}
              <button class="recent-item" on:click={() => openPath(recent)} title={recent}>
                {recent}
              </button>
            {/each}
          </div>
        {/if}
      {:else if openSource === 'latest'}
        <div class="welcome-panel">
          {#if recentFiles.length}
            <div class="empty-recent-title">{$t('app.welcome.recent')}</div>
            {#each recentFiles as recent}
              <button class="recent-item" on:click={() => openPath(recent)} title={recent}>
                {recent}
              </button>
            {/each}
          {:else}
            <p class="settings-hint">{$t('app.welcome.noRecent')}</p>
          {/if}
        </div>
      {:else if openSource === 'favorites'}
        <div class="welcome-panel">
          {#if favorites.length}
            {#each favorites as favorite (favorite)}
              <div class="favorite-row">
                <button class="recent-item favorite-item" on:click={() => openPath(favorite)} title={favorite}>
                  <span class="favorite-star">★</span>
                  <span class="favorite-labels">
                    <span class="favorite-name">{basename(favorite)}</span>
                    <span class="favorite-path">{favorite}</span>
                  </span>
                </button>
                <button
                  class="favorite-remove"
                  title={$t('favorites.remove')}
                  on:click={() => removeFavoritePath(favorite)}
                >×</button>
              </div>
            {/each}
          {:else}
            <p class="settings-hint">{$t('app.welcome.noFavorites')}</p>
          {/if}
        </div>
      {:else if openSource === 'directory'}
        <div class="welcome-panel">
          <p class="settings-hint">{$t('app.welcome.directoryHint')}</p>
          <button class="primary" on:click={() => requestOpenFile()}>{$t(desktop ? 'app.welcome.openFileLocal' : 'app.welcome.browse')}</button>
        </div>
      {:else if openSource === 'url'}
        <div class="welcome-panel">
          <p class="settings-hint">{$t('app.welcome.urlHint')}</p>
          <button class="primary" on:click={() => openLocation()}>
            {$t('app.welcome.openLocation')}
          </button>
        </div>
      {:else}
        <div class="welcome-panel">
          <p class="settings-hint">{$t('app.welcome.recorderHint')}</p>
          {#if recorderCommands.length}
            {#each recorderCommands as command (command.name)}
              <div class="recorder-source-row">
                <div class="recorder-source-info">
                  <strong>{command.name}</strong>
                  <code>{command.command}</code>
                </div>
                <button
                  class="primary compact"
                  on:click={() => chooseRecorderTarget(command.name)}
                >{$t('app.welcome.chooseRecorderTarget')}</button>
              </div>
            {/each}
          {:else}
            <p class="settings-hint">{$t('app.welcome.noRecorderCommands')}</p>
          {/if}
          <button class="ghost" on:click={() => openSettings('recorder')}>
            {$t('app.welcome.manageRecorder')}
          </button>
        </div>
      {/if}
    </div>
  {/if}
</div>

{#if contextMenu}
  <ContextMenu
    x={contextMenu.x}
    y={contextMenu.y}
    items={contextMenu.items}
    on:select={(event) => onMenuSelect(event.detail)}
    on:close={() => (contextMenu = null)}
  />
{/if}

{#if settingsOpen}
  <SettingsDialog
    {settings}
    sets={highlightStore.sets}
    activeSetId={highlightStore.activeSetId}
    {quickDefs}
    {recorderCommands}
    initialPanel={settingsInitialPanel}
    {clientInfo}
    {connection}
    {availableEncodings}
    on:close={() => (settingsOpen = false)}
    on:save={(event) => void saveSettingsFromDialog(event.detail)}
  />
{/if}

{#if locationOpen}
  <OpenLocationDialog
    initial={locationInitial}
    on:close={() => (locationOpen = false)}
    on:open={(event) => {
      locationOpen = false;
      openPath(event.detail);
    }}
  />
{/if}

{#if browserOpen}
  <FileBrowser
    {api}
    {favorites}
    {recentFiles}
    {recorderCommands}
    recorderTargetCommand={pendingRecorderCommand ?? ''}
    startPath={browserStartPath}
    on:close={() => {
      browserOpen = false;
      pendingRecorderCommand = null;
    }}
    on:open={(event) => {
      browserOpen = false;
      const commandName = pendingRecorderCommand;
      pendingRecorderCommand = null;
      openPath(event.detail, null, commandName ?? undefined);
    }}
    on:openWithRecorder={(event) => {
      browserOpen = false;
      pendingRecorderCommand = null;
      openPath(event.detail.path, null, event.detail.command);
    }}
    on:manageRecorder={() => {
      browserOpen = false;
      pendingRecorderCommand = null;
      openSettings('recorder');
    }}
  />
{/if}
