import assert from 'node:assert/strict';
import { afterEach, test } from 'node:test';

import { ApiClient } from '../src/lib/api.ts';

const originalFetch = globalThis.fetch;

afterEach(() => {
  globalThis.fetch = originalFetch;
});

test('API requests send a bearer token when configured', async () => {
  let authorization = '';
  globalThis.fetch = async (_input, init) => {
    authorization = (init?.headers as Record<string, string>).Authorization;
    return new Response(JSON.stringify({ ok: true, value: {} }), { status: 200 });
  };

  await new ApiClient('local-token').call('system.info');

  assert.equal(authorization, 'Bearer local-token');
});

test('API requests omit authorization when no token is configured', async () => {
  let authorization: string | undefined;
  globalThis.fetch = async (_input, init) => {
    authorization = (init?.headers as Record<string, string>).Authorization;
    return new Response(JSON.stringify({ ok: true, value: {} }), { status: 200 });
  };

  await new ApiClient().call('system.info');

  assert.equal(authorization, undefined);
});

test('search.extend posts the search id and exclusive end line', async () => {
  let url = '';
  let body = '';
  globalThis.fetch = async (input, init) => {
    url = String(input);
    body = String(init?.body ?? '');
    return new Response(JSON.stringify({ ok: true, value: {} }), { status: 200 });
  };

  await new ApiClient().extendSearch(7, 1234);

  assert.equal(url, '/api/search.extend');
  assert.deepEqual(JSON.parse(body), { searchId: 7, toLine: 1234 });
});
