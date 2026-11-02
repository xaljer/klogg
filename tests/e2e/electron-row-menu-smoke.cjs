const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { _electron: electron } = require('playwright');

const repoRoot = path.resolve(__dirname, '..', '..');
const appExe = path.join(repoRoot, 'out', 'release', 'electron', 'win-unpacked', 'klogg.exe');
const logFile = path.join(repoRoot, 'src', 'shell', 'web', 'e2e', 'fixtures', 'alpha.log');
const selectionShot = path.join(repoRoot, 'out', 'tmp', 'filtered-row-selection.png');
const menuShot = path.join(repoRoot, 'out', 'tmp', 'context-color-grid.png');
const userData = fs.mkdtempSync(path.join(os.tmpdir(), 'klogg-electron-menu-'));

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
  await page.waitForTimeout(1500);

  const rows = page.locator('.filtered-row');
  const rowCount = await rows.count();
  console.log(`filtered-rows=${rowCount}`);
  assert.ok(rowCount >= 5, `need at least 5 filtered rows, got ${rowCount}`);

  const first = await rows.nth(0).boundingBox();
  const fifth = await rows.nth(4).boundingBox();
  assert.ok(first && fifth);
  await page.mouse.move(first.x + 90, first.y + first.height / 2);
  await page.mouse.down();
  await page.mouse.move(fifth.x + 90, fifth.y + fifth.height / 2, { steps: 12 });
  await page.mouse.up();
  await page.waitForTimeout(400);

  const selectedCount = await page.locator('.filtered-row.selected').count();
  const nativeSelection = await page.evaluate(() => window.getSelection()?.toString() ?? '');
  console.log(`selected-rows=${selectedCount}`);
  console.log(`native-selection=${JSON.stringify(nativeSelection.slice(0, 80))}`);
  assert.ok(selectedCount >= 5, `expected whole-row selection, got ${selectedCount} selected rows`);
  assert.equal(nativeSelection, '', 'drag should not create a column-based native selection');
  await page.screenshot({ path: selectionShot, fullPage: true });

  await rows.nth(1).dblclick({ position: { x: 220, y: 10 } });
  await page.waitForTimeout(600);
  const wordHits = await page.locator('.filtered-row .word-hit').count();
  const wordSelection = await page.evaluate(() => window.getSelection()?.toString() ?? '');
  console.log(`word-hits=${wordHits}`);
  console.log(`word-selection=${JSON.stringify(wordSelection)}`);
  assert.ok(wordHits > 0, 'double-click word highlight should still work');

  const host = page.locator('.terminal-host');
  const hostBox = await host.boundingBox();
  assert.ok(hostBox);
  await page.mouse.click(hostBox.x + 320, hostBox.y + 110, { button: 'right' });
  await page.locator('.context-menu').waitFor({ timeout: 5000 });
  const menu = page.locator('.context-menu');
  const menuText = await menu.innerText();
  const gridCells = await page.locator('.context-grid-cell').count();
  const submenus = await page.locator('.context-menu .context-submenu').count();
  console.log(`grid-cells=${gridCells}`);
  console.log(`submenus=${submenus}`);
  console.log(menuText.replace(/\s+/g, ' | ').slice(0, 500));
  assert.equal(gridCells, 12, 'color grid should contain none, ten labels and clear');
  assert.equal(submenus, 0, 'color labels should expand inline without a submenu');
  assert.ok(!menuText.includes('高亮选中文本'), 'highlight selection item must be removed');
  assert.ok(!menuText.includes('高亮规则设置'), 'highlight settings item must be removed');

  const cellTexts = (await page.locator('.context-grid-cell').allTextContents()).map((text) =>
    text.trim(),
  );
  console.log(`grid-texts=${JSON.stringify(cellTexts)}`);
  assert.ok(cellTexts.includes('1') && cellTexts.includes('10'));
  const rowTops = await page
    .locator('.context-grid-cell')
    .evaluateAll((elements) =>
      [...new Set(elements.map((element) => Math.round(element.getBoundingClientRect().top)))],
    );
  console.log(`grid-rows=${rowTops.length}`);
  assert.equal(rowTops.length, 2, 'color grid should fit into two rows');
  const labelTwo = page.locator('.context-grid-cell').filter({ hasText: /^\s*2\s*$/ });
  const chipStyle = await labelTwo.locator('.context-grid-chip').evaluate((element) => ({
    background: getComputedStyle(element).backgroundColor,
    color: getComputedStyle(element).color,
    text: element.textContent?.trim() ?? '',
  }));
  console.log(`label-2-chip=${JSON.stringify(chipStyle)}`);
  assert.equal(chipStyle.text, '2');
  assert.equal(chipStyle.background, 'rgb(255, 161, 198)');
  assert.equal(chipStyle.color, 'rgb(128, 0, 93)');
  await page.screenshot({ path: menuShot, fullPage: true });

  await labelTwo.click();
  await page.waitForTimeout(500);
  assert.equal(await page.locator('.context-menu').count(), 0);
  const statusText = (await page.locator('.toolbar-message').allTextContents()).join(' ');
  console.log(`label-status=${JSON.stringify(statusText)}`);
  assert.ok(statusText.includes('2'), 'clicking color label 2 should apply the label');

  console.log(`selection-shot=${selectionShot}`);
  console.log(`menu-shot=${menuShot}`);
  console.log('packaged-row-selection-and-menu-ok');
  await app.evaluate(({ app: electronApp }) => electronApp.quit());
  await app.close().catch(() => {});
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
