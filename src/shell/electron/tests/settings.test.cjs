const assert = require('node:assert/strict');
const path = require('node:path');
const test = require('node:test');

const { resolveResources, serverExeName } = require('../electron/paths.cjs');
const {
  defaultDesktopSettings,
  loadDesktopSettings,
  saveDesktopSettings,
} = require('../electron/settings.cjs');
const fs = require('node:fs');
const os = require('node:os');

test('resolves packaged server and web resources', () => {
  const resourcesPath = path.join('C:', 'Program Files', 'klogg', 'resources');
  assert.deepEqual(
    resolveResources({
      isPackaged: true,
      resourcesPath,
      appDir: path.join('C:', 'src', 'shell', 'electron'),
    }),
    {
      serverPath: path.join(resourcesPath, 'server', serverExeName),
      webRoot: path.join(resourcesPath, 'web'),
    },
  );
});

test('resolves development resources from the repository and honours an override', () => {
  const appDir = path.join('C:', 'workspace', 'klogg', 'src', 'shell', 'electron');
  const resolved = resolveResources({ isPackaged: false, resourcesPath: '', appDir });
  assert.equal(
    resolved.serverPath,
    path.join('C:', 'workspace', 'klogg', 'out', 'server', 'output', serverExeName),
  );
  assert.equal(resolved.webRoot, path.join('C:', 'workspace', 'klogg', 'out', 'web'));

  const override = path.join('C:', 'custom', 'klogg_server.exe');
  assert.equal(
    resolveResources({
      isPackaged: false,
      resourcesPath: '',
      appDir,
      serverOverride: override,
    }).serverPath,
    override,
  );
});

test('desktop settings provide server-mode defaults and a token', () => {
  const settings = defaultDesktopSettings();
  assert.equal(settings.serverMode, false);
  assert.equal(settings.listenAddress, '0.0.0.0');
  assert.equal(settings.port, 8080);
  assert.match(settings.token, /^[0-9a-f]{48}$/);
});

test('desktop settings sanitize invalid values and round-trip', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'klogg-desktop-settings-'));
  const file = path.join(dir, 'desktop-settings.json');
  fs.writeFileSync(
    file,
    JSON.stringify({
      serverMode: true,
      listenAddress: '',
      port: 99999,
      token: '',
    }),
  );

  const loaded = loadDesktopSettings(file);
  assert.equal(loaded.serverMode, true);
  assert.equal(loaded.listenAddress, '0.0.0.0');
  assert.equal(loaded.port, 8080);
  assert.match(loaded.token, /^[0-9a-f]{48}$/);

  saveDesktopSettings(file, loaded);
  assert.deepEqual(loadDesktopSettings(file), loaded);
  fs.rmSync(dir, { recursive: true, force: true });
});
