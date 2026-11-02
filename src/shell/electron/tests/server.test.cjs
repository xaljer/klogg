const assert = require('node:assert/strict');
const test = require('node:test');

const {
  buildServerArgs,
  localServerUrl,
  parseServerUrl,
  redactServerArgs,
} = require('../electron/server.cjs');

test('parses the klogg server startup line', () => {
  assert.equal(
    parseServerUrl('klogg_server 26.7.4 listening on http://127.0.0.1:49152'),
    'http://127.0.0.1:49152',
  );
});

test('rejects unrelated stdout and malformed urls', () => {
  assert.equal(parseServerUrl('some other line'), null);
  assert.equal(parseServerUrl('klogg_server listening on ftp://127.0.0.1:1'), null);
  assert.equal(parseServerUrl('klogg_server listening on http://127.0.0.1'), null);
});

test('builds loopback and server-mode arguments', () => {
  assert.deepEqual(
    buildServerArgs({ webRoot: 'C:\\web', listenAddress: '127.0.0.1', port: 0, token: '' }),
    ['--port', '0', '--listen', '127.0.0.1', '--web-root', 'C:\\web'],
  );
  assert.deepEqual(
    buildServerArgs({
      webRoot: 'C:\\web',
      listenAddress: '0.0.0.0',
      port: 8080,
      token: 'secret',
    }),
    [
      '--port',
      '8080',
      '--listen',
      '0.0.0.0',
      '--token',
      'secret',
      '--web-root',
      'C:\\web',
    ],
  );
});

test('redacts the bearer token from the server log', () => {
  const args = ['--port', '8080', '--listen', '0.0.0.0', '--token', 'secret', '--web-root', 'C:\\web'];
  const redacted = redactServerArgs(args).join(' ');
  assert.equal(redacted.includes('secret'), false);
  assert.equal(redacted.includes('--token <redacted>'), true);
});

test('maps a wildcard listen address to loopback for local windows', () => {
  assert.equal(localServerUrl('http://0.0.0.0:8080'), 'http://127.0.0.1:8080');
  assert.equal(localServerUrl('http://[::]:8080'), 'http://127.0.0.1:8080');
  assert.equal(localServerUrl('http://127.0.0.1:49152'), 'http://127.0.0.1:49152');
});
