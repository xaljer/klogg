export interface ApiErrorBody {
  code: string;
  message: string;
}

export interface TabMeta {
  id: string;
  title: string;
  path: string;
  pinned?: boolean;
}

export interface FileInfo {
  fileId: number;
  path: string;
  sizeBytes: number;
  totalLines: number;
  maxLineLength: number;
  encoding: { name: string; confidence: number };
  lastModifiedMs: number;
  indexed: boolean;
}

export interface ArchiveEntry {
  name: string;
  path: string;
  size: number;
}

export interface ArchiveListing {
  archive: {
    files: ArchiveEntry[];
  };
}
export interface LineData {
  num: number;
  text: string;
  segments: AnsiSegment[];
}

export interface AnsiSegment {
  start: number;
  end: number;
  fg: string;
  bg: string;
  bold: boolean;
  italic: boolean;
  underline: boolean;
}

export interface WrapPart {
  line: number;
  start: number;
  length: number;
}

export interface LineChunk {
  start: number;
  length: number;
  foreColor: string;
  backColor: string;
  startsRow: boolean;
}

export interface ViewportLine {
  text: string;
  parts: WrapPart[];
  chunks: LineChunk[];
  lineNumber: number;
  displayLineNumber: number;
  flags: number;
  foreColor: string;
  backColor: string;
}

export interface ViewportResult {
  lines: ViewportLine[];
}

export interface HighlightRule {
  name: string;
  pattern: string;
  useRegex: boolean;
  caseSensitive: boolean;
  color: string;
  backColor: string;
  useThemeBack: boolean;
  onlyMatch: boolean;
  variateColors: boolean;
  colorVariance: number;
  enabled: boolean;
}

export interface QuickHighlighterDef {
  name: string;
  color: string;
  backColor: string;
  useInCycle: boolean;
}

// A named, ordered list of rules. Only one set is active at a time; its rules
// are sent to the viewport in list order (earlier rules win).
export interface HighlighterSet {
  id: string;
  name: string;
  rules: HighlightRule[];
}

export type SearchChipType = 'or' | 'and' | 'not' | 'notAnd' | 'raw';

export interface SearchChip {
  id: number;
  type: SearchChipType;
  terms: string[];
}

export interface SearchResult {
  searchId: number;
  totalMatches: number;
  processedLines: number;
  progress: number;
}

export interface PatternInfo {
  valid: boolean;
  error: string;
  isPlainText: boolean;
}

export interface ClientInfo {
  clientId: string;
  apiVersion: string;
  serverVersion: string;
  capabilities: string[];
  role: string;
  // Transport limits the client has to respect when sending bulk line lists.
  limits?: { maxLinesPerRequest?: number };
}

export interface BrowseEntry {
  name: string;
  path: string;
  directory: boolean;
  sizeBytes: number;
}

export interface BrowseResult {
  path: string;
  parent: string | null;
  roots: { name: string; path: string }[];
  entries: BrowseEntry[];
  truncated: boolean;
}

export interface ServerEvent<T = unknown> {
  event: string;
  seq: number;
  originClientId: string;
  payload: T;
}

export interface SearchProgressPayload {
  searchId: number;
  matched: number;
  ratio: number;
}

export interface SearchCompletedPayload {
  searchId: number;
  totalMatches: number;
  durationMs: number;
}

export interface IndexedPayload {
  fileId: number;
  totalLines: number;
  encoding: string;
}

export interface IndexingProgressPayload {
  fileId: number;
  progress: number;
}

export interface FileChangedPayload {
  fileId: number;
  reason: 'modified' | 'truncated';
}

export interface FileReindexedPayload {
  fileId: number;
  totalLines: number;
  sizeBytes?: number;
  lastModifiedMs?: number;
}

export interface MarksChangedPayload {
  fileId: number;
}

export interface EngineErrorPayload {
  fileId: number;
  kind: string;
  message: string;
}

export const LineFlagMark = 1;
export const LineFlagMatch = 2;
export const LineFlagEvent = 4;
