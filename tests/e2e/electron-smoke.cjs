const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { _electron: electron } = require('playwright');

const appExe =
  process.env.KLOGG_ELECTRON_BIN ||
  path.resolve(__dirname, '..', '..', 'out', 'release', 'electron', 'win-unpacked', 'klogg.exe');
const userData = fs.mkdtempSync(path.join(os.tmpdir(), 'klogg-electron-smoke-'));
const settingsFile = path.join(userData, 'desktop-settings.json');
const stateFile = path.join(userData, 'windows.json');

function cleanup() {
  fs.rmSync(userData, { recursive: true, force: true });
}

async function launch() {
  return electron.launch({
    executablePath: appExe,
    args: [],
    env: { ...process.env, KLOGG_DESKTOP_USER_DATA: userData },
    timeout: 60000,
  });
}

(async () => {
  fs.writeFileSync(
    settingsFile,
    JSON.stringify(
      {
        serverMode: true,
        listenAddress: '0.0.0.0',
        port: 8099,
        token: 'test-token-8099',
      },
      null,
      2,
    ),
  );
  fs.writeFileSync(stateFile, JSON.stringify({ windows: [] }, null, 2));

  let app = await launch();
  let page = await app.firstWindow({ timeout: 30000 });
  await page.waitForTimeout(1500);
  const localUrl = page.url();
  console.log('server-mode-url=' + localUrl);
  assert.match(localUrl, /window=w1/);
  assert.match(localUrl, /token=test-token-8099/);

  const pageInfo = await page.evaluate(async () => {
    const response = await fetch('/api/system.info', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        Authorization: 'Bearer test-token-8099',
      },
      body: '{}',
    });
    return { status: response.status, body: await response.json() };
  });
  assert.equal(pageInfo.status, 200);
  assert.equal(pageInfo.body.ok, true);

  const unauthenticated = await fetch('http://127.0.0.1:8099/api/system.info', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: '{}',
  });
  assert.equal(unauthenticated.status, 401);
  const authenticated = await fetch('http://127.0.0.1:8099/api/system.info', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', Authorization: 'Bearer test-token-8099' },
    body: '{}',
  });
  assert.equal(authenticated.status, 200);
  assert.equal(fs.readFileSync(path.join(userData, 'server.log'), 'utf8').includes('test-token'), false);

  const secondPromise = app.waitForEvent('window', { timeout: 15000 });
  await page.evaluate(() => window.open(window.location.href, '_blank', 'noopener'));
  const second = await secondPromise;
  await second.waitForTimeout(1000);
  console.log('second-url=' + second.url());
  assert.match(second.url(), /window=w2/);
  assert.match(second.url(), /token=test-token-8099/);
  await page.evaluate(() => localStorage.setItem('klogg.web.smoke-shared', 'shared'));
  assert.equal(
    await second.evaluate(() => localStorage.getItem('klogg.web.smoke-shared')),
    'shared',
  );
  await page.evaluate(() => localStorage.removeItem('klogg.web.smoke-shared'));

  await app.evaluate(({ app: electronApp }) => electronApp.quit());
  await new Promise((resolve) => setTimeout(resolve, 3000));
  await app.close().catch(() => {});

  const state = JSON.parse(fs.readFileSync(stateFile, 'utf8'));
  console.log('window-count=' + state.windows.length);
  assert.equal(state.windows.length, 2);

  app = await launch();
  await app.firstWindow({ timeout: 30000 });
  await new Promise((resolve) => setTimeout(resolve, 1500));
  const restored = app.windows().map((window) => window.url()).sort();
  console.log('restored=' + restored.join(','));
  assert.equal(restored.length, 2);
  assert.match(restored[0], /window=w1/);
  assert.match(restored[1], /window=w2/);
  await app.evaluate(({ app: electronApp }) => electronApp.quit());
  await app.close().catch(() => {});

  cleanup();
  console.log('electron-server-mode-ok');
})().catch((error) => {
  cleanup();
  console.error(error);
  process.exit(1);
});
