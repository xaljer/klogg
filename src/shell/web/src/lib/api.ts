import type {
  ArchiveListing,
  BrowseResult,
  ClientInfo,
  FileInfo,
  HighlightRule,
  LineData,
  PatternInfo,
  QuickHighlighterDef,
  SearchResult,
  ViewportResult,
} from './types';

export class ApiError extends Error {
  code: string;
  status: number;

  constructor(code: string, message: string, status = 0) {
    super(message);
    this.code = code;
    this.status = status;
  }
}

interface RpcEnvelope<T> {
  ok: boolean;
  value?: T;
  error?: { code: string; message: string };
}

export class ApiClient {
  clientId = '';
  // Lines per request the server accepts; advertised by session.connect and used
  // to split bulk calls so a large selection is never silently truncated.
  maxLinesPerRequest = 10000;
  private token = '';

  constructor(token = '') {
    this.token = token;
  }

  /** Lines per chunk for bulk calls, leaving headroom below the server cap. */
  get lineChunkSize(): number {
    return Math.max(1, Math.floor(this.maxLinesPerRequest * 0.8));
  }

  async connect(name = 'web', role = 'human'): Promise<ClientInfo> {
    const info = await this.call<ClientInfo>('session.connect', { name, role });
    this.clientId = info.clientId;
    const advertised = info.limits?.maxLinesPerRequest;
    if (typeof advertised === 'number' && Number.isFinite(advertised) && advertised > 0) {
      this.maxLinesPerRequest = Math.floor(advertised);
    }
    return info;
  }

  async call<T>(method: string, params: Record<string, unknown> = {}): Promise<T> {
    const headers: Record<string, string> = { 'Content-Type': 'application/json' };
    if (this.token) {
      headers.Authorization = `Bearer ${this.token}`;
    }
    if (this.clientId) {
      headers['X-Klogg-Client-Id'] = this.clientId;
    }

    const response = await fetch(`/api/${method}`, {
      method: 'POST',
      headers,
      body: JSON.stringify(params),
    });

    let envelope: RpcEnvelope<T>;
    try {
      envelope = (await response.json()) as RpcEnvelope<T>;
    } catch {
      throw new ApiError('InvalidResponse', `HTTP ${response.status}`, response.status);
    }

    if (!response.ok || !envelope.ok) {
      const error = envelope.error;
      throw new ApiError(
        error?.code ?? `Http${response.status}`,
        error?.message ?? `HTTP ${response.status}`,
        response.status,
      );
    }
    return envelope.value as T;
  }

  systemInfo(): Promise<Record<string, unknown>> {
    return this.call('system.info');
  }

  openFile(path: string, allowMissing = false, encoding?: string): Promise<FileInfo> {
    const params: Record<string, unknown> = { path, allowMissing };
    if (encoding) {
      params.encoding = encoding;
    }
    return this.call<FileInfo>('file.open', params);
  }

  saveTempText(text: string): Promise<{ path: string }> {
    return this.call<{ path: string }>('file.saveTemp', { text });
  }

  openUrl(url: string): Promise<FileInfo | ArchiveListing> {
    return this.call<FileInfo | ArchiveListing>('file.openUrl', { url });
  }

  setEncoding(fileId: number, encoding: string): Promise<void> {
    return this.call<void>('file.setEncoding', { fileId, encoding });
  }

  supportedEncodings(): Promise<{ encodings: string[] }> {
    return this.call<{ encodings: string[] }>('file.supportedEncodings');
  }

  interruptLoading(fileId: number): Promise<void> {
    return this.call<void>('file.interruptLoading', { fileId });
  }

  closeFile(fileId: number): Promise<void> {
    return this.call<void>('file.close', { fileId });
  }

  fileInfo(fileId: number): Promise<FileInfo> {
    return this.call<FileInfo>('file.info', { fileId });
  }

  fileStat(fileId: number): Promise<{ sizeBytes: number; totalLines: number }> {
    return this.call<{ sizeBytes: number; totalLines: number }>('file.stat', { fileId });
  }

  listDirectory(path: string): Promise<BrowseResult> {
    return this.call<BrowseResult>('fs.list', { path });
  }

  reloadFile(fileId: number): Promise<FileInfo> {
    return this.call<FileInfo>('file.reload', { fileId });
  }

  checkFileChanges(fileId: number): Promise<void> {
    return this.call<void>('file.checkChanges', { fileId });
  }

  watchFile(fileId: number, enabled = true): Promise<void> {
    return this.call<void>('file.watch', { fileId, enabled });
  }

  validatePattern(pattern: string, isRegex: boolean): Promise<PatternInfo> {
    return this.call<PatternInfo>('search.validatePattern', { pattern, isRegex });
  }

