import type { ApiClient } from './api';

/**
 * Bulk line operations (copy with colour, marks) are split into requests the
 * transport accepts. The server advertises the per-request line limit and the
 * desktop UI has no such limit at all, so the split has to stay invisible: the
 * merged result covers every selected line, in order.
 */

/** Thrown when the server reports that it clipped a bulk request. */
export class TruncatedRequestError extends Error {
  constructor(message = 'server truncated the request') {
    super(message);
    this.name = 'TruncatedRequestError';
  }
}

/** Splits a list into consecutive chunks of at most `size` items. */
export function chunkItems<T>(items: T[], size: number): T[][] {
  const step = Math.max(1, Math.floor(size));
  const chunks: T[][] = [];
  for (let index = 0; index < items.length; index += step) {
    chunks.push(items.slice(index, index + step));
  }
  return chunks;
}

interface PreParts {
  open: string;
  body: string;
}

function splitPre(html: string): PreParts {
  const match = /^(<pre[^>]*>)([\s\S]*?)(<\/pre>\s*)$/.exec(html);
  if (!match) {
    return { open: '', body: html };
  }
  return { open: match[1], body: match[2] };
}

export interface CopiedLines {
  html: string;
  text: string;
}

/** Same result as one `render.linesHtml` call, assembled from chunked requests. */
export async function copyLinesWithColorChunked(
  api: ApiClient,
  fileId: number,
  searchId: number | null,
  lines: number[],
  options: Record<string, unknown>,
): Promise<CopiedLines> {
  if (lines.length === 0) {
    return { html: '', text: '' };
  }
  const chunks = chunkItems(lines, api.lineChunkSize);
  let open = '<pre>';
  const bodies: string[] = [];
  const texts: string[] = [];
  for (const chunk of chunks) {
    const result = await api.renderLinesHtml(fileId, searchId, chunk, options);
    if (result.truncated) {
      throw new TruncatedRequestError(
        `rendered ${chunk.length} lines but the server answered with fewer`,
      );
    }
    const parts = splitPre(result.html);
    if (bodies.length === 0 && parts.open) {
      open = parts.open;
    }
    bodies.push(parts.body);
    texts.push(result.text);
  }
  return { html: `${open}${bodies.join('\n')}</pre>`, text: texts.join('\n') };
}

export async function addMarksChunked(
  api: ApiClient,
  fileId: number,
  lines: number[],
): Promise<void> {
  for (const chunk of chunkItems(lines, api.lineChunkSize)) {
    const result = await api.addMarks(fileId, chunk);
    if (result?.truncated) {
      throw new TruncatedRequestError(`added ${chunk.length} marks but the server clipped them`);
    }
  }
}

export async function removeMarksChunked(
  api: ApiClient,
  fileId: number,
  lines: number[],
): Promise<void> {
  for (const chunk of chunkItems(lines, api.lineChunkSize)) {
    const result = await api.removeMarks(fileId, chunk);
    if (result?.truncated) {
      throw new TruncatedRequestError(`removed ${chunk.length} marks but the server clipped them`);
    }
  }
}
