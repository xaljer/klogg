const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { _electron: electron } = require('playwright');

const repoRoot = path.resolve(__dirname, '..', '..');
const appExe = path.join(repoRoot, 'out', 'release', 'electron', 'win-unpacked', 'klogg.exe');
const logFile = path.join(repoRoot, 'out', 'testdata', 'random_block_500Mb.txt');
const darkShot = path.join(repoRoot, 'out', 'tmp', 'theme-dark.png');
const lightShot = path.join(repoRoot, 'out', 'tmp', 'theme-light.png');
const userData = fs.mkdtempSync(path.join(os.tmpdir(), 'klogg-electron-theme-'));

(async () => {
  const app = await electron.launch({
    executablePath: appExe,
    args: [logFile],
    env: { ...process.env, KLOGG_DESKTOP_USER_DATA: userData },
    timeout: 60000,
  });
  const page = await app.firstWindow({ timeout: 30000 });
  await page.locator('.file-tab-title').filter({ hasText: 'random_block_500Mb.txt' }).waitFor({
    timeout: 30000,
  });
  await page.waitForTimeout(2000);

  const themeBox = await page.locator('.theme-toggle').boundingBox();
  const settingsBox = await page.locator('.settings-button:not(.theme-toggle)').boundingBox();
  assert.ok(themeBox && settingsBox, 'theme and settings buttons must exist');
  assert.ok(
    themeBox.x + themeBox.width <= settingsBox.x + 1,
    `theme button must be left of settings (${themeBox.x + themeBox.width} > ${settingsBox.x})`,
  );

  const initial = await page.evaluate(() => ({
    theme: document.documentElement.dataset.theme,
    stored: JSON.parse(localStorage.getItem('klogg.web.settings') || '{}').theme,
    select: (() => {
      const select = document.querySelector('.encoding-select');
      const option = select?.querySelector('option');
      return {
        colorScheme: select ? getComputedStyle(select).colorScheme : '',
        optionBackground: option ? getComputedStyle(option).backgroundColor : '',
        optionColor: option ? getComputedStyle(option).color : '',
      };
    })(),
  }));
  console.log(`initial=${JSON.stringify(initial)}`);
  assert.equal(initial.theme, 'mocha');
  assert.equal(initial.select.colorScheme, 'dark');
  assert.equal(initial.select.optionBackground, 'rgb(49, 50, 68)');
  await page.screenshot({ path: darkShot, fullPage: true });

  await page.locator('.theme-toggle').click();
  await page.waitForTimeout(800);
  const light = await page.evaluate(() => ({
    theme: document.documentElement.dataset.theme,
    colorScheme: document.documentElement.style.colorScheme,
    stored: JSON.parse(localStorage.getItem('klogg.web.settings') || '{}').theme,
    bg: getComputedStyle(document.documentElement).getPropertyValue('--bg').trim(),
    select: (() => {
      const select = document.querySelector('.encoding-select');
      const option = select?.querySelector('option');
      return {
        colorScheme: select ? getComputedStyle(select).colorScheme : '',
        optionBackground: option ? getComputedStyle(option).backgroundColor : '',
        optionColor: option ? getComputedStyle(option).color : '',
      };
    })(),
  }));
  console.log(`light=${JSON.stringify(light)}`);
  assert.equal(light.theme, 'latte');
  assert.equal(light.colorScheme, 'light');
  assert.equal(light.stored, 'latte');
  assert.equal(light.bg, '#eff1f5');
  assert.equal(light.select.colorScheme, 'light');
  assert.equal(light.select.optionBackground, 'rgb(230, 233, 239)');
  assert.equal(light.select.optionColor, 'rgb(76, 79, 105)');
  await page.screenshot({ path: lightShot, fullPage: true });

  await page.locator('.theme-toggle').click();
  await page.waitForTimeout(800);
  const back = await page.evaluate(() => document.documentElement.dataset.theme);
  console.log(`back=${back}`);
  assert.equal(back, 'mocha');

  console.log(`dark=${darkShot}`);
  console.log(`light=${lightShot}`);
  console.log('packaged-theme-toggle-ok');
  await app.evaluate(({ app: electronApp }) => electronApp.quit());
  await app.close().catch(() => {});
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