  runSearch(
    fileId: number,
    params: {
      pattern: string;
      caseSensitive: boolean;
      isRegex: boolean;
      invertMatch: boolean;
      isBoolean?: boolean;
      fromLine?: number | null;
      toLine?: number | null;
      maxResults?: number | null;
    },
  ): Promise<{ searchId: number }> {
    return this.call<{ searchId: number }>('search.run', { fileId, params });
  }

  extendSearch(searchId: number, toLine: number): Promise<void> {
    return this.call<void>('search.extend', { searchId, toLine });
  }

  setHighlighters(rules: HighlightRule[]): Promise<void> {
    return this.call<void>('highlighters.set', { rules });
  }

  setQuickHighlighters(defs: QuickHighlighterDef[]): Promise<void> {
    return this.call<void>('quickHighlighters.set', { defs });
  }

  searchProgress(searchId: number): Promise<SearchResult> {
    return this.call<SearchResult>('search.progress', { searchId });
  }

  searchMatches(searchId: number, from: number, count: number): Promise<{ matches: number[] }> {
    return this.call<{ matches: number[] }>('search.matches', { searchId, from, count });
  }

  getLines(
    fileId: number,
    from: number,
    count: number,
    ansi: 'raw' | 'strip' = 'strip',
  ): Promise<{ lines: LineData[] }> {
    return this.call<{ lines: LineData[] }>('lines.get', { fileId, from, count, ansi });
  }

  getFilteredLines(
    searchId: number,
    fromIndex: number,
    count: number,
    ansi: 'raw' | 'strip' = 'strip',
  ): Promise<{ lines: LineData[] }> {
    return this.call<{ lines: LineData[] }>('search.filteredLines', {
      searchId,
      fromIndex,
      count,
      ansi,
    });
  }

  /**
   * Index in the search's filtered projection of the last row at or before a
   * source line; null when the projection does not cover that line.
   */
  filteredLineIndexAtOrBefore(
    searchId: number,
    line: number,
  ): Promise<{ index: number | null }> {
    return this.call<{ index: number | null }>('search.filteredLineIndexAtOrBefore', {
      searchId,
      line,
    });
  }

  startRecorder(
    commandName: string,
    filePath: string,
  ): Promise<{ processId: number; pid: number; state: string }> {
    return this.call('recorder.start', { commandName, filePath });
  }

  recorderConfig(): Promise<{
    commands: { name: string; command: string }[];
    bindings: { file: string; command: string }[];
  }> {
    return this.call('recorder.config');
  }

  setRecorderCommands(commands: { name: string; command: string }[]): Promise<void> {
    return this.call<void>('recorder.commands.set', { commands });
  }

  setRecorderBinding(filePath: string, commandName: string): Promise<void> {
    return this.call<void>('recorder.binding.set', { filePath, commandName });
  }

  stopRecorder(processId: number): Promise<void> {
    return this.call<void>('recorder.stop', { processId });
  }

  recorderStatus(processId: number): Promise<{
    processId: number;
    state: string;
    exitCode: number;
    pid: number;
    error: string;
  }> {
    return this.call('recorder.status', { processId });
  }

  cancelSearch(searchId: number): Promise<void> {
    return this.call<void>('search.cancel', { searchId });
  }

  interruptSearch(searchId: number): Promise<void> {
    return this.call<void>('search.interrupt', { searchId });
  }

  listMarks(fileId: number): Promise<{ marks: number[] }> {
    return this.call<{ marks: number[] }>('marks.list', { fileId });
  }

  addMarks(fileId: number, lines: number[]): Promise<{ truncated?: boolean }> {
    return this.call<{ truncated?: boolean }>('marks.add', { fileId, lines });
  }

  removeMarks(fileId: number, lines: number[]): Promise<{ truncated?: boolean }> {
    return this.call<{ truncated?: boolean }>('marks.remove', { fileId, lines });
  }

  markAfter(fileId: number, line: number): Promise<{ line: number | null }> {
    return this.call<{ line: number | null }>('marks.after', { fileId, line });
  }

  markBefore(fileId: number, line: number): Promise<{ line: number | null }> {
    return this.call<{ line: number | null }>('marks.before', { fileId, line });
  }

  renderViewport(
    fileId: number,
    searchId: number | null,
    firstLine: number,
    visibleLines: number,
    options: Record<string, unknown>,
  ): Promise<ViewportResult> {
    return this.call<ViewportResult>('render.viewport', {
      fileId,
      searchId,
      firstLine,
      visibleLines,
      options,
    });
  }

  // The desktop "copy with color" payload: the lines composed by the same code
  // that paints the views, as HTML plus the plain text counterpart.
  renderLinesHtml(
    fileId: number,
    searchId: number | null,
    lines: number[],
    options: Record<string, unknown>,
  ): Promise<{ html: string; text: string; truncated?: boolean }> {
    return this.call<{ html: string; text: string; truncated?: boolean }>('render.linesHtml', {
      fileId,
      searchId,
      lines,
      options,
    });
  }
}
