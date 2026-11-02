const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { _electron: electron } = require('playwright');

const appExe =
  process.env.KLOGG_ELECTRON_BIN ||
  path.resolve(__dirname, '..', '..', 'out', 'release', 'electron', 'win-unpacked', 'klogg.exe');
const initialFile =
  process.env.KLOGG_BROWSE_NO_FILE === '1'
    ? ''
    : path.resolve(__dirname, '..', '..', 'src', 'shell', 'web', 'e2e', 'fixtures', 'alpha.log');
const directory = path.resolve(__dirname, '..', '..', 'src', 'shell', 'web', 'e2e', 'fixtures');
const screenshot = path.resolve(__dirname, '..', '..', 'out', 'tmp', 'klogg-browser.png');
const userData = fs.mkdtempSync(path.join(os.tmpdir(), 'klogg-electron-browser-'));

function cleanup() {
  try {
    fs.rmSync(userData, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
  } catch {}
}

(async () => {
  const app = await electron.launch({
    executablePath: appExe,
    args: initialFile ? [initialFile] : [],
    env: { ...process.env, KLOGG_DESKTOP_USER_DATA: userData },
    timeout: 60000,
  });
  const page = await app.firstWindow({ timeout: 30000 });
  if (initialFile) {
    await page.locator('.file-tab-title').filter({ hasText: 'alpha.log' }).waitFor({ timeout: 30000 });
    await page.locator('.file-tab-new').first().click();
  } else {
    await page.locator('.welcome .primary').click();
  }
  await page.locator('.file-source-tabs button').filter({ hasText: '目录' }).click();
  if (initialFile) {
    await page.locator('.file-path').fill(directory);
    await page.locator('.file-path').press('Enter');
  }
  await page.waitForTimeout(1000);
  await page.locator('.file-path').fill(directory);
  await page.locator('.file-path').press('Enter');
  await page.locator('.file-row').filter({ hasText: 'alpha.log' }).first().dblclick();
  await page.locator('.file-tab-title').filter({ hasText: 'alpha.log' }).waitFor({ timeout: 15000 });
  console.log('opened-alpha=true');
  await page.waitForTimeout(1000);
  console.log('rows=' + (await page.locator('.file-row').count()));
  console.log('empty=' + (await page.locator('.file-empty').allTextContents()).join('|'));
  console.log('path=' + (await page.locator('.file-path').inputValue().catch(() => '')));
  if (await page.locator('.file-dialog').count()) {
    console.log((await page.locator('.file-dialog').innerText()).slice(0, 1500));
  }
  await page.screenshot({ path: screenshot, fullPage: true });
  console.log('screenshot=' + screenshot);
  await app.evaluate(({ app: electronApp }) => electronApp.quit());
  await app.close().catch(() => {});
  cleanup();
})().catch((error) => {
  cleanup();
  console.error(error);
  process.exit(1);
});
