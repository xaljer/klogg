import assert from 'node:assert/strict';
import test from 'node:test';

import {
  addMarksChunked,
  chunkItems,
  copyLinesWithColorChunked,
  removeMarksChunked,
  TruncatedRequestError,
} from '../src/lib/bulk.ts';

interface FakeCall {
  lines: number[];
}

function fakeApi(chunkSize: number) {
  const renderCalls: FakeCall[] = [];
  const addCalls: FakeCall[] = [];
  const removeCalls: FakeCall[] = [];
  const truncated = { render: false, marks: false };
  const api = {
    lineChunkSize: chunkSize,
    async renderLinesHtml(_fileId: number, _searchId: number | null, lines: number[]) {
      renderCalls.push({ lines: [...lines] });
      const body = lines.map((line) => `line ${line}`).join('\n');
      return {
        html: `<pre style="font-family:monospace">${body}</pre>`,
        text: body,
        truncated: truncated.render,
      };
    },
    async addMarks(_fileId: number, lines: number[]) {
      addCalls.push({ lines: [...lines] });
      return { truncated: truncated.marks };
    },
    async removeMarks(_fileId: number, lines: number[]) {
      removeCalls.push({ lines: [...lines] });
      return { truncated: truncated.marks };
    },
  };
  return { api, renderCalls, addCalls, removeCalls, truncated };
}

test('chunkItems keeps the order and handles every boundary', () => {
  assert.deepEqual(chunkItems([], 10), []);
  assert.deepEqual(chunkItems([1], 10), [[1]]);
  assert.deepEqual(chunkItems([1, 2, 3, 4], 2), [
    [1, 2],
    [3, 4],
  ]);
  assert.deepEqual(chunkItems([1, 2, 3, 4, 5], 2), [[1, 2], [3, 4], [5]]);
  assert.deepEqual(chunkItems([1, 2], 0), [[1], [2]], 'a zero size still advances');
});

test('copy with color merges chunked answers into one pre block', async () => {
  const { api, renderCalls } = fakeApi(2);
  const result = await copyLinesWithColorChunked(
    api as never,
    7,
    null,
    [0, 1, 2, 3, 4],
    {},
  );

  assert.deepEqual(
    renderCalls.map((call) => call.lines),
    [[0, 1], [2, 3], [4]],
  );
  assert.equal(result.text, 'line 0\nline 1\nline 2\nline 3\nline 4');
  assert.ok(result.html.startsWith('<pre style="font-family:monospace">'));
  assert.ok(result.html.endsWith('</pre>'));
  assert.equal(result.html.split('<pre').length - 1, 1, 'a single pre wrapper');
  assert.ok(result.html.includes('line 0\nline 1\nline 2\nline 3\nline 4'));
});

test('copy with color keeps every line and reports a clipped chunk', async () => {
  const { api, truncated } = fakeApi(2);
  truncated.render = true;
  await assert.rejects(
    copyLinesWithColorChunked(api as never, 7, null, [0, 1], {}),
    TruncatedRequestError,
  );
});

test('marks are added and removed in chunks', async () => {
  const { api, addCalls, removeCalls } = fakeApi(3);
  await addMarksChunked(api as never, 1, [0, 1, 2, 3, 4, 5, 6]);
  await removeMarksChunked(api as never, 1, [0, 1]);
  assert.deepEqual(
    addCalls.map((call) => call.lines),
    [
      [0, 1, 2],
      [3, 4, 5],
      [6],
    ],
  );
  assert.deepEqual(
    removeCalls.map((call) => call.lines),
    [[0, 1]],
  );
});

test('a clipped marks request is an error, not a silent partial update', async () => {
  const { api, truncated } = fakeApi(2);
  truncated.marks = true;
  await assert.rejects(addMarksChunked(api as never, 1, [0, 1, 2]), TruncatedRequestError);
  await assert.rejects(removeMarksChunked(api as never, 1, [0, 1]), TruncatedRequestError);
});

test('an empty line list costs no request', async () => {
  const { api, renderCalls } = fakeApi(2);
  const result = await copyLinesWithColorChunked(api as never, 7, null, [], {});
  assert.deepEqual(result, { html: '', text: '' });
  assert.equal(renderCalls.length, 0);
});
