const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const { createServerManager } = require('../electron/server.cjs');
const { serverExeName } = require('../electron/paths.cjs');

const repoRoot = path.resolve(__dirname, '..', '..', '..', '..');
const serverPath =
  process.env.KLOGG_SERVER_BIN ||
  path.join(repoRoot, 'out', 'server', 'output', serverExeName);
const webRoot = path.join(repoRoot, 'src', 'shell', 'web', 'dist');

const canRun = fs.existsSync(serverPath) && fs.existsSync(path.join(webRoot, 'index.html'));

test('starts a real server on an ephemeral port and stops it', { skip: !canRun }, async () => {
  const manager = createServerManager({ serverPath, webRoot });
  try {
    const url = await manager.start({ listenAddress: '127.0.0.1', port: 0 });
    assert.match(url, /^http:\/\/127\.0\.0\.1:\d+$/);
    const health = await fetch(`${url}/health`);
    assert.equal(health.status, 200);
    assert.equal((await health.json()).ok, true);
    assert.ok(manager.pid > 0);
  } finally {
    await manager.stop();
  }
  assert.equal(manager.pid, 0);
});
