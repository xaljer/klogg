import assert from 'node:assert/strict';
import test from 'node:test';

import { writeRichText } from '../src/lib/clipboard.ts';

interface ClipboardStub {
  writes: unknown[][];
  texts: string[];
}

function installClipboard(options: { richFails?: boolean } = {}): ClipboardStub {
  const stub: ClipboardStub = { writes: [], texts: [] };
  const navigatorStub = {
    clipboard: {
      write: async (items: unknown[]) => {
        if (options.richFails) {
          throw new Error('denied');
        }
        stub.writes.push(items);
      },
      writeText: async (text: string) => {
        stub.texts.push(text);
      },
    },
  };
  Object.defineProperty(globalThis, 'navigator', {
    value: navigatorStub,
    configurable: true,
    writable: true,
  });
  (globalThis as any).ClipboardItem = class {
    data: Record<string, unknown>;
    constructor(data: Record<string, unknown>) {
      this.data = data;
    }
  };
  return stub;
}

test('rich text goes through the clipboard API with both flavours', async () => {
  const stub = installClipboard();
  await writeRichText('<pre>colored</pre>', 'colored');
  assert.equal(stub.writes.length, 1);
  const item = stub.writes[0][0] as { data: Record<string, unknown> };
  assert.deepEqual(Object.keys(item.data).sort(), ['text/html', 'text/plain']);
  assert.equal(stub.texts.length, 0);
});

test('a rejected rich write falls back to the copy event', async () => {
  const stub = installClipboard({ richFails: true });
  const listeners: ((event: unknown) => void)[] = [];
  (globalThis as any).document = {
    addEventListener: (_name: string, listener: (event: unknown) => void) => {
      listeners.push(listener);
    },
    removeEventListener: () => {},
    execCommand: () => {
      const data = new Map<string, string>();
      const event = {
        clipboardData: {
          setData: (type: string, value: string) => data.set(type, value),
        },
        preventDefault: () => {},
      };
      for (const listener of listeners) {
        listener(event);
      }
      return data.get('text/html') === '<pre>colored</pre>';
    },
  };

  await writeRichText('<pre>colored</pre>', 'colored');
  assert.equal(stub.writes.length, 0);
  assert.equal(stub.texts.length, 0);
});

test('without any rich support the plain text is still copied', async () => {
  const stub = installClipboard();
  delete (globalThis as any).ClipboardItem;
  (globalThis as any).document = {
    addEventListener: () => {},
    removeEventListener: () => {},
    execCommand: () => false,
  };
  await writeRichText('<pre>colored</pre>', 'colored');
  assert.deepEqual(stub.texts, ['colored']);
});
