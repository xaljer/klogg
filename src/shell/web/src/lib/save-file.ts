import type { LineData } from './types';

export interface SaveResult {
  saved: boolean;
  canceled?: boolean;
  fileName?: string;
}

export interface SaveLinesOptions {
  suggestedName: string;
  total: number;
  fetch: (from: number, count: number) => Promise<LineData[]>;
  onProgress?: (done: number, total: number) => void;
}

interface WritableFile {
  write(data: Blob | string): Promise<void>;
  close(): Promise<void>;
  abort?(): Promise<void>;
}

interface SaveFileHandle {
  name: string;
  createWritable(): Promise<WritableFile>;
}

type SaveFilePicker = (options: {
  suggestedName?: string;
  types?: { description?: string; accept: Record<string, string[]> }[];
}) => Promise<SaveFileHandle>;

const CHUNK_LINES = 2000;

export function saveBaseName(path: string): string {
  const fileName = path.split(/[\\/]/).pop() ?? path;
  return fileName.replace(/\.[^.]+$/, '') || 'klogg';
}

function sanitizeFileName(name: string): string {
  return name.replace(/[\\/:*?"<>|]/g, '_');
}

function triggerDownload(name: string, blob: Blob): void {
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = name;
  anchor.style.display = 'none';
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  window.setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function picker(): SaveFilePicker | null {
  const candidate = (window as unknown as { showSaveFilePicker?: SaveFilePicker })
    .showSaveFilePicker;
  return typeof candidate === 'function' ? candidate.bind(window) : null;
}

function pickerOptions(name: string) {
  return {
    suggestedName: name,
    types: [
      {
        description: 'Text file',
        accept: { 'text/plain': ['.txt', '.log'] },
      },
    ],
  };
}

async function saveViaBlob(
  suggestedName: string,
  total: number,
  fetch: SaveLinesOptions['fetch'],
  onProgress: SaveLinesOptions['onProgress'],
): Promise<SaveResult> {
  const parts: string[] = [];
  let done = 0;
  while (done < total) {
    const count = Math.min(CHUNK_LINES, total - done);
    const lines = await fetch(done, count);
    if (lines.length === 0) {
      break;
    }
    parts.push(`${lines.map((line) => line.text).join('\n')}\n`);
    done += lines.length;
    onProgress?.(done, total);
    if (lines.length < count) {
      break;
    }
  }
  triggerDownload(suggestedName, new Blob(parts, { type: 'text/plain;charset=utf-8' }));
  return { saved: true, fileName: suggestedName };
}

export async function saveLinesToFile(options: SaveLinesOptions): Promise<SaveResult> {
  const name = sanitizeFileName(options.suggestedName || 'klogg.txt');
  const choose = picker();
  let handle: SaveFileHandle | null = null;

  // Ask for the destination before any network request so the call remains
  // inside the browser's user-activation window (required by the picker).
  if (choose) {
    try {
      handle = await choose(pickerOptions(name));
    } catch (error) {
      if (error instanceof DOMException && error.name === 'AbortError') {
        return { saved: false, canceled: true };
      }
      // Other picker failures (unsupported/permission) fall back to a download.
      handle = null;
    }
  }

  if (!handle) {
    return saveViaBlob(name, options.total, options.fetch, options.onProgress);
  }

  const writable = await handle.createWritable();
  let done = 0;
  try {
    while (done < options.total) {
      const count = Math.min(CHUNK_LINES, options.total - done);
      const lines = await options.fetch(done, count);
      if (lines.length === 0) {
        break;
      }
      await writable.write(`${lines.map((line) => line.text).join('\n')}\n`);
      done += lines.length;
      options.onProgress?.(done, options.total);
      if (lines.length < count) {
        break;
      }
    }
    await writable.close();
    return { saved: true, fileName: handle.name };
  } catch (error) {
    await writable.abort?.().catch(() => {});
    throw error;
  }
}
