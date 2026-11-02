const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { _electron: electron } = require('playwright');

const appExe = process.env.KLOGG_ELECTRON_BIN || path.resolve(__dirname, '..', '..', 'out', 'release', 'electron', 'win-unpacked', 'klogg.exe');
const logFile =
  process.env.KLOGG_UI_TEST_FILE ||
  path.resolve(__dirname, '..', '..', 'out', 'testdata', 'recorder_demo.log');
const screenshotPath = process.env.KLOGG_SCREENSHOT || '';
const userData = fs.mkdtempSync(path.join(os.tmpdir(), 'klogg-electron-ui-'));

function cleanup() {
  try {
    fs.rmSync(userData, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
  } catch {
    // Chromium may still hold a cache file after the process exits.
  }
}

(async () => {
  const app = await electron.launch({
    executablePath: appExe,
    args: [logFile],
    env: { ...process.env, KLOGG_DESKTOP_USER_DATA: userData },
    timeout: 60000,
  });
  const page = await app.firstWindow({ timeout: 30000 });
  await page.waitForTimeout(1500);
  assert.equal(page.url().includes('token='), false);
  await page.locator('.file-tab-title').filter({ hasText: path.basename(logFile) }).waitFor({ timeout: 15000 });
  await page.locator('.terminal-host').waitFor({ state: 'visible', timeout: 15000 });
  await page.locator('.search-input').fill('INFO');
  await page.locator('.search-input').press('Enter');
  await page.locator('.filtered-row').first().waitFor({ timeout: 15000 });
  assert.ok((await page.locator('.filtered-row').count()) > 0);
  if (screenshotPath) {
    await page.screenshot({ path: screenshotPath, fullPage: true });
    console.log(`screenshot=${screenshotPath}`);
  }
  console.log('packaged-ui-search-ok');
  await app.evaluate(({ app: electronApp }) => electronApp.quit());
  await app.close().catch(() => {});
  cleanup();
})().catch((error) => {
  cleanup();
  console.error(error);
  process.exit(1);
});
