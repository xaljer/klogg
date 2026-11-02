const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { _electron: electron } = require('playwright');

const repoRoot = path.resolve(__dirname, '..', '..');
const appExe = path.join(repoRoot, 'out', 'release', 'electron', 'win-unpacked', 'klogg.exe');
const logFile = path.join(repoRoot, 'src', 'shell', 'web', 'e2e', 'fixtures', 'alpha.log');
const userData = fs.mkdtempSync(path.join(os.tmpdir(), 'klogg-electron-search-menu-'));

async function closeMenu(page) {
  await page.keyboard.press('Escape');
  await page.locator('.context-menu').waitFor({ state: 'detached' });
}

async function assertSearchItems(page, expectedDisabled, ids) {
  await page.locator('.context-menu').waitFor({ timeout: 5000 });
  for (const id of ids) {
    const item = page.locator(`[data-menu-id="${id}"]`);
    assert.equal(await item.count(), 1, `missing menu item ${id}`);
    assert.equal(await item.isDisabled(), expectedDisabled, `${id} disabled state`);
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
  await page.locator('.file-tab-title').filter({ hasText: 'alpha.log' }).waitFor({ timeout: 30000 });
  await page.locator('.terminal-host').waitFor({ state: 'visible', timeout: 30000 });
  await page.locator('.search-input').fill('INFO');
  await page.locator('.search-input').press('Enter');
  await page.locator('.filtered-row').first().waitFor({ timeout: 15000 });
  await page.waitForTimeout(1200);

  const filteredRows = page.locator('.filtered-row');
  const first = await filteredRows.nth(0).boundingBox();
  const fifth = await filteredRows.nth(4).boundingBox();
  assert.ok(first && fifth);
  await page.mouse.move(first.x + 90, first.y + first.height / 2);
  await page.mouse.down();
  await page.mouse.move(fifth.x + 90, fifth.y + fifth.height / 2, { steps: 10 });
  await page.mouse.up();
  await page.waitForTimeout(300);

  await filteredRows.nth(0).click({ button: 'right' });
  await assertSearchItems(page, true, [
    'filtered-replace',
    'filtered-add',
    'filtered-exclude',
  ]);
  console.log('filtered-row-selection-search-items=disabled');
  await closeMenu(page);

  await filteredRows.nth(1).dblclick({ position: { x: 220, y: 10 } });
  await page.waitForTimeout(500);
  await filteredRows.nth(1).click({ button: 'right' });
  await assertSearchItems(page, false, [
    'filtered-replace',
    'filtered-add',
    'filtered-exclude',
  ]);
  console.log('filtered-word-selection-search-items=enabled');
  await closeMenu(page);

  const host = page.locator('.terminal-host');
  const hostBox = await host.boundingBox();
  assert.ok(hostBox);

  await page.mouse.click(hostBox.x + 320, hostBox.y + 110, { button: 'right' });
  await assertSearchItems(page, true, ['replace-search', 'add-search', 'exclude-search']);
  console.log('main-line-selection-search-items=disabled');
  await closeMenu(page);

  await page.mouse.dblclick(hostBox.x + 290, hostBox.y + 20);
  await page.waitForTimeout(500);
  await page.mouse.click(hostBox.x + 290, hostBox.y + 20, { button: 'right' });
  await assertSearchItems(page, false, ['replace-search', 'add-search', 'exclude-search']);
  console.log('main-word-selection-search-items=enabled');
  await closeMenu(page);

  console.log('packaged-search-selection-menu-ok');
  await app.evaluate(({ app: electronApp }) => electronApp.quit());
  await app.close().catch(() => {});
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
