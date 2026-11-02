const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { _electron: electron } = require('playwright');

const repoRoot = path.resolve(__dirname, '..', '..');
const appExe = path.join(repoRoot, 'out', 'release', 'electron', 'win-unpacked', 'klogg.exe');
const fixtures = path.join(repoRoot, 'src', 'shell', 'web', 'e2e', 'fixtures');
const testData = path.join(repoRoot, 'out', 'testdata');
const initialFile = path.join(fixtures, 'alpha.log');
const screenshot = path.join(repoRoot, 'out', 'tmp', 'pinned-tabs.png');
const userData = fs.mkdtempSync(path.join(os.tmpdir(), 'klogg-electron-pins-'));

async function openFile(page, directory, name) {
  await page.locator('.file-tab-new:not(.tab-finder-button)').click();
  await page.locator('.file-source-tabs button').filter({ hasText: '目录' }).click();
  await page.locator('.file-path').fill(directory);
  await page.locator('.file-path').press('Enter');
  await page.waitForTimeout(700);
  await page.locator('.file-row').filter({ hasText: name }).first().dblclick();
  await page.locator('.file-tab-title').filter({ hasText: name }).waitFor({ timeout: 20000 });
  await page.waitForTimeout(400);
}

async function tabOrder(page) {
  return page.locator('.file-tab').evaluateAll((tabs) =>
    tabs.map((tab) => ({
      title: tab.querySelector('.file-tab-title')?.textContent?.trim() ?? '',
      pinned: tab.classList.contains('pinned'),
      group: tab.closest('.tabbar-pinned') ? 'pinned' : 'scroll',
    })),
  );
}

(async () => {
  const app = await electron.launch({
    executablePath: appExe,
    args: [initialFile],
    env: { ...process.env, KLOGG_DESKTOP_USER_DATA: userData },
    timeout: 60000,
  });
  const page = await app.firstWindow({ timeout: 30000 });
  await page.locator('.file-tab-title').filter({ hasText: 'alpha.log' }).waitFor({ timeout: 30000 });
  assert.equal(await page.locator('.tab-finder-button, .tab-finder').count(), 0);

  await openFile(page, fixtures, 'beta.log');
  assert.equal(await page.locator('.file-tab').count(), 2);

  await page.locator('.file-tab').filter({ hasText: 'alpha.log' }).click({ button: 'right' });
  await page.locator('.context-item').filter({ hasText: /钉住标签页|Pin tab/ }).first().click();
  await page.waitForTimeout(500);

  let order = await tabOrder(page);
  console.log(`pinned=${JSON.stringify(order)}`);
  assert.equal(order.length, 2);
  assert.deepEqual(order[0], { title: 'alpha.log', pinned: true, group: 'pinned' });
  assert.deepEqual(order[1], { title: 'beta.log', pinned: false, group: 'scroll' });
  assert.equal(await page.locator('.tabbar-pinned .file-tab-pin').count(), 1);
  const pinStyle = await page.locator('.tabbar-pinned .file-tab-pin svg').evaluate((element) => ({
    width: element.getAttribute('width'),
    height: element.getAttribute('height'),
    fill: element.getAttribute('fill'),
    stroke: element.getAttribute('stroke'),
    strokeWidth: element.getAttribute('stroke-width'),
  }));
  console.log(`pin-icon=${JSON.stringify(pinStyle)}`);
  assert.deepEqual(pinStyle, {
    width: '14',
    height: '14',
    fill: 'none',
    stroke: 'currentColor',
    strokeWidth: '1.4',
  });
  await page.screenshot({ path: screenshot, fullPage: true });

  await openFile(page, testData, 'random_block_100byte.txt');
  order = await tabOrder(page);
  console.log(`after-third=${JSON.stringify(order)}`);
  assert.equal(order.length, 3);
  assert.equal(order[0].title, 'alpha.log');
  assert.equal(order[0].group, 'pinned');
  assert.equal(await page.locator('.tabbar-scroll .file-tab').count(), 2);

  await page.evaluate(() => {
    const url = new URL(window.location.href);
    url.searchParams.delete('file');
    window.history.replaceState(null, '', url.toString());
  });
  await page.reload();
  await page.locator('.file-tab').nth(2).waitFor({ timeout: 30000 });
  await page.waitForTimeout(800);
  order = await tabOrder(page);
  console.log(`after-reload=${JSON.stringify(order)}`);
  assert.equal(order.length, 3);
  assert.equal(order[0].title, 'alpha.log');
  assert.equal(order[0].pinned, true);
  assert.equal(order[0].group, 'pinned');
  assert.equal(await page.locator('.tabbar-pinned .file-tab').count(), 1);

  await page.locator('.file-tab').filter({ hasText: 'alpha.log' }).click({ button: 'right' });
  await page.locator('.context-item').filter({ hasText: /取消钉住|Unpin tab/ }).first().click();
  await page.waitForTimeout(500);
  order = await tabOrder(page);
  console.log(`after-unpin=${JSON.stringify(order)}`);
  assert.equal(await page.locator('.tabbar-pinned .file-tab').count(), 0);
  assert.equal(order.length, 3);
  assert.equal(order[0].title, 'alpha.log');
  assert.equal(order[0].pinned, false);
  assert.equal(order[0].group, 'scroll');

  console.log(`screenshot=${screenshot}`);
  console.log('packaged-tab-pinning-ok');
  await app.evaluate(({ app: electronApp }) => electronApp.quit());
  await app.close().catch(() => {});
})().catch((error) => {
  console.error(error);
  process.exit(1);
});
