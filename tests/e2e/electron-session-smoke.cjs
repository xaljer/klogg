const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { _electron: electron } = require('playwright');

const appExe = process.env.KLOGG_ELECTRON_BIN || path.resolve(__dirname, '..', '..', 'out', 'release', 'electron', 'win-unpacked', 'klogg.exe');
const logFile = path.resolve(__dirname, '..', '..', 'out', 'testdata', 'recorder_demo.log');
const userData = fs.mkdtempSync(path.join(os.tmpdir(), 'klogg-electron-session-'));
const env = { ...process.env, KLOGG_DESKTOP_USER_DATA: userData };

function cleanup() {
  try {
    fs.rmSync(userData, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
  } catch {
    // Chromium may still hold a cache file after the process exits.
  }
}

async function launch(args = []) {
  return electron.launch({ executablePath: appExe, args, env, timeout: 60000 });
}

(async () => {
  let app = await launch([logFile]);
  let page = await app.firstWindow({ timeout: 30000 });
  await page.locator('.file-tab-title').filter({ hasText: 'recorder_demo.log' }).waitFor({ timeout: 30000 });
  await page.waitForTimeout(2500);
  console.log('first-url=' + page.url());
  await app.evaluate(({ app: electronApp }) => electronApp.quit());
  await new Promise((resolve) => setTimeout(resolve, 3000));
  await app.close().catch(() => {});

  app = await launch([]);
  page = await app.firstWindow({ timeout: 30000 });
  await page.waitForTimeout(1500);
  console.log('second-url=' + page.url());
  await page.locator('.file-tab-title').filter({ hasText: 'recorder_demo.log' }).waitFor({ timeout: 30000 });
  assert.match(page.url(), /window=w1/);
  console.log('electron-session-restore-ok');
  await app.evaluate(({ app: electronApp }) => electronApp.quit());
  await app.close().catch(() => {});
  cleanup();
})().catch((error) => {
  cleanup();
  console.error(error);
  process.exit(1);
});
