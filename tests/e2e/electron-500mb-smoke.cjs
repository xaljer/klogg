const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { _electron: electron } = require('playwright');

const repoRoot = path.resolve(__dirname, '..', '..');
const appExe = path.join(repoRoot, 'out', 'release', 'electron', 'win-unpacked', 'klogg.exe');
const logFile = path.join(repoRoot, 'out', 'testdata', 'random_block_500Mb.txt');
const screenshotPath = path.join(repoRoot, 'out', 'tmp', 'klogg-500mb.png');
const userData = fs.mkdtempSync(path.join(os.tmpdir(), 'klogg-electron-500mb-'));

(async () => {
  const app = await electron.launch({
    executablePath: appExe,
    args: [logFile],
    env: { ...process.env, KLOGG_DESKTOP_USER_DATA: userData },
    timeout: 60000,
  });
  const page = await app.firstWindow({ timeout: 30000 });
  const initialMenu = await app.evaluate(({ BrowserWindow, Menu }) => ({
    applicationMenu: Menu.getApplicationMenu() !== null,
    menuBarVisible: BrowserWindow.getAllWindows()[0].isMenuBarVisible(),
  }));
  console.log(`menu=${JSON.stringify(initialMenu)}`);
  assert.equal(initialMenu.applicationMenu, false);
  assert.equal(initialMenu.menuBarVisible, false);

  await page.keyboard.press('Alt');
  await page.waitForTimeout(300);
  const altMenu = await app.evaluate(({ BrowserWindow }) => ({
    menuBarVisible: BrowserWindow.getAllWindows()[0].isMenuBarVisible(),
  }));
  console.log(`alt-menu=${JSON.stringify(altMenu)}`);
  assert.equal(altMenu.menuBarVisible, false);

  await page.locator('.file-tab-title').filter({ hasText: 'random_block_500Mb.txt' }).waitFor({
    timeout: 30000,
  });
  await page.locator('.terminal-host').waitFor({ state: 'visible', timeout: 30000 });
  await page.waitForTimeout(5000);
  const rows = await page.locator('.log-row').count();
  console.log(`rows=${rows}`);
  await page.screenshot({ path: screenshotPath, fullPage: true });
  console.log(`screenshot=${screenshotPath}`);
  console.log('packaged-500mb-no-menu-ok');
  await app.evaluate(({ app: electronApp }) => electronApp.quit());
  await app.close().catch(() => {});
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
