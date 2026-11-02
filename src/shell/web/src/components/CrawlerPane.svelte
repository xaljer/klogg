<script lang="ts">
  import { createEventDispatcher, onDestroy, onMount } from 'svelte';

  import type { ApiClient } from '../lib/api';
  import { ApiError } from '../lib/api';
  import type {
    ArchiveEntry,
    FileInfo,
    FileReindexedPayload,
    HighlightRule,
    IndexedPayload,
    IndexingProgressPayload,
    LineData,
    MarksChangedPayload,
    QuickHighlighterDef,
    SearchChip,
    SearchCompletedPayload,
    SearchProgressPayload,
    ServerEvent,
  } from '../lib/types';
  import { t } from '../lib/i18n';
  import { toHtmlColor } from '../lib/highlighters';
  import {
    applyColorLabel as applyColorLabelToSlots,
    applyNextColorLabel,
    clearColorLabel,
    colorLabelKey,
    colorLabelOf,
  } from '../lib/color-labels';
  import {
    clearSearchHistory,
    loadSearchHistory,
    rememberSearch,
    removeSearchHistory,
  } from '../lib/search-history';
  import { chipsToExpression, chipsToText, parseChipText } from '../lib/search-chips';
  import { escapeRegExp } from '../lib/regex-escape';
  import {
    selectionCopyLinesText,
    selectionEndLine,
    selectionHasSelection,
    selectionIsMultiRow,
    selectionLines,
    selectionPortionText,
    selectionRowCount,
    selectionStartLine,
    selectionText as selectionPayloadText,
    type SelectionPayload,
  } from '../lib/selection/payload';
  import {
    createSearchSnapshot,
    snapshotHighlight,
    snapshotSearchParams,
    type SearchSnapshot,
  } from '../lib/search-snapshot';
  import { saveBaseName, saveLinesToFile } from '../lib/save-file';
  import { addMarksChunked, copyLinesWithColorChunked, removeMarksChunked } from '../lib/bulk';
  import { writeRichText } from '../lib/clipboard';
  import type { RecorderCommand, ShortcutMap } from '../lib/settings';
  import type { ThemeDefinition } from '../lib/theme';
  import {
    computeSelectionTimeDelta,
    type SelectionLines,
    type SelectionTimeDelta,
  } from '../lib/time-delta';

  import ContextMenu from './ContextMenu.svelte';
  import FilteredView from './FilteredView.svelte';
  import LogViewport from './LogViewport.svelte';
  import Overview from './Overview.svelte';
  import SearchBar from './SearchBar.svelte';
  import SelectionTimeBadge from './SelectionTimeBadge.svelte';
  import Toolbar from './Toolbar.svelte';

  type SelectionSource = 'main' | 'filtered';

  export let api: ApiClient;
  export let path = '';
  export let restoreState: Record<string, any> | null = null;
  export let defaultWrap = false;
  export let defaultFollow = false;
  export let theme: ThemeDefinition;
  export let shortcuts: ShortcutMap = {};
  export let quickDefs: QuickHighlighterDef[] = [];
  export let favorite = false;
  export let fontSize = 13;
  export let fontFamily = '"Cascadia Mono", Consolas, monospace';
  export let boldFont = false;
  export let showMainLineNumbers = true;
  export let showFilteredLineNumbers = true;
  export let recorderCommands: RecorderCommand[] = [];
  export let recorderBindings: Record<string, string> = {};
  export let initialRecorderCommand = '';
  export let defaultSearchRegex = true;
  export let defaultSearchCaseSensitive = false;
  export let defaultEncoding = '';
  export let filePollIntervalMs = 2000;
  export let searchHistoryLimit = 20;
  export let autoRunSearch = true;
  export let defaultLiveSearch = true;
  export let highlightRules: HighlightRule[] = [];

  const dispatch = createEventDispatcher<{
    opened: FileInfo;
    requestOpen: void;
    requestClose: void;
    favoriteToggle: string;
    fontSizeChange: number;
    bindRecorder: { path: string; name: string };
    manageRecorder: void;
    error: string;
  }>();

  const SPLIT_KEY = 'klogg.web.split';
  const SPLIT_HANDLE_PX = 5;
  const DEFAULT_SPLIT_PERCENT = 62;

  interface MenuItem {
    id: string;
    label: string;
    shortcut?: string;
    disabled?: boolean;
    separator?: boolean;
    checked?: boolean;
    swatch?: string;
    children?: MenuItem[];
    grid?: {
      id: string;
      label: string;
      short?: string;
      swatch?: string;
      color?: string;
      checked?: boolean;
      disabled?: boolean;
    }[];
  }

  interface LogContextRequest extends SelectionPayload {
    x: number;
    y: number;
    source: 'main' | 'filtered';
    /** Filtered view: size of the result set, for "save all results". */
    total?: number;
  }

  interface FileContext {
    x: number;
    y: number;
    path: string;
  }

  let fileInfo: FileInfo | null = null;
  // Set when an URL download is a multi-file archive: the pane shows the
  // extracted entries until the user picks one.
  let archiveFiles: ArchiveEntry[] | null = null;
  let totalLines = 0;
  let firstLine = 0;
  let currentLine: number | null = null;
  let rows = 30;
  let follow = false;
  let wrap = false;
  let busy = false;
  let errorMessage = '';
  let statusMessage = '';
  let encodings: string[] = [];
  let indexingProgress: number | null = null;
  let openingFileId: number | null = null;
  let stopRequested = false;

  let searchPattern = '';
  let searchRegex = defaultSearchRegex;
  let searchCase = defaultSearchCaseSensitive;
  let searchInvert = false;
  let searchFromLine: number | null = null;
  let searchToLine: number | null = null;
  let searchMaxResults: number | null = null;
  let searchHistory = loadSearchHistory(searchHistoryLimit);
  let chipMode = true;
  let liveSearch = defaultLiveSearch;
  let liveSearchTimer: number | null = null;
  // A live refresh keeps the previous result set visible until the new search
  // completes, so the filtered pane never blanks while the file is growing.
  let liveSearchPending = false;
  let liveRefreshInFlight = false;
  let preserveOnRefresh = false;
  let viewSearchId: number | null = null;
  let chips: SearchChip[] = [];
  let chipSeq = 1;
  let lastColorLabel = -1;
  let activePattern = '';
  let activeSearchRegex = true;
  let activeSearchCase = false;
  let visibility = 'marks-matches';
  let searchId: number | null = null;
  let searchedToLine = 0;
  let searchRunning = false;
  let searchProgress = 0;
  let searchTotal = 0;
  // Match lines are fetched page-wise from the session; keeping every match
  // in a client array would cap navigation at an arbitrary size.
  const MATCH_PAGE_SIZE = 128;
  let matchCache = new Map<number, number[]>();
  let currentMatch = -1;
  // Bumped when the filtered projection's content or identity changes
  // (search, visibility, marks, reload): the filtered view reloads its window.
  let projectionRevision = 0;

  let marks = new Set<number>();
  let quickWords: string[][] = Array.from({ length: 10 }, () => []);
  // Bumped when the main frame or the overview density changes (marks, flags,
  // colours): the viewport re-fetches its frame and the overview redraws.
  let frameRevision = 0;
  let selectionText = '';
  let selectionRange = '';
  let selectionTime: { source: SelectionSource; delta: SelectionTimeDelta } | null = null;
  let highlightedWord = '';
  let searchPollTimer: number | null = null;
  let statTimer: number | null = null;
  let recorderPollTimer: number | null = null;
  let recorderProcessId: number | null = null;
  let recorderRunning = false;
  let recorderBusy = false;
  let closed = false;

  let workspaceEl: HTMLDivElement;
  let splitHandleEl: HTMLDivElement;
  let viewportRef: any = null;
  let filteredRef: any = null;

  function loadSplitPercent(): number {
    try {
      const raw = localStorage.getItem(SPLIT_KEY);
      const parsed = raw === null ? Number.NaN : Number(raw);
      return Number.isFinite(parsed) ? clamp(parsed, 0, 100) : DEFAULT_SPLIT_PERCENT;
    } catch {
      return DEFAULT_SPLIT_PERCENT;
    }
  }

  // The percentage covers the pane area only; the handle is excluded so 0
  // and 100 can truly collapse either pane without overflowing the workspace.
  function splitBasis(percent: number): string {
    return `calc(${percent}% - ${(percent * SPLIT_HANDLE_PX) / 100}px)`;
  }

  let mainPercent = loadSplitPercent();
  let draggingSplit = false;
  let splitPointerId: number | null = null;
  let contextMenu:
    | {
        x: number;
        y: number;
        items: MenuItem[];
        payload: LogContextRequest | FileContext | null;
        kind: 'log' | 'file' | 'recorder';
      }
    | null = null;

  $: fileId = fileInfo?.fileId ?? null;
  let lastPollInterval = filePollIntervalMs;
  $: if (lastPollInterval !== filePollIntervalMs) {
    lastPollInterval = filePollIntervalMs;
    if (fileInfo) {
      startStatPolling();
    }
  }
  $: if (searchHistory.length > searchHistoryLimit) {
    searchHistory = searchHistory.slice(0, searchHistoryLimit);
  }
  $: boundRecorder = fileInfo ? (recorderBindings[fileInfo.path] ?? '') : '';
  $: activeRecorderCommand = recorderCommands.find((item) => item.name === boundRecorder) ?? null;
  $: if (quickDefs.length > 0 && quickWords.length !== quickDefs.length) {
    quickWords = quickDefs.map((_, index) => quickWords[index] ?? []);
  }
  // The desktop scroll bar range is totalLines - visibleLines + 1: the last line
  // can be scrolled one row up, leaving a blank row at the bottom.
  $: maxFirstLine = Math.max(0, totalLines - rows + 1);
  $: if (firstLine > maxFirstLine) {
    firstLine = maxFirstLine;
  }

  function messageOf(error: unknown): string {
    return error instanceof ApiError ? `${error.code}: ${error.message}` : String(error);
  }

  function clamp(value: number, min: number, max: number): number {
    return Math.min(Math.max(value, min), max);
  }

  function isHttpUrl(value: string): boolean {
    return /^https?:\/\//i.test(value.trim());
  }

  function scrollToBottom() {
    firstLine = maxFirstLine;
    currentLine = totalLines > 0 ? totalLines - 1 : null;
  }

  function jumpToLine(line: number) {
    const target = clamp(line - Math.floor(rows * 0.35), 0, Math.max(0, totalLines - 1));
    follow = false;
    currentLine = line;
    firstLine = target;
  }

  function visibilityFlags() {
    return {
      matches: visibility.includes('matches'),
      marks: visibility.includes('marks'),
      events: visibility.includes('events'),
    };
  }

  async function applyVisibility(id: number | null): Promise<void> {
    if (id === null) {
      projectionRevision += 1;
      return;
    }
    const flags = visibilityFlags();
    try {
      await api.call('search.setVisibility', {
        searchId: id,
        matches: flags.matches,
        marks: flags.marks,
        events: flags.events,
      });
      projectionRevision += 1;
    } catch (error) {
      errorMessage = messageOf(error);
    }
  }

  async function refreshInfo(): Promise<void> {
    if (!fileInfo) {
      return;
    }
    try {
      const info = await api.fileInfo(fileInfo.fileId);
      const displayPath = isHttpUrl(fileInfo.path) ? fileInfo.path : info.path;
      fileInfo = { ...info, path: displayPath };
      totalLines = info.totalLines;
      frameRevision += 1;
      projectionRevision += 1;
    } catch (error) {
      errorMessage = messageOf(error);
    }
  }

  export async function refreshMarks(): Promise<void> {
    if (!fileInfo) {
      marks = new Set();
      return;
    }
    try {
      const result = await api.listMarks(fileInfo.fileId);
      marks = new Set(result.marks);
      frameRevision += 1;
      // The marks/matches union in the filtered view depends on this set.
      projectionRevision += 1;
    } catch (error) {
      errorMessage = messageOf(error);
    }
  }

  function stopStatPolling(): void {
    if (statTimer !== null) {
      window.clearInterval(statTimer);
      statTimer = null;
    }
  }

  async function checkFileStat(): Promise<void> {
    if (!fileInfo || busy || fileInfo.path.includes('://')) {
      return;
    }
    try {
      const stat = await api.fileStat(fileInfo.fileId);
      if (stat.sizeBytes !== fileInfo.sizeBytes) {
        // Cheap probe only: the engine decides incremental vs full reindex and
        // reports via file.reindexed. Never force a full reload from polling.
        await api.checkFileChanges(fileInfo.fileId);
        fileInfo = { ...fileInfo, sizeBytes: stat.sizeBytes };
      }
    } catch {
      // A transient stat failure must not spam the UI; the next tick retries.
    }
  }

  function startStatPolling(): void {
    stopStatPolling();
    if (!fileInfo || fileInfo.path.includes('://')) {
      return;
    }
    // The engine watcher normally drives file.changed; this cheap stat poll is
    // the fallback for hosts where the native watcher is unavailable.
    statTimer = window.setInterval(
      () => void checkFileStat(),
      Math.max(500, Math.min(10000, filePollIntervalMs)),
    );
  }

  function stopRecorderPolling(): void {
    if (recorderPollTimer !== null) {
      window.clearInterval(recorderPollTimer);
      recorderPollTimer = null;
    }
  }

  function startRecorderPolling(processId: number): void {
    stopRecorderPolling();
    recorderPollTimer = window.setInterval(async () => {
      if (recorderProcessId !== processId) {
        stopRecorderPolling();
        return;
      }
      try {
        const status = await api.recorderStatus(processId);
        if (status.state === 'running') {
          return;
        }
        stopRecorderPolling();
        recorderRunning = false;
        recorderBusy = false;
        recorderProcessId = null;
        if (status.state === 'error') {
          errorMessage = $t('crawler.recorderFailed', {
            reason: status.error || `exit ${status.exitCode}`,
          });
          statusMessage = '';
        }
        else if (status.state === 'stopped') {
          statusMessage = $t('crawler.recorderStopped');
        }
        else {
          statusMessage = $t('crawler.recorderFinished');
        }
      } catch (error) {
        stopRecorderPolling();
        recorderRunning = false;
        recorderBusy = false;
        recorderProcessId = null;
        errorMessage = $t('crawler.recorderFailed', { reason: messageOf(error) });
        statusMessage = '';
      }
    }, 1000);
  }

  async function toggleRecorder(): Promise<void> {
    if (!fileInfo || recorderBusy) {
      return;
    }
    if (recorderProcessId !== null) {
      recorderBusy = true;
      statusMessage = $t('crawler.recorderStopping');
      try {
        await api.stopRecorder(recorderProcessId);
      } catch (error) {
        errorMessage = $t('crawler.recorderFailed', { reason: messageOf(error) });
        recorderBusy = false;
        statusMessage = '';
      }
      return;
    }

    const command = activeRecorderCommand;
    if (!command) {
      openRecorderMenu(window.innerWidth / 2, window.innerHeight / 2);
      return;
    }
    if (fileInfo.path.includes('://')) {
      return;
    }

    await startRecorderCommand(command);
  }

  async function startRecorderCommand(command: RecorderCommand): Promise<void> {
    if (!fileInfo || recorderBusy || fileInfo.path.includes('://')) {
      return;
    }
    recorderBusy = true;
    try {
      const started = await api.startRecorder(command.name, fileInfo.path);
      recorderProcessId = started.processId;
      recorderRunning = true;
      statusMessage = $t('crawler.recorderStarted', { name: command.name });
      startRecorderPolling(started.processId);
    } catch (error) {
      errorMessage = $t('crawler.recorderFailed', { reason: messageOf(error) });
      statusMessage = '';
    } finally {
      recorderBusy = false;
    }
  }

  export async function startRecorderByName(name: string): Promise<void> {
    const command = recorderCommands.find((item) => item.name === name);
    if (!command || !fileInfo || recorderRunning || recorderBusy) {
      return;
    }
    dispatch('bindRecorder', { path: fileInfo.path, name });
    await startRecorderCommand(command);
  }

  function changeRecorderBinding(name: string): void {
    if (fileInfo) {
      dispatch('bindRecorder', { path: fileInfo.path, name });
    }
  }

  function openRecorderMenu(x: number, y: number): void {
    const items: MenuItem[] = recorderCommands.map((command, index) => ({
      id: `recorder-command-${index}`,
      label: command.name,
      checked: command.name === boundRecorder,
    }));
    if (items.length > 0) {
      items.push({ id: 'recorder-sep', label: '', separator: true });
    }
    items.push({ id: 'recorder-new', label: $t('crawler.menu.newRecorderCommand') });
    if (boundRecorder) {
      items.push({ id: 'recorder-clear', label: $t('crawler.menu.clearRecorderBinding') });
    }
    const position = menuPosition(x, y, items.length);
    contextMenu = { ...position, items, payload: null, kind: 'recorder' };
  }

  function handleRecorderAction(detail: { x: number; y: number; menu: boolean }): void {
    if (!fileInfo || fileInfo.path.includes('://') || recorderBusy) {
      return;
    }
    if (recorderRunning) {
      if (detail.menu) {
        return;
      }
      void toggleRecorder();
      return;
    }
    if (detail.menu || !boundRecorder || !activeRecorderCommand) {
      openRecorderMenu(detail.x, detail.y);
      return;
    }
    void toggleRecorder();
  }

  function stopRecorderForClose(): void {
    stopRecorderPolling();
    if (recorderProcessId !== null) {
      void api.stopRecorder(recorderProcessId).catch(() => {});
      recorderProcessId = null;
    }
    recorderRunning = false;
    recorderBusy = false;
  }

  async function closeCurrentFile(): Promise<void> {
    stopStatPolling();
    stopRecorderForClose();
    if (!fileInfo) {
      return;
    }
    const id = fileInfo.fileId;
    const oldSearch = searchId;
    fileInfo = null;
    totalLines = 0;
    currentLine = null;
    searchId = null;
    searchedToLine = 0;
    viewSearchId = null;
    preserveOnRefresh = false;
    searchRunning = false;
    stopSearchPolling();
    matchCache.clear();
    searchTotal = 0;
    currentMatch = -1;
    marks = new Set();
    projectionRevision += 1;
    frameRevision += 1;
    selectionText = '';
    selectionRange = '';
    selectionTime = null;
    if (oldSearch !== null) {
      api.cancelSearch(oldSearch).catch(() => {});
    }
    try {
      await api.closeFile(id);
    } catch {
      // closing a dropped file is harmless
    }
  }

  export async function close(): Promise<void> {
    if (closed) {
      return;
    }
    closed = true;
    stopSearchPolling();
    stopStatPolling();
    await closeCurrentFile();
  }

  export function focusViewport(): void {
    viewportRef?.focus?.();
  }

  export function toggleCurrentMark(): void {
    // The desktop `Add line mark` marks every selected line (or the current line
    // when nothing is selected), in whichever view has the focus.
    if (filteredRef?.hasFocus()) {
      void (async () => {
        const lines = (await filteredRef?.selectionTargetLines?.()) ?? [];
        await toggleMarkLines(lines.length > 0 ? lines : [currentLine ?? firstLine]);
      })();
      return;
    }
    const lines = viewportRef?.copyTargetLines?.() ?? [];
    void toggleMarkLines(lines.length > 0 ? lines : [currentLine ?? firstLine]);
  }

  export function addSelectionToSearch(): void {
    if (selectionText) {
      void addTextToSearch(selectionText);
    }
  }

  export function excludeSelectionFromSearch(): void {
    if (selectionText) {
      void excludeTextFromSearch(selectionText);
    }
  }

  export function replaceSearchWithSelection(): void {
    if (selectionText) {
      void searchFromText(selectionText, false);
    }
  }

  /**
   * Text the colour-label shortcuts act on. The desktop UI labels the selection
   * of the focused view, so a row picked in the search view must work there and
   * not fall back to whatever the main view happened to select earlier.
   */
  function colorLabelTargetText(): string {
    if (filteredRef?.hasFocus()) {
      return filteredRef?.selectionTextForLabel?.() ?? '';
    }
    return selectionText;
  }

  export function applyColorLabel(index: number): void {
    const text = colorLabelTargetText();
    if (text.trim()) {
      lastColorLabel = index;
      applyQuickLabel(index, text);
    }
  }

  export function nextColorLabel(): void {
    const text = colorLabelTargetText();
    if (!text.trim() || quickDefs.length === 0) {
      return;
    }
    applyNextQuickLabel(text);
  }

  export function clearColorLabels(): void {
    clearQuickLabels();
  }

  export function toggleFollowShortcut(): void {
    toggleFollow(!follow);
  }

  export function getState(): Record<string, unknown> {
    return {
      path: fileInfo?.path ?? path,
      fileId,
      totalLines,
      firstLine,
      currentLine,
      rows,
      follow,
      wrap,
      sizeBytes: fileInfo?.sizeBytes ?? 0,
      encoding: fileInfo?.encoding.name ?? '',
      visibility,
      searchId,
      searchRunning,
      searchProgress,
      searchTotal,
      currentMatch,
      chipMode,
      chips,
      liveSearch,
      marks: [...marks],
      errorMessage,
      statusMessage,
      selectionText,
      selectionRange,
      mainPercent,
      busy,
    };
  }

  async function applyRestoreState(state: Record<string, any>): Promise<void> {
    const has = (key: string) => Object.prototype.hasOwnProperty.call(state, key);
    if (has('mainPercent')) {
      const value = Number(state.mainPercent);
      if (Number.isFinite(value)) {
        mainPercent = clamp(value, 0, 100);
      }
    }
    if (has('wrap')) {
      wrap = !!state.wrap;
    }
    if (has('follow')) {
      follow = !!state.follow;
    }
    if (has('firstLine')) {
      firstLine = clamp(Number(state.firstLine) || 0, 0, Math.max(0, totalLines - 1));
    }
    if (has('currentLine') && state.currentLine !== null) {
      const line = Number(state.currentLine);
      currentLine = Number.isFinite(line) ? clamp(line, 0, Math.max(0, totalLines - 1)) : null;
    }
    if (typeof state.visibility === 'string') {
      visibility = state.visibility;
    }
    if (typeof state.searchPattern === 'string') {
      searchPattern = state.searchPattern;
    }
    if (has('searchRegex')) {
      searchRegex = !!state.searchRegex;
    }
    if (has('searchCase')) {
      searchCase = !!state.searchCase;
    }
    if (has('searchInvert')) {
      searchInvert = !!state.searchInvert;
    }
    if (has('searchFromLine') && state.searchFromLine !== null) {
      searchFromLine = Number(state.searchFromLine);
    }
    if (has('searchToLine') && state.searchToLine !== null) {
      searchToLine = Number(state.searchToLine);
    }
    if (has('searchMaxResults') && state.searchMaxResults !== null) {
      searchMaxResults = Number(state.searchMaxResults);
    }
    if (has('chipMode')) {
      chipMode = !!state.chipMode;
    }
    if (has('liveSearch')) {
      liveSearch = !!state.liveSearch;
    }
    if (Array.isArray(state.chips)) {
      chips = state.chips
        .filter((chip: any) => chip && typeof chip === 'object')
        .map((chip: any, index: number) => ({
          id: chipSeq++,
          type: ['or', 'and', 'not', 'notAnd', 'raw'].includes(chip.type) ? chip.type : 'or',
          terms: Array.isArray(chip.terms)
            ? chip.terms.filter((term: unknown) => typeof term === 'string')
            : [],
        }));
    }
    if (Array.isArray(state.quickWords)) {
      quickWords = state.quickWords
        .filter((words: unknown): words is string[] => Array.isArray(words))
        .map((words: string[]) => words.filter((word) => typeof word === 'string'));
    }
    if (Array.isArray(state.marks) && state.marks.length > 0) {
      await addMarksChunked(api, fileInfo!.fileId, state.marks.filter(Number.isFinite)).catch(
        () => {},
      );
      await refreshMarks();
    }
    if ((chipMode && chips.length > 0) || (!chipMode && searchPattern.trim())) {
      await runSearch();
    }
  }

  async function openFile(targetPath: string): Promise<void> {
    const trimmed = targetPath.trim();
    if (!trimmed || busy) {
      return;
    }
    busy = true;
    errorMessage = '';
    statusMessage = $t('crawler.opening');
    openingFileId = null;
    indexingProgress = null;
    stopRequested = false;

    try {
      await loadEncodings();
      await closeCurrentFile();
      const opened = isHttpUrl(trimmed)
        ? await api.openUrl(trimmed)
        : await api.openFile(trimmed, false, defaultEncoding || undefined);
      if ('archive' in opened) {
        if (closed) {
          return;
        }
        archiveFiles = opened.archive.files;
        fileInfo = null;
        statusMessage = '';
        return;
      }
      const info = isHttpUrl(trimmed) ? { ...opened, path: trimmed } : opened;
      archiveFiles = null;
      if (closed) {
        await api.closeFile(opened.fileId).catch(() => {});
        return;
      }
      fileInfo = info;
      totalLines = info.totalLines;
      firstLine = 0;
      currentLine = null;
      follow = defaultFollow;
      wrap = defaultWrap;
      marks = new Set();
      highlightedWord = '';
      searchId = null;
      searchedToLine = 0;
      viewSearchId = null;
      preserveOnRefresh = false;
      searchTotal = 0;
      matchCache.clear();
      currentMatch = -1;
      searchRunning = false;
      searchProgress = 0;
      searchFromLine = null;
      searchToLine = null;
      searchMaxResults = null;
      searchRegex = defaultSearchRegex;
      searchCase = defaultSearchCaseSensitive;
      chipMode = true;
      chips = [];
      chipSeq = 1;
      activePattern = '';
      projectionRevision += 1;
      statusMessage = '';

      api.watchFile(info.fileId, true).catch(() => {});
      await refreshMarks();
      startStatPolling();
      openingFileId = null;
      indexingProgress = null;
      stopRequested = false;
      if (restoreState) {
        await applyRestoreState(restoreState);
      }
      if (follow) {
        scrollToBottom();
      }
      if (initialRecorderCommand) {
        const commandName = initialRecorderCommand;
        initialRecorderCommand = '';
        await startRecorderByName(commandName);
      }
      dispatch('opened', info);
    } catch (error) {
      stopStatPolling();
      openingFileId = null;
      indexingProgress = null;
      stopRequested = false;
      errorMessage = messageOf(error);
      statusMessage = '';
      dispatch('error', errorMessage);
    } finally {
      busy = false;
    }
  }

  async function openArchiveEntry(entryPath: string): Promise<void> {
    archiveFiles = null;
    await openFile(entryPath);
  }

  function formatSize(bytes: number): string {
    if (!bytes) {
      return '';
    }
    if (bytes < 1024) {
      return `${bytes} B`;
    }
    if (bytes < 1024 * 1024) {
      return `${(bytes / 1024).toFixed(1)} KiB`;
    }
    return `${(bytes / 1024 / 1024).toFixed(1)} MiB`;
  }

  async function reloadFile(): Promise<void> {
    if (!fileInfo || busy) {
      return;
    }
    const previousLines = totalLines;
    busy = true;
    statusMessage = $t('crawler.reloading');
    try {
      const info = await api.reloadFile(fileInfo.fileId);
      fileInfo = info;
      totalLines = info.totalLines;
      const grew = info.totalLines > previousLines;
      // With an active search the filtered pane is refreshed by the search
      // completion path; bumping the version here would blank it first.
      if (!(liveSearch && searchId !== null && grew)) {
        preserveOnRefresh = false;
        projectionRevision += 1;
      }
      await refreshMarks();
      if (follow) {
        scrollToBottom();
      }
      if (liveSearch && searchId !== null && grew) {
        scheduleLiveSearch();
      }
      statusMessage = $t('crawler.reloadedLines', { count: info.totalLines.toLocaleString() });
    } catch (error) {
      errorMessage = messageOf(error);
    } finally {
      busy = false;
    }
  }

  async function loadEncodings(): Promise<void> {
    if (encodings.length > 0) {
      return;
    }
    try {
      const result = await api.supportedEncodings();
      encodings = result.encodings;
    } catch {
      // Encoding switching is optional; keep the detected encoding label.
    }
  }

  async function changeEncoding(encoding: string): Promise<void> {
    if (!fileInfo || busy || encoding === fileInfo.encoding.name) {
      return;
    }
    busy = true;
    errorMessage = '';
    statusMessage = $t('crawler.switchingEncoding', { encoding });
    try {
      await api.setEncoding(fileInfo.fileId, encoding);
      const info = await api.fileInfo(fileInfo.fileId);
      fileInfo = isHttpUrl(fileInfo.path) ? { ...info, path: fileInfo.path } : info;
      totalLines = info.totalLines;
      if (currentLine !== null && currentLine >= totalLines) {
        currentLine = totalLines > 0 ? totalLines - 1 : null;
      }
      if (searchId !== null) {
        await api.cancelSearch(searchId).catch(() => {});
        searchId = null;
        searchedToLine = 0;
        viewSearchId = null;
        preserveOnRefresh = false;
        searchTotal = 0;
        matchCache.clear();
        currentMatch = -1;
      }
      await refreshMarks();
      frameRevision += 1;
      projectionRevision += 1;
      statusMessage = $t('crawler.encodingChanged', { encoding: info.encoding.name });
    } catch (error) {
      errorMessage = messageOf(error);
      statusMessage = '';
    } finally {
      busy = false;
    }
  }

  async function stopLoading(): Promise<void> {
    const id = fileInfo?.fileId ?? openingFileId;
    if (id === null) {
      stopRequested = true;
      statusMessage = $t('crawler.stopPending');
      return;
    }
    try {
      await api.interruptLoading(id);
      stopRequested = false;
      indexingProgress = null;
      statusMessage = $t('crawler.stopRequested');
    } catch (error) {
      errorMessage = messageOf(error);
    }
  }

  function toggleFollow(enabled: boolean): void {
    follow = enabled;
    if (follow) {
      scrollToBottom();
      filteredRef?.followToEnd?.();
    }
  }

  function setHighlightedWord(word: string): void {
    highlightedWord = word.trim();
  }

  // Both views keep their own row selection, so the badge follows whichever
  // view produced the latest multi-line selection.
  function applySelectionTime(source: SelectionSource, selection: SelectionLines): void {
    const delta = computeSelectionTimeDelta(selection.lines, selection.lineCount);
    if (delta) {
      selectionTime = { source, delta };
      return;
    }
    if (selectionTime?.source === source) {
      selectionTime = null;
    }
  }

  async function toggleMark(line: number): Promise<void> {
    if (!fileInfo) {
      return;
    }
    const hasMark = marks.has(line);
    const next = new Set(marks);
    if (hasMark) {
      next.delete(line);
    } else {
      next.add(line);
    }
    marks = next;
    frameRevision += 1;
    // The projection carries the mark bit as well: reload so the row flags and
    // the marks/matches union stay in sync.
    projectionRevision += 1;

    try {
      if (hasMark) {
        await api.removeMarks(fileInfo.fileId, [line]);
      } else {
        await api.addMarks(fileInfo.fileId, [line]);
      }
    } catch (error) {
      errorMessage = messageOf(error);
      await refreshMarks();
    }
  }

  export async function nextMark(): Promise<void> {
    if (!fileInfo) {
      return;
    }
    try {
      // Anchor on the cursor, not the viewport top: jumpToLine() parks firstLine
      // above the target, so anchoring there returns the same mark on every press.
      const result = await api.markAfter(fileInfo.fileId, navigationLine());
      if (result.line !== null) {
        jumpToLine(result.line);
        // The desktop mark shortcuts are per view; the web shortcuts are global,
        // so the search pane follows the same mark.
        void filteredRef?.revealSourceLine(result.line);
      } else {
        statusMessage = $t('crawler.noMarkAfter');
      }
    } catch (error) {
      errorMessage = messageOf(error);
    }
  }

  export async function prevMark(): Promise<void> {
    if (!fileInfo) {
      return;
    }
    try {
      const result = await api.markBefore(fileInfo.fileId, navigationLine());
      if (result.line !== null) {
        jumpToLine(result.line);
        void filteredRef?.revealSourceLine(result.line);
      } else {
        statusMessage = $t('crawler.noMarkBefore');
      }
    } catch (error) {
      errorMessage = messageOf(error);
    }
  }

  function navigationLine(): number {
    return currentLine ?? firstLine;
  }

  /**
   * The desktop views take focus when they are clicked, which is what stops the
   * search box from swallowing the single-letter shortcuts after the search it
   * was used for. A click on an editable control keeps its focus. The listener
   * is global so that clicks on the splitter and the overview count as well;
   * hidden panes ignore it.
   */
  function releaseSearchFocus(event: PointerEvent): void {
    if (!workspaceEl || !workspaceEl.closest('.pane-slot')?.classList.contains('active')) {
      return;
    }
    const target = event.target as HTMLElement | null;
    if (!target || target.closest('input, textarea, select, [contenteditable="true"]')) {
      return;
    }
    // The search bar's own controls keep the caret where it was, like the
    // desktop buttons that are created with NoFocus.
    if (target.closest('.searchbar')) {
      return;
    }
    const active = document.activeElement as HTMLElement | null;
    if (
      active &&
      active !== document.body &&
      active.closest('input, textarea, select, [contenteditable="true"]')
    ) {
      active.blur();
    }
    if (target.closest('.filtered-scroll')) {
      filteredRef?.focusList?.();
    }
  }

  function stopSearchPolling(): void {
    if (searchPollTimer !== null) {
      window.clearInterval(searchPollTimer);
      searchPollTimer = null;
    }
  }

  // Match state is index-based: only the page around the current match is
  // fetched, so navigation works for any number of matches.
  function syncMatchNavigation(): void {
    matchCache.clear();
    if (searchTotal <= 0) {
      currentMatch = -1;
    } else if (currentMatch < 0) {
      currentMatch = 0;
    } else if (currentMatch >= searchTotal) {
      currentMatch = searchTotal - 1;
    }
  }

  async function fetchMatchLine(index: number): Promise<number | null> {
    if (searchId === null) {
      return null;
    }
    const page = Math.floor(index / MATCH_PAGE_SIZE);
    let lines = matchCache.get(page);
    if (!lines) {
      const id = searchId;
      try {
        const result = await api.searchMatches(id, page * MATCH_PAGE_SIZE, MATCH_PAGE_SIZE);
        if (searchId !== id) {
          return null;
        }
        lines = result.matches;
        matchCache.set(page, lines);
      } catch (error) {
        errorMessage = messageOf(error);
        return null;
      }
    }
    return lines[index - page * MATCH_PAGE_SIZE] ?? null;
  }

  async function finishSearch(id: number | null): Promise<void> {
    syncMatchNavigation();
    if (id !== null && searchId === id) {
      await applyVisibility(id);
      // Swap the filtered pane to the new result set only once it is ready.
      viewSearchId = id;
      preserveOnRefresh = liveRefreshInFlight;
      liveRefreshInFlight = false;
      projectionRevision += 1;
      const maxResults = executedSearch?.maxResults ?? searchMaxResults;
      if (maxResults !== null) {
        searchTotal = Math.min(searchTotal, maxResults);
      }
      // Match counts belong to the search bar; keep the toolbar free of
      // duplicate search status text.
      statusMessage = '';
      if (liveSearchPending) {
        liveSearchPending = false;
        scheduleLiveSearch();
      }
    }
  }

  function startSearchPolling(id: number): void {
    stopSearchPolling();
    searchPollTimer = window.setInterval(async () => {
      if (searchId !== id) {
        stopSearchPolling();
        return;
      }
      try {
        const result = await api.searchProgress(id);
        searchProgress = result.progress;
        searchTotal = result.totalMatches;
        if (result.progress >= 1) {
          searchRunning = false;
          stopSearchPolling();
          await finishSearch(id);
        }
      } catch {
        stopSearchPolling();
      }
    }, 300);
  }

  function addChipText(text: string, negate: boolean): SearchChip[] {
    const specs = parseChipText(text, negate);
    if (specs.length === 0) {
      return chips;
    }
    chips = [
      ...chips,
      ...specs.map((spec) => ({ id: chipSeq++, type: spec.type, terms: spec.terms })),
    ];
    return chips;
  }

  async function addChipAndSearch(text: string, negate: boolean): Promise<void> {
    addChipText(text, negate);
    if (autoRunSearch) {
      await runSearch();
    }
  }

  async function removeChip(index: number): Promise<void> {
    chips = chips.filter((_, item) => item !== index);
    if (chips.length > 0) {
      await runSearch();
    } else {
      await clearSearchText();
    }
  }

  /**
   * In-place chip editing, like the desktop pattern widget: the chip keeps its
   * type, an AND group is re-split on "&", and clearing the text drops the chip.
   */
  async function editChip(index: number, text: string): Promise<void> {
    const chip = chips[index];
    if (!chip) {
      return;
    }
    const trimmed = text.trim();
    if (!trimmed) {
      await removeChip(index);
      return;
    }
    let terms: string[];
    if (chip.type === 'and' || chip.type === 'notAnd') {
      terms = trimmed
        .split('&')
        .map((part) => part.trim())
        .filter(Boolean);
      if (terms.length === 0) {
        await removeChip(index);
        return;
      }
    } else {
      terms = [chip.type === 'not' ? stripNotPrefix(trimmed) : trimmed];
    }
    if (terms.join('\u0000') === chip.terms.join('\u0000')) {
      return;
    }
    chips = chips.map((item, at) => (at === index ? { ...item, terms } : item));
    if (autoRunSearch) {
      await runSearch();
    }
  }

  function stripNotPrefix(text: string): string {
    const stripped = text.replace(/^(?:not\b|!)\s*/i, '').trim();
    return stripped || text;
  }

  async function setChipMode(enabled: boolean): Promise<void> {
    if (enabled === chipMode) {
      return;
    }
    if (enabled && chips.length === 0 && searchPattern.trim()) {
      addChipText(searchPattern, false);
    }
    chipMode = enabled;
  }

  function setLiveSearch(enabled: boolean): void {
    liveSearch = enabled;
  }

  // A growing log fires file.changed repeatedly; collapse the bursts into one
  // re-search so the filtered view keeps up without re-running on every tick.
  function scheduleLiveSearch(): void {
    if (liveSearchTimer !== null) {
      window.clearTimeout(liveSearchTimer);
    }
    liveSearchTimer = window.setTimeout(() => {
      liveSearchTimer = null;
      if (!liveSearch || searchId === null || !fileInfo) {
        return;
      }
      if (searchRunning) {
        // Never cancel a search that is still running: remember one pending
        // refresh and start it when the current one completes.
        liveSearchPending = true;
        return;
      }
      liveRefreshInFlight = true;
      void runSearch(true);
    }, 400);
  }

  function pickSearchHistory(value: string): void {
    if (chipMode) {
      // Only real boolean syntax is kept as an opaque chip; compact history such
      // as `a|b`, `a&b` or `!x` is parsed back into the chips it came from.
      if (/[()]| (?:and|or|not) /i.test(value)) {
        chips = [{ id: chipSeq++, type: 'raw', terms: [value] }];
      } else {
        chips = [];
        addChipText(value, false);
      }
    } else {
      searchPattern = value;
    }
  }

  // Parameters of the search that is actually running. The live refresh re-runs
  // this snapshot, never the pattern being typed: with a growing file, reading
  // the input would turn every keystroke into a search for the half-typed
  // pattern and replace the user's results with its own.
  let executedSearch: SearchSnapshot | null = null;

  function currentSearchParams(): SearchSnapshot {
    return createSearchSnapshot(
      {
        chipMode,
        chips,
        pattern: searchPattern,
        regex: searchRegex,
        caseSensitive: searchCase,
        invert: searchInvert,
        fromLine: searchFromLine,
        toLine: searchToLine,
        maxResults: searchMaxResults,
      },
      chipsToExpression,
    );
  }

  function applyExecutedSearchToHighlight(search: SearchSnapshot): void {
    const highlight = snapshotHighlight(search, escapeRegExp);
    activePattern = highlight.pattern;
    activeSearchRegex = highlight.regex;
    activeSearchCase = highlight.caseSensitive;
  }

  async function runSearch(live = false): Promise<void> {
    const search = live ? executedSearch : currentSearchParams();
    if (!fileInfo || !search || !search.expression) {
      return;
    }
    if (live && searchRunning) {
      liveSearchPending = true;
      return;
    }
    errorMessage = '';
    statusMessage = '';

    try {
      if (!live && !search.chipMode) {
        const validation = await api.validatePattern(search.expression, search.regex);
        if (!validation.valid) {
          errorMessage = validation.error || $t('crawler.invalidPattern');
          return;
        }
      }

      if (!live && searchId !== null) {
        await api.cancelSearch(searchId).catch(() => {});
      }

      const fromLine = search.fromLine;
      if (!live && fromLine !== null && totalLines > 0 && fromLine >= totalLines) {
        errorMessage = $t('crawler.searchFromOutOfRange');
        return;
      }
      if (
        !live &&
        search.toLine !== null &&
        totalLines > 0 &&
        search.toLine >= totalLines
      ) {
        searchToLine = totalLines - 1;
        search.toLine = searchToLine;
      }
      if (fromLine !== null && search.toLine !== null && fromLine > search.toLine) {
        errorMessage = $t('crawler.searchFromAfterTo');
        return;
      }

      applyExecutedSearchToHighlight(search);

      // A live refresh with an unchanged pattern/range only scans the appended
      // lines; fall back to a full run when the engine rejects the extension.
      if (
        live &&
        searchId !== null &&
        executedSearch &&
        executedSearch.fromLine === null &&
        executedSearch.toLine === null &&
        totalLines > searchedToLine
      ) {
        const previousSearchId = searchId;
        try {
          await api.extendSearch(previousSearchId, totalLines);
          searchedToLine = totalLines;
          searchRunning = true;
          startSearchPolling(previousSearchId);
          return;
        } catch {
          await api.cancelSearch(previousSearchId).catch(() => {});
        }
      }

      const scanEnd = totalLines;
      const result = await api.runSearch(fileInfo.fileId, snapshotSearchParams(search));

      // History keeps the compact chip syntax (`a|b`), not the generated
      // boolean expression that the engine receives. A live refresh reaches
      // here with unchanged chips, so it must not touch the history.
      if (!live) {
        executedSearch = search;
        const historyEntry = search.chipMode ? chipsToText(search.chips) : search.expression;
        if (historyEntry) {
          searchHistory = rememberSearch(historyEntry, searchHistoryLimit);
        }
      }
      searchId = result.searchId;
      searchedToLine = scanEnd;
      matchCache.clear();
      searchRunning = true;
      if (!live) {
        searchProgress = 0;
        searchTotal = 0;
        matchCache.clear();
        currentMatch = -1;
      }
      statusMessage = '';
      startSearchPolling(result.searchId);
    } catch (error) {
      errorMessage = messageOf(error);
      searchRunning = false;
      liveRefreshInFlight = false;
      liveSearchPending = false;
    }
  }

  async function stopSearch(): Promise<void> {
    stopSearchPolling();
    searchRunning = false;
    liveSearchPending = false;
    liveRefreshInFlight = false;
    if (searchId === null) {
      return;
    }
    // Like the desktop UI's Stop: keep the matches found so far.
    await api.interruptSearch(searchId).catch(() => {});
    viewSearchId = searchId;
    preserveOnRefresh = false;
    projectionRevision += 1;
    statusMessage = '';
  }

  async function changeVisibility(value: string): Promise<void> {
    visibility = value;
    await applyVisibility(searchId);
  }

  function clearHistory(): void {
    clearSearchHistory();
    searchHistory = [];
  }

  function removeHistoryItem(value: string): void {
    searchHistory = removeSearchHistory(value, searchHistoryLimit);
  }

  async function setSearchBoundary(which: 'from' | 'to' | 'clear', line: number): Promise<void> {
    if (which === 'clear') {
      searchFromLine = null;
      searchToLine = null;
    } else if (which === 'from') {
      searchFromLine = line;
    } else {
      searchToLine = line;
    }
    if ((chipMode && chips.length > 0) || (!chipMode && searchPattern.trim())) {
      await runSearch();
    }
  }

  async function stepMatch(direction: 1 | -1): Promise<void> {
    if (!searchId || searchTotal <= 0) {
      return;
    }
    const base = currentMatch < 0 ? (direction > 0 ? -1 : 0) : currentMatch;
    const next = (base + direction + searchTotal) % searchTotal;
    const line = await fetchMatchLine(next);
    if (line === null) {
      return;
    }
    currentMatch = next;
    jumpToLine(line);
    statusMessage = $t('crawler.matchCounter', {
      current: next + 1,
      total: searchTotal,
    });
  }

  async function clearSearchText(): Promise<void> {
    searchPattern = '';
    chips = [];
    activePattern = '';
    executedSearch = null;
    stopSearchPolling();
    if (searchId !== null) {
      await api.cancelSearch(searchId).catch(() => {});
    }
    searchId = null;
    searchedToLine = 0;
    viewSearchId = null;
    preserveOnRefresh = false;
    liveSearchPending = false;
    liveRefreshInFlight = false;
    searchRunning = false;
    searchProgress = 0;
    searchTotal = 0;
    matchCache.clear();
    currentMatch = -1;
    projectionRevision += 1;
    statusMessage = '';
  }

  /**
   * Desktop `MainWindow::copyWithColor` targets the view that has the focus;
   * the shortcut does the same and falls back to the main view.
   */
  export function copySelectionWithColor(): void {
    if (!fileInfo) {
      return;
    }
    if (filteredRef?.hasFocus()) {
      void (async () => {
        await copyLinesWithColor((await filteredRef?.selectionTargetLines?.()) ?? []);
      })();
      return;
    }
    void copyLinesWithColor(viewportRef?.copyTargetLines?.() ?? []);
  }

  // The lines are composed by the core, with the same palette, search highlight
  // and colour labels the views paint, so a paste keeps the colours the user
  // sees instead of raw ANSI escape sequences.
  async function copyLinesWithColor(lines: number[]): Promise<void> {
    if (!fileInfo || lines.length === 0) {
      return;
    }
    const options: Record<string, unknown> = {
      ansiProcessing: 'renderColors',
      palette: {
        text: theme.palette.text,
        base: theme.palette.base,
        dimmedText: theme.palette.dimmed,
        selectionText: theme.palette.selectionText,
        selectionBack: theme.palette.selectionBack,
      },
      searchStartIndex: searchFromLine ?? 0,
      searchEndIndex: searchToLine !== null ? searchToLine + 1 : Math.max(1, totalLines),
      quickHighlighterWords: quickWords,
    };
    if (activePattern) {
      options.patternHighlight = {
        pattern: activePattern,
        caseSensitive: activeSearchCase,
        useRegex: activeSearchRegex,
        variateColors: false,
        foreColor: theme.palette.matchFore,
        backColor: theme.palette.matchBack,
      };
    }
    try {
      // Chunked so a selection larger than one request limit still copies in
      // full; the desktop UI has no such boundary.
      const result = await copyLinesWithColorChunked(
        api,
        fileInfo.fileId,
        searchId,
        lines,
        options,
      );
      await writeRichText(result.html, result.text);
      statusMessage = $t('crawler.copied');
    } catch {
      errorMessage = $t('crawler.copyFailed');
    }
  }

  async function copyText(text: string): Promise<void> {
    if (!text) {
      return;
    }
    try {
      await navigator.clipboard.writeText(text);
      statusMessage = $t('crawler.copied');
    } catch {
      const area = document.createElement('textarea');
      area.value = text;
      area.style.position = 'fixed';
      area.style.opacity = '0';
      document.body.appendChild(area);
      area.select();
      try {
        document.execCommand('copy');
        statusMessage = $t('crawler.copied');
      } catch {
        errorMessage = $t('crawler.copyFailed');
      }
      area.remove();
    }
  }

  async function performSave(
    suggestedName: string,
    total: number,
    fetch: (from: number, count: number) => Promise<LineData[]>,
  ): Promise<void> {
    if (total <= 0) {
      return;
    }
    try {
      const result = await saveLinesToFile({
        suggestedName,
        total,
        fetch,
        onProgress: (done, all) => {
          statusMessage = $t('crawler.saving', {
            done: done.toLocaleString(),
            total: all.toLocaleString(),
          });
        },
      });
      statusMessage = result.saved ? $t('crawler.saved') : '';
    } catch (error) {
      statusMessage = '';
      errorMessage = $t('crawler.saveFailed', { reason: messageOf(error) });
    }
  }

  function saveFileName(suffix: string): string {
    return `${saveBaseName(fileInfo?.path ?? path)}.${suffix}.txt`;
  }

  async function saveMainLines(from: number, count: number, selected: boolean): Promise<void> {
    const info = fileInfo;
    if (!info || count <= 0) {
      return;
    }
    await performSave(saveFileName(selected ? 'selection' : 'saved'), count, async (offset, chunk) => {
      const result = await api.getLines(info.fileId, from + offset, chunk, 'strip');
      return result.lines;
    });
  }

  async function saveFilteredResults(total: number): Promise<void> {
    const info = fileInfo;
    if (!info || total <= 0) {
      return;
    }
    const id = searchId;
    let markNumbers: number[] | null = null;
    await performSave(saveFileName('filtered'), total, async (offset, chunk) => {
      if (id !== null) {
        const result = await api.getFilteredLines(id, offset, chunk, 'strip');
        return result.lines;
      }
      if (markNumbers === null) {
        markNumbers = (await api.listMarks(info.fileId)).marks.slice().sort((a, b) => a - b);
      }
      const rows = markNumbers.slice(offset, offset + chunk);
      if (rows.length === 0) {
        return [];
      }
      const result = await api.call<{ lines: LineData[] }>('lines.getBatch', {
        fileId: info.fileId,
        lines: rows,
        ansi: 'strip',
      });
      return result.lines;
    });
  }

  async function saveFilteredSelection(lines: { line: number; text: string }[]): Promise<void> {
    if (lines.length === 0) {
      return;
    }
    await performSave(saveFileName('selection'), lines.length, async (offset, chunk) =>
      lines.slice(offset, offset + chunk).map((item) => ({
        num: item.line,
        text: item.text,
        segments: [],
      })),
    );
  }

  function linesForRange(start: number, end: number): number[] {
    const from = Math.min(start, end);
    const to = Math.max(start, end);
    const lines: number[] = [];
    for (let line = from; line <= to; line++) {
      lines.push(line);
    }
    return lines;
  }

  async function toggleMarkLines(lines: number[]): Promise<void> {
    if (!fileInfo || lines.length === 0) {
      return;
    }
    const unique = [...new Set(lines)];
    const allMarked = unique.every((line) => marks.has(line));
    const next = new Set(marks);
    for (const line of unique) {
      if (allMarked) {
        next.delete(line);
      } else {
        next.add(line);
      }
    }
    marks = next;
    frameRevision += 1;
    // Marks join the projection's union, so its rows and flags change.
    projectionRevision += 1;
    try {
      // Chunked by the advertised request limit so marking a whole file keeps
      // the UI state and the engine in sync.
      if (allMarked) {
        await removeMarksChunked(api, fileInfo.fileId, unique);
      } else {
        await addMarksChunked(api, fileInfo.fileId, unique);
      }
    } catch (error) {
      errorMessage = messageOf(error);
      await refreshMarks();
    }
  }

  async function toggleMarkRange(start: number, end: number): Promise<void> {
    await toggleMarkLines(linesForRange(start, end));
  }

  function firstSelectedLine(text: string): string {
    return (
      text
        .split(/\r?\n/)
        .map((line) => line.trim())
        .find((line) => line.length > 0) ?? ''
    );
  }



  function quickLabelName(def: QuickHighlighterDef | undefined, index: number): string {
    const name = def?.name?.trim() ?? '';
    const match = /^Color label (\d+)$/i.exec(name);
    if (match) {
      return $t('crawler.defaultColorLabel', { n: match[1] });
    }
    return name || String(index + 1);
  }

  // The desktop UI keys a colour label by the selected text itself: a portion
  // of a line as selected, or the whole line of a single-line selection.
  function labelKey(text: string): string {
    return colorLabelKey(text).replace(/\r?\n[\s\S]*$/, '');
  }

  function withSlotCount(slots: string[][]): string[][] {
    const next = slots.map((words) => [...words]);
    while (next.length < quickDefs.length) {
      next.push([]);
    }
    return next;
  }

  function applyQuickLabel(index: number, text: string): void {
    const key = labelKey(text);
    if (!key || index < 0 || index >= quickDefs.length) {
      return;
    }
    quickWords = withSlotCount(applyColorLabelToSlots(quickWords, index, key));
    frameRevision += 1;
    const applied = colorLabelOf(quickWords, key) === index;
    statusMessage = applied
      ? $t('crawler.colorLabelApplied', {
          label: quickLabelName(quickDefs[index], index),
          word: key,
        })
      : $t('crawler.colorLabelsCleared');
  }

  function applyNextQuickLabel(text: string): void {
    const key = labelKey(text);
    if (!key) {
      return;
    }
    const result = applyNextColorLabel(quickWords, quickDefs, lastColorLabel, key);
    lastColorLabel = result.label;
    quickWords = withSlotCount(result.slots);
    frameRevision += 1;
    const applied = colorLabelOf(quickWords, key);
    statusMessage =
      applied >= 0
        ? $t('crawler.colorLabelApplied', {
            label: quickLabelName(quickDefs[applied], applied),
            word: key,
          })
        : $t('crawler.colorLabelsCleared');
  }

  function clearQuickLabelForText(text: string): void {
    const key = labelKey(text);
    if (!key) {
      return;
    }
    quickWords = quickWords.map((words) => words.filter((item) => item !== key));
    frameRevision += 1;
    statusMessage = $t('crawler.colorLabelsCleared');
  }

  function clearQuickLabels(): void {
    quickWords = quickDefs.map(() => []);
    frameRevision += 1;
    statusMessage = $t('crawler.colorLabelsCleared');
  }

  async function searchFromText(text: string, invert: boolean): Promise<void> {
    const first = firstSelectedLine(text);
    if (!first) {
      return;
    }
    if (chipMode) {
      chips = [];
      addChipText(first, invert);
      if (autoRunSearch) {
        await runSearch();
      }
      return;
    }
    searchPattern = first;
    searchRegex = false;
    searchCase = false;
    searchInvert = invert;
    activePattern = '';
    activeSearchRegex = false;
    activeSearchCase = false;
    if (autoRunSearch) {
      await runSearch();
    }
  }

  async function addTextToSearch(text: string): Promise<void> {
    const first = firstSelectedLine(text);
    if (!first) {
      return;
    }
    if (chipMode) {
      addChipText(first, false);
      if (autoRunSearch) {
        await runSearch();
      }
      return;
    }
    const current = searchPattern.trim();
    const currentExpr = current ? (searchRegex ? current : escapeRegExp(current)) : '';
    const term = escapeRegExp(first);
    searchPattern = currentExpr ? `(?:${currentExpr})|(?:${term})` : term;
    searchRegex = true;
    searchInvert = false;
    activePattern = '';
    activeSearchRegex = true;
    activeSearchCase = false;
    if (autoRunSearch) {
      await runSearch();
    }
  }

  async function excludeTextFromSearch(text: string): Promise<void> {
    const first = firstSelectedLine(text);
    if (!first) {
      return;
    }
    if (chipMode) {
      addChipText(first, true);
      if (autoRunSearch) {
        await runSearch();
      }
      return;
    }
    const current = searchPattern.trim();
    const currentExpr = current ? (searchRegex ? current : escapeRegExp(current)) : '';
    const term = escapeRegExp(first);
    searchPattern = currentExpr
      ? `^(?!.*(?:${term})).*(?:${currentExpr})`
      : `^(?!.*(?:${term})).*$`;
    searchRegex = true;
    searchInvert = false;
    activePattern = '';
    activeSearchRegex = true;
    activeSearchCase = false;
    if (autoRunSearch) {
      await runSearch();
    }
  }

  function menuPosition(x: number, y: number, itemCount: number): { x: number; y: number } {
    const width = 250;
    const height = itemCount * 27 + 12;
    return {
      x: Math.max(8, Math.min(x, window.innerWidth - width - 8)),
      y: Math.max(8, Math.min(y, window.innerHeight - height - 8)),
    };
  }

  // Both views offer the same colour label submenu; the desktop UI enables it
  // for a portion of a line or a single whole line, not for a line range.
  function colorLabelMenuItem(text: string, enabled: boolean): MenuItem {
    const currentLabel = enabled ? colorLabelOf(quickWords, text) : -1;
    return {
      id: 'color-labels',
      label: $t('crawler.colorLabels'),
      grid: [
        {
          id: 'quick-label-none',
          label: $t('crawler.colorLabelNone'),
          short: '×',
          checked: currentLabel < 0,
          disabled: !enabled,
        },
        ...quickDefs.map((def, index) => ({
          id: `quick-label-${index}`,
          label: quickLabelName(def, index),
          short: String(index + 1),
          swatch: toHtmlColor(def.backColor),
          color: toHtmlColor(def.color),
          checked: currentLabel === index,
          disabled: !enabled,
        })),
        {
          id: 'quick-clear',
          label: $t('crawler.clearAllColorLabels'),
          short: '⌫',
          disabled: quickWords.every((words) => words.length === 0),
        },
      ],
    };
  }

  // One menu for both log views. The selection payload says what the actions act
  // on; `source` only adds the entries that exist in one of the views.
  function openLogContext(request: LogContextRequest): void {
    if (!fileInfo) {
      return;
    }
    const filtered = request.source === 'filtered';
    const lines = selectionLines(request);
    const text = selectionPayloadText(request);
    const allMarked = lines.length > 0 && lines.every((line) => marks.has(line));
    const markLabel = selectionIsMultiRow(request)
      ? allMarked
        ? $t('crawler.markCancelSelected')
        : $t('crawler.markSelected')
      : allMarked
        ? $t('crawler.unmark')
        : $t('crawler.mark');
    const labelText = labelKey(text);
    // The filtered view labels one row at a time; a range has no single key.
    const labelEnabled = !!labelText && (filtered ? selectionRowCount(request) <= 1 : true);
    const items: MenuItem[] = [
      { id: 'mark', label: markLabel },
      { id: 'sep-edit', label: '', separator: true },
      {
        id: 'copy',
        label: $t('crawler.menu.copy'),
        shortcut: filtered ? undefined : 'Ctrl+C',
        disabled: !text,
      },
      { id: 'copy-lines', label: $t('crawler.menu.copyLines'), disabled: lines.length === 0 },
      {
        id: 'copy-color',
        label: $t('crawler.menu.copyColor'),
        shortcut: shortcuts.copyWithColor?.[0] ?? 'Ctrl+Shift+C',
        disabled: lines.length === 0,
      },
      {
        id: 'save-file',
        label: $t('crawler.menu.saveFile'),
        disabled: filtered && (request.total ?? 0) === 0,
      },
      {
        id: 'save-selection',
        label: $t('crawler.menu.saveSelection'),
        disabled: lines.length === 0,
      },
      { id: 'sep-view', label: '', separator: true },
    ];
    if (filtered) {
      items.push({ id: 'jump', label: $t('crawler.menu.filteredJump') });
    } else {
      items.push({
        id: 'select-all',
        label: $t('crawler.menu.selectAll'),
        shortcut: shortcuts.selectAll?.[0] ?? 'Ctrl+A',
      });
    }
    items.push(
      { id: 'sep-search', label: '', separator: true },
      {
        id: 'replace-search',
        label: $t('crawler.menu.replaceSelection'),
        disabled: !request.portion,
      },
      { id: 'add-search', label: $t('crawler.menu.addSearch'), disabled: !request.portion },
      {
        id: 'exclude-search',
        label: $t('crawler.menu.excludeSearch'),
        disabled: !request.portion,
      },
      { id: 'sep-range', label: '', separator: true },
      {
        id: 'search-from-here',
        label: filtered ? $t('crawler.menu.filteredSearchFrom') : $t('crawler.menu.searchFromHere'),
      },
      {
        id: 'search-to-here',
        label: filtered ? $t('crawler.menu.filteredSearchTo') : $t('crawler.menu.searchToHere'),
      },
      {
        id: 'search-clear-range',
        label: filtered
          ? $t('crawler.menu.filteredSearchClear')
          : $t('crawler.menu.clearSearchRange'),
        disabled:
          !filtered && searchFromLine === null && searchToLine === null && searchMaxResults === null,
      },
      { id: 'sep-labels', label: '', separator: true },
      colorLabelMenuItem(labelText, labelEnabled),
    );
    const position = menuPosition(request.x, request.y, items.length);
    contextMenu = { ...position, items, payload: request, kind: 'log' };
  }

  function openFileContext(detail: { x: number; y: number }): void {
    if (!fileInfo) {
      return;
    }
    const items: MenuItem[] = [
      { id: 'file-reload', label: $t('crawler.menu.reload') },
      {
        id: 'file-favorite',
        label: favorite ? $t('favorites.remove') : $t('favorites.add'),
      },
      { id: 'file-sep', label: '', separator: true },
      { id: 'file-copy-path', label: $t('crawler.menu.copyPath') },
      { id: 'file-close', label: $t('crawler.menu.closeFile') },
    ];
    const position = menuPosition(detail.x, detail.y, items.length);
    contextMenu = {
      ...position,
      items,
      payload: { ...detail, path: fileInfo.path },
      kind: 'file',
    };
  }

  function onMenuSelect(id: string): void {
    const menu = contextMenu;
    contextMenu = null;
    if (!menu) {
      return;
    }

    if (menu.kind === 'recorder') {
      if (id === 'recorder-new') {
        dispatch('manageRecorder');
        return;
      }
      if (id === 'recorder-clear') {
        changeRecorderBinding('');
        return;
      }
      if (id.startsWith('recorder-command-')) {
        const index = Number.parseInt(id.slice('recorder-command-'.length), 10);
        const command = recorderCommands[index];
        if (command) {
          changeRecorderBinding(command.name);
        }
      }
      return;
    }

    if (menu.kind === 'log') {
      const request = menu.payload as LogContextRequest;
      const requestLines = selectionLines(request);
      switch (id) {
        case 'mark':
          if (request.source === 'filtered') {
            // The payload only carries the loaded window, so a wide selection is
            // mapped through the result set before marking.
            void (async () => {
              const lines =
                selectionIsMultiRow(request) && filteredRef?.selectionTargetLines
                  ? await filteredRef.selectionTargetLines()
                  : requestLines;
              await toggleMarkLines(lines);
            })();
          } else {
            void toggleMarkRange(selectionStartLine(request), selectionEndLine(request));
          }
          break;
        case 'copy':
          void copyText(selectionPayloadText(request));
          break;
        case 'copy-lines':
          void copyText(selectionCopyLinesText(request));
          break;
        case 'copy-color':
          void copyLinesWithColor(requestLines);
          break;
        case 'save-file':
          if (request.source === 'filtered') {
            void saveFilteredResults(request.total ?? 0);
          } else if (fileInfo) {
            void saveMainLines(0, fileInfo.totalLines, false);
          }
          break;
        case 'save-selection':
          if (request.source === 'filtered') {
            void saveFilteredSelection(request.rows);
          } else if (selectionHasSelection(request)) {
            void saveMainLines(selectionStartLine(request), selectionRowCount(request), true);
          }
          break;
        case 'select-all':
          viewportRef?.selectAll();
          break;
        case 'jump':
          jumpToLine(request.focusLine);
          break;
        case 'replace-search':
          void searchFromText(selectionPortionText(request), false);
          break;
        case 'add-search':
          void addTextToSearch(selectionPortionText(request));
          break;
        case 'exclude-search':
          void excludeTextFromSearch(selectionPortionText(request));
          break;
        case 'search-from-here':
          void setSearchBoundary('from', selectionStartLine(request));
          break;
        case 'search-to-here':
          void setSearchBoundary('to', selectionEndLine(request));
          break;
        case 'search-clear-range':
          void setSearchBoundary('clear', 0);
          break;
        case 'quick-label-none':
          clearQuickLabelForText(selectionPayloadText(request));
          break;
        case 'quick-clear':
          clearQuickLabels();
          break;
        default:
          if (id.startsWith('quick-label-')) {
            const index = Number.parseInt(id.slice('quick-label-'.length), 10);
            applyQuickLabel(index, selectionPayloadText(request));
          }
          break;
      }
      return;
    }

    const payload = menu.payload as FileContext;
    switch (id) {
      case 'file-reload':
        void reloadFile();
        break;
      case 'file-favorite':
        dispatch('favoriteToggle', payload.path);
        break;
      case 'file-copy-path':
        void copyText(payload.path);
        break;
      case 'file-close':
        dispatch('requestClose');
        break;
      default:
        break;
    }
  }
  export function handleEvent(event: ServerEvent): void {
    switch (event.event) {
      case 'file.indexed': {
        const payload = event.payload as IndexedPayload;
        if (fileInfo && payload.fileId === fileInfo.fileId) {
          indexingProgress = null;
          totalLines = payload.totalLines;
          fileInfo = { ...fileInfo, totalLines: payload.totalLines, indexed: true };
          if (currentLine !== null && currentLine >= payload.totalLines) {
            currentLine = payload.totalLines > 0 ? payload.totalLines - 1 : null;
          }
          frameRevision += 1;
          projectionRevision += 1;
          statusMessage = $t('crawler.indexDone', { count: payload.totalLines.toLocaleString() });
          if (follow) {
            scrollToBottom();
          }
        }
        break;
      }
      case 'indexing.progress': {
        const payload = event.payload as IndexingProgressPayload;
        if (!fileInfo && busy) {
          openingFileId = payload.fileId;
        }
        if (stopRequested && openingFileId !== null) {
          stopRequested = false;
          void api.interruptLoading(openingFileId).catch(() => {});
        }
        if (fileInfo && payload.fileId === fileInfo.fileId) {
          indexingProgress = payload.progress;
          statusMessage = $t('crawler.indexingProgress', { progress: payload.progress });
        }
        break;
      }
      case 'file.changed': {
        // The engine has already queued its incremental check/reindex for this
        // event; wait for file.reindexed instead of reloading the whole file.
        break;
      }
      case 'file.reindexed': {
        const payload = event.payload as FileReindexedPayload;
        if (fileInfo && payload.fileId === fileInfo.fileId) {
          const changed = payload.totalLines !== totalLines;
          totalLines = payload.totalLines;
          fileInfo = {
            ...fileInfo,
            totalLines: payload.totalLines,
            sizeBytes: payload.sizeBytes ?? fileInfo.sizeBytes,
            lastModifiedMs: payload.lastModifiedMs ?? fileInfo.lastModifiedMs,
          };
          if (currentLine !== null && currentLine >= payload.totalLines) {
            currentLine = payload.totalLines > 0 ? payload.totalLines - 1 : null;
          }
          frameRevision += 1;
          projectionRevision += 1;
          if (indexingProgress !== null) {
            indexingProgress = null;
            statusMessage = '';
          }
          if (follow) {
            scrollToBottom();
          }
          if (changed && liveSearch && searchId !== null) {
            scheduleLiveSearch();
          }
        }
        break;
      }
      case 'file.indexing_failed': {
        const payload = event.payload as { fileId: number; reason: string };
        if (fileInfo && payload.fileId === fileInfo.fileId) {
          statusMessage = $t('crawler.indexFailed', { reason: payload.reason });
        }
        break;
      }
      case 'search.progress': {
        const payload = event.payload as SearchProgressPayload;
        if (payload.searchId === searchId) {
          searchProgress = payload.ratio;
          searchTotal = payload.matched;
          searchRunning = payload.ratio < 1;
        }
        break;
      }
      case 'search.completed': {
        const payload = event.payload as SearchCompletedPayload;
        if (payload.searchId === searchId) {
          searchRunning = false;
          searchProgress = 1;
          searchTotal =
            searchMaxResults !== null
              ? Math.min(payload.totalMatches, searchMaxResults)
              : payload.totalMatches;
          stopSearchPolling();
          void finishSearch(payload.searchId);
        }
        break;
      }
      case 'marks.changed': {
        const payload = event.payload as MarksChangedPayload;
        if (fileInfo && payload.fileId === fileInfo.fileId) {
          void refreshMarks();
        }
        break;
      }
      case 'engine.error': {
        const payload = event.payload as { fileId: number; message: string };
        if (fileInfo && payload.fileId === fileInfo.fileId) {
          errorMessage = payload.message;
        }
        break;
      }
      case 'view.changed': {
        frameRevision += 1;
        projectionRevision += 1;
        break;
      }
      case 'stream.reset': {
        void refreshInfo();
        void refreshMarks();
        if (searchId !== null) {
          syncMatchNavigation();
        }
        break;
      }
      default:
        break;
    }
  }

  function startSplit(event: PointerEvent): void {
    if (event.button !== 0 || draggingSplit) {
      return;
    }
    draggingSplit = true;
    splitPointerId = event.pointerId;
    try {
      splitHandleEl?.setPointerCapture(event.pointerId);
    } catch {
      // Pointer capture is best-effort; the window listeners still cover the
      // in-window drag.
    }
    event.preventDefault();
  }

  function moveSplit(event: PointerEvent): void {
    if (!draggingSplit || !workspaceEl) {
      return;
    }
    if (splitPointerId !== null && event.pointerId !== splitPointerId) {
      return;
    }
    const rect = workspaceEl.getBoundingClientRect();
    const available = Math.max(1, rect.height - SPLIT_HANDLE_PX);
    mainPercent = clamp(((event.clientY - rect.top) / available) * 100, 0, 100);
    event.preventDefault();
  }

  function endSplit(event?: PointerEvent): void {
    if (!draggingSplit) {
      return;
    }
    if (
      event &&
      splitPointerId !== null &&
      event.pointerId !== undefined &&
      event.pointerId !== splitPointerId
    ) {
      return;
    }
    const pointerId = splitPointerId;
    draggingSplit = false;
    splitPointerId = null;
    if (pointerId !== null) {
      try {
        splitHandleEl?.releasePointerCapture(pointerId);
      } catch {
        // The pointer may already be released by the browser.
      }
    }
    try {
      localStorage.setItem(SPLIT_KEY, String(mainPercent));
    } catch {
      // storage disabled
    }
    viewportRef?.fitAfterResize?.();
  }

  function onGlobalKeyDown(event: KeyboardEvent): void {
    if (event.key === 'Escape') {
      contextMenu = null;
      return;
    }
    if (event.key === 'F3') {
      event.preventDefault();
      void stepMatch(event.shiftKey ? -1 : 1);
      return;
    }
    const target = event.target as HTMLElement | null;
    const editing =
      target &&
      (target.tagName === 'INPUT' ||
        target.tagName === 'TEXTAREA' ||
        target.tagName === 'SELECT' ||
        target.isContentEditable);
    if (!editing && (event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'a') {
      event.preventDefault();
      viewportRef?.selectAll();
    }
  }

  export function refreshHighlights(): void {
    frameRevision += 1;
    projectionRevision += 1;
  }

  export function toggleWrap(): void {
    wrap = !wrap;
  }

  /**
   * Desktop LogViewSelectionUp/Down: the selected line moves and the view only
   * scrolls when the new line would leave it, unlike jumpToLine's centring.
   */
  export function moveCurrentLine(delta: number): void {
    if (totalLines <= 0) {
      return;
    }
    const base = currentLine ?? firstLine;
    const target = clamp(base + delta, 0, totalLines - 1);
    follow = false;
    currentLine = target;
    const visibleRows = Math.max(1, rows);
    if (target < firstLine) {
      firstLine = target;
    } else if (target > firstLine + visibleRows - 1) {
      firstLine = clamp(target - visibleRows + 1, 0, maxFirstLine);
    }
    viewportRef?.selectLine?.(target);
  }

  export function clearMessages(): void {
    statusMessage = '';
    errorMessage = '';
  }

  export function focusSearch(): void {
    const input = document.querySelector('.pane-slot.active .search-input') as HTMLInputElement | null;
    input?.focus();
    input?.select();
  }

  export function focusGoLine(): void {
    const input = document.querySelector('.pane-slot.active .go-line') as HTMLInputElement | null;
    input?.focus();
    input?.select();
  }

  export function getSession(): Record<string, unknown> {
    return {
      opened: fileInfo !== null,
      firstLine,
      currentLine,
      wrap,
      follow,
      visibility,
      searchPattern,
      searchRegex,
      searchCase,
      searchInvert,
      searchFromLine,
      searchToLine,
      searchMaxResults,
      chipMode,
      chips,
      liveSearch,
      quickWords,
      marks: [...marks],
    };
  }

  export function nextMatch(): void {
    void stepMatch(1);
  }

  export function prevMatch(): void {
    void stepMatch(-1);
  }

  export function selectAll(): void {
    // The desktop UI selects the data of the focused view: the whole file in the
    // main view, every row of the result set in the search view.
    if (filteredRef?.hasFocus()) {
      filteredRef?.selectAll();
      return;
    }
    viewportRef?.selectAll();
  }

  export function setVisibility(value: string): void {
    void changeVisibility(value);
  }

  export function gotoLine(line: number): void {
    jumpToLine(line);
  }

  export function scrollToBottomPublic(): void {
    scrollToBottom();
  }

  onMount(() => {
    window.addEventListener('pointerdown', releaseSearchFocus, true);
    if (path) {
      void openFile(path);
    }
    return () => {
      window.removeEventListener('pointerdown', releaseSearchFocus, true);
    };
  });

  onDestroy(() => {
    if (liveSearchTimer !== null) {
      window.clearTimeout(liveSearchTimer);
      liveSearchTimer = null;
    }
    void close();
  });
</script>

<svelte:window
  on:pointermove={moveSplit}
  on:pointerup={endSplit}
  on:pointercancel={endSplit}
  on:keydown={onGlobalKeyDown}
/>

<div
  class="pane-inner"
  role="presentation"
  on:pointerdown|capture={(event) => {
    // A double clicked or dragged word is a transient marker: a left click in
    // the pane drops it, like editors drop the highlighted occurrence when the
    // caret moves. A right press keeps it: the context menu acts on the current
    // selection, and re-rendering the highlight would replace the text nodes
    // that selection points into.
    if (highlightedWord && event.button === 0) {
      setHighlightedWord('');
    }
  }}
>
  <Toolbar
    {fileInfo}
    busy={busy}
    {follow}
    {wrap}
    marksCount={marks.size}
    {firstLine}
    {currentLine}
    {selectionRange}
    message={statusMessage}
    {encodings}
    indexing={indexingProgress !== null}
    {favorite}
    recorderBound={!!activeRecorderCommand}
    recorderRunning={recorderRunning}
    {recorderBusy}
    on:recorderAction={(event) => handleRecorderAction(event.detail)}
    on:reload={() => void reloadFile()}
    on:followChange={(event) => toggleFollow(event.detail)}
    on:wrapChange={(event) => (wrap = event.detail)}
    on:goToLine={(event) => jumpToLine(event.detail)}
    on:nextMark={nextMark}
    on:prevMark={prevMark}
    on:closeFile={() => dispatch('requestClose')}
    on:encodingChange={(event) => void changeEncoding(event.detail)}
    on:stopLoading={stopLoading}
    on:favoriteToggle={() => dispatch('favoriteToggle', fileInfo?.path ?? path)}
    on:fileContextMenu={(event) => openFileContext(event.detail)}
    on:top={() => {
      follow = false;
      currentLine = totalLines > 0 ? 0 : null;
      firstLine = 0;
    }}
  />

  {#if errorMessage}
    <div class="error-banner">
      <span>{errorMessage}</span>
      <button class="ghost" on:click={() => (errorMessage = '')}>×</button>
    </div>
  {/if}

  <div class="workspace" bind:this={workspaceEl}>
    <div class="main-pane" style={`flex:0 0 ${splitBasis(mainPercent)}`}>
      {#if archiveFiles}
        <div class="archive-picker">
          <h2>{$t('archive.title')}</h2>
          <p class="settings-hint">{$t('archive.hint', { count: archiveFiles.length })}</p>
          <div class="archive-list">
            {#each archiveFiles as entry}
              <button class="ghost archive-entry" on:click={() => void openArchiveEntry(entry.path)}>
                <span class="archive-name">{entry.name}</span>
                <span class="archive-size">{formatSize(entry.size)}</span>
              </button>
            {/each}
          </div>
        </div>
      {:else if fileInfo}
        <div class="main-view">
          <LogViewport
            bind:this={viewportRef}
            {api}
            {fileId}
            {totalLines}
            {firstLine}
            {wrap}
            dragging={draggingSplit}
            {searchId}
            pattern={activePattern}
            searchRegex={activeSearchRegex}
            searchCase={activeSearchCase}
            {searchFromLine}
            {searchToLine}
            palette={theme.palette}
            terminalTheme={theme.xterm}
            {fontSize}
            {fontFamily}
            {boldFont}
            showLineNumbers={showMainLineNumbers}
            maxLineLength={fileInfo?.maxLineLength ?? 0}
            {frameRevision}
            {currentLine}
            {quickWords}
            highlightWord={highlightedWord}
            on:wordHighlight={(event) => setHighlightedWord(event.detail)}
            on:currentLineChange={(event) => (currentLine = event.detail)}
            on:lineChange={(event) => {
              follow = false;
              firstLine = clamp(event.detail, 0, maxFirstLine);
            }}
            on:rowsChange={(event) => {
              rows = event.detail;
              if (follow) {
                scrollToBottom();
              }
            }}
            on:selection={(event) => {
              selectionText = event.detail.text;
              selectionRange = event.detail.range;
              applySelectionTime('main', event.detail);
            }}
            on:fontSizeChange={(event) => dispatch('fontSizeChange', event.detail)}
            on:markToggle={(event) => {
              currentLine = event.detail;
              void toggleMark(event.detail);
            }}
            on:contextMenu={(event) => openLogContext({ ...event.detail, source: 'main' })}
            on:error={(event) => (errorMessage = event.detail)}
          />
          <Overview
            {api}
            {fileId}
            {totalLines}
            {searchId}
            {firstLine}
            {rows}
            {frameRevision}
            palette={theme.palette}
            on:seek={(event) => jumpToLine(event.detail)}
          />
        </div>
      {:else}
        <div class="empty-main">
          <div class="empty-logo">klogg</div>
          <h2>{busy ? $t('crawler.emptyOpening') : $t('crawler.emptyNoFile')}</h2>
          <p>{path || $t('crawler.emptyChoose')}</p>
          {#if errorMessage}
            <button class="primary" on:click={() => dispatch('requestOpen')}>
              {$t('crawler.emptyReselect')}
            </button>
          {/if}
        </div>
      {/if}
    </div>

    <div
      class="split-handle"
      role="separator"
      aria-orientation="horizontal"
      title={$t('crawler.splitter')}
      bind:this={splitHandleEl}
      on:pointerdown={startSplit}
      on:lostpointercapture={endSplit}
    ></div>

    <div class="bottom-pane">
      <SearchBar
        bind:visibility
        bind:pattern={searchPattern}
        bind:regex={searchRegex}
        bind:caseSensitive={searchCase}
        bind:invert={searchInvert}
        running={searchRunning}
        quiet={liveRefreshInFlight}
        progress={searchProgress}
        totalMatches={searchTotal}
        disabled={!fileInfo}
        history={searchHistory}
        {chipMode}
        {chips}
        {liveSearch}
        on:run={() => void runSearch()}
        on:stop={stopSearch}
        on:clear={clearSearchText}
        on:visibilityChange={(event) => changeVisibility(event.detail)}
        on:historyClear={clearHistory}
        on:historyRemove={(event) => removeHistoryItem(event.detail)}
        on:historyPick={(event) => void pickSearchHistory(event.detail)}
        on:prevMatch={() => stepMatch(-1)}
        on:nextMatch={() => stepMatch(1)}
        on:chipModeChange={(event) => void setChipMode(event.detail)}
        on:liveSearchChange={(event) => setLiveSearch(event.detail)}
        on:chipAdd={(event) => void addChipAndSearch(event.detail.text, event.detail.negate)}
        on:chipRemove={(event) => void removeChip(event.detail)}
        on:chipEdit={(event) => void editChip(event.detail.index, event.detail.text)}
      />

      {#if fileInfo}
        <FilteredView
          bind:this={filteredRef}
          {api}
          {fileId}
          searchId={viewSearchId}
          {preserveOnRefresh}
          repositionLine={currentLine ?? -1}
          {visibility}
          pattern={activePattern}
          regex={activeSearchRegex}
          caseSensitive={activeSearchCase}
          projectionRevision={projectionRevision}
          displayLimit={searchMaxResults}
          {wrap}
          {follow}
          {fontSize}
          {fontFamily}
          {boldFont}
          showLineNumbers={showFilteredLineNumbers}
          highlightWord={highlightedWord}
          {highlightRules}
          {quickDefs}
          {quickWords}
          on:wordHighlight={(event) => setHighlightedWord(event.detail)}
          on:selectLine={(event) => jumpToLine(event.detail)}
          on:selection={(event) => applySelectionTime('filtered', event.detail)}
          on:fontSizeChange={(event) => dispatch('fontSizeChange', event.detail)}
          on:contextMenu={(event) => openLogContext({ ...event.detail, source: 'filtered' })}
        />
      {:else}
        <div class="filtered-view">
          <div class="filtered-empty">{$t('crawler.filteredEmpty')}</div>
        </div>
      {/if}
    </div>
  </div>

  <SelectionTimeBadge delta={selectionTime?.delta ?? null} />

  {#if contextMenu}
    <ContextMenu
      x={contextMenu.x}
      y={contextMenu.y}
      items={contextMenu.items}
      on:select={(event) => onMenuSelect(event.detail)}
      on:close={() => (contextMenu = null)}
    />
  {/if}

</div>
