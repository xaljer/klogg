// Verifies the selection time delta badge in the real Electron window: it must
// show the interval of a multi-line selection, sit in the bottom-right corner,
// ignore pointer events and disappear once the selection is gone.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { _electron: electron } = require('playwright');

const repoRoot = path.resolve(__dirname, '..', '..');
// Point this at desktop/dist/win-unpacked/klogg.exe to verify a packaged build;
// the default launches the development Electron with src/web/dist.
const packagedExe = process.env.KLOGG_ELECTRON_BIN || '';
const electronBin = path.resolve(repoRoot, 'src', 'shell', 'electron', 'node_modules', 'electron', 'dist', 'electron.exe');
const appDir = path.resolve(repoRoot, 'src', 'shell', 'electron');
const logFile = path.resolve(repoRoot, 'src', 'shell', 'web', 'e2e', 'fixtures', 'alpha.log');
const screenshotPath =
  process.env.KLOGG_SCREENSHOT || path.resolve(repoRoot, 'out', 'tmp', 'selection-time.png');
// Keep the profile inside the repository: platform temp areas can reject
// Chromium's cache and singleton files depending on the sandbox token.
const userData = path.resolve(repoRoot, 'out', 'tmp', 'smoke-userdata');
fs.rmSync(userData, { recursive: true, force: true });
fs.mkdirSync(userData, { recursive: true });
const plainLog = path.join(userData, 'no-timestamps.log');

fs.writeFileSync(plainLog, ['alpha start', 'beta middle', 'gamma end'].join('\n'));

function cleanup() {
  try {
    fs.rmSync(userData, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 });
  } catch {
    // Chromium may still hold a cache file after the process exits.
  }
}

async function dragSelect(page, fromPoint, toPoint) {
  await page.mouse.move(fromPoint.x, fromPoint.y);
  await page.mouse.down();
  await page.mouse.move(toPoint.x, toPoint.y, { steps: 10 });
  await page.mouse.up();
  await page.waitForTimeout(350);
}

// Row positions come from the rendered row elements, so a font or DPI change
// cannot shift the drag onto a neighbouring line.
async function rowGeometry(page) {
  const rows = page.locator('.pane-slot.active .xterm-rows > div');
  const count = await rows.count();
  assert.ok(count > 4, `need a few terminal rows, got ${count}`);
  return {
    count,
    point: async (index) => {
      const box = await rows.nth(index).boundingBox();
      assert.ok(box, `terminal row ${index} must be laid out`);
      return { x: box.x + 120, y: box.y + box.height / 2 };
    },
  };
}

function badgeOf(page) {
  return page.locator('.pane-slot.active .selection-time');
}

async function readBadge(page) {
  const badge = badgeOf(page);
  try {
    await badge.waitFor({ timeout: 5000 });
  } catch (error) {
    const state = await page.evaluate(() => ({
      selection: window.__kloggViewport?.selectionInfo?.(),
      toolbar: document.querySelector('.toolbar-info.selection')?.textContent ?? '',
      badges: document.querySelectorAll('.selection-time').length,
    }));
    const diagnostic = path.resolve(repoRoot, 'out', 'tmp', 'selection-time-missing.png');
    await page.screenshot({ path: diagnostic, fullPage: true }).catch(() => {});
    console.error(`badge missing: ${JSON.stringify(state)} shot=${diagnostic}`);
    throw error;
  }
  const [text, value, unit, label] = await Promise.all([
    badge.innerText(),
    badge.getAttribute('data-value'),
    badge.getAttribute('data-unit'),
    badge.getAttribute('data-label'),
  ]);
  return { text: text.replace(/\s+/g, ' ').trim(), value, unit, label };
}

async function selectTerminalRows(page, first, last) {
  // A drag started while the frame is still loading lands on stale rows; wait for
  // the viewport to settle before selecting.
  await page
    .locator('.pane-slot.active .viewport-loading')
    .waitFor({ state: 'detached', timeout: 5000 })
    .catch(() => {});
  const geometry = await rowGeometry(page);
  await dragSelect(page, await geometry.point(first), await geometry.point(last));
}

// "1 m 4 s" / "66 s" / "1 h 0 m 5.5 s" -> seconds, so assertions survive the
// unit the badge happens to pick.
function durationSeconds(label) {
  const units = { h: 3600, m: 60, s: 1, ms: 0.001, '\u00b5s': 0.000001 };
  let total = 0;
  for (const match of label.matchAll(/([\d.]+)\s*(h|ms|s|m|\u00b5s)/g)) {
    total += Number(match[1]) * (units[match[2]] ?? 0);
  }
  return total;
}

async function filteredRowPoint(page, index) {
  const box = await page.locator('.filtered-row').nth(index).boundingBox();
  assert.ok(box, `filtered row ${index} must be laid out`);
  return { x: box.x + 90, y: box.y + box.height / 2 };
}

(async () => {
  const env = { ...process.env, KLOGG_DESKTOP_USER_DATA: userData };
  // Inheriting this from an editor or a test harness turns Electron into plain
  // node and the window never appears.
  delete env.ELECTRON_RUN_AS_NODE;
  const app = await electron.launch({
    executablePath: packagedExe || electronBin,
    // Chromium's sandbox cannot initialize when the app is started from this
    // harness's job object; the flag only affects the automated run.
    args: packagedExe
      ? ['--no-sandbox', logFile]
      : ['--no-sandbox', appDir, logFile],
    env,
    timeout: 60000,
  });
  const page = await app.firstWindow({ timeout: 30000 });
  const consoleProblems = [];
  // Electron itself warns about the dev-mode CSP; that warning is expected and
  // disappears once the app is packaged.
  const ignoredConsole = /Electron Security Warning/;
  page.on('console', (message) => {
    if ((message.type() === 'error' || message.type() === 'warning') && !ignoredConsole.test(message.text())) {
      consoleProblems.push(`${message.type()}: ${message.text()}`);
    }
  });
  page.on('pageerror', (error) => consoleProblems.push(`pageerror: ${error.message}`));
  await page.locator('.file-tab-title').filter({ hasText: 'alpha.log' }).waitFor({ timeout: 30000 });
  await page.locator('.terminal-host').first().waitFor({ state: 'visible', timeout: 30000 });
  await page.waitForTimeout(1500);

  // A three line selection: alpha.log puts one line per second.
  await selectTerminalRows(page, 0, 2);
  const mainBadge = await readBadge(page);
  console.log(`main-badge=${JSON.stringify(mainBadge.text)} value=${mainBadge.value} unit=${mainBadge.unit}`);
  assert.equal(mainBadge.unit, 's');
  assert.equal(mainBadge.value, '2');
  assert.equal(mainBadge.text, 'Δ 2 s');

  const windowSize = await page.evaluate(() => ({
    width: window.innerWidth,
    height: window.innerHeight,
  }));
  const badgeBox = await badgeOf(page).boundingBox();
  const pointerEvents = await page.evaluate(
    () => getComputedStyle(document.querySelector('.selection-time')).pointerEvents,
  );
  console.log(`badge-box=${JSON.stringify(badgeBox)} window=${JSON.stringify(windowSize)} pe=${pointerEvents}`);
  assert.equal(pointerEvents, 'none', 'the badge must not swallow pointer events');
  assert.ok(windowSize.width - (badgeBox.x + badgeBox.width) <= 40, 'badge near the right edge');
  assert.ok(windowSize.height - (badgeBox.y + badgeBox.height) <= 40, 'badge near the bottom edge');
  if (screenshotPath) {
    await page.screenshot({ path: screenshotPath, fullPage: true });
    console.log(`screenshot=${screenshotPath}`);
  }

  // A single line selection has no interval.
  const singlePoint = await (await rowGeometry(page)).point(1);
  await page.mouse.click(singlePoint.x, singlePoint.y);
  await page.waitForTimeout(350);
  assert.equal(await badgeOf(page).count(), 0, 'a single line must hide the badge');

  // The filtered view reports its own row selection.
  await page.locator('.search-input').fill('INFO');
  await page.locator('.search-input').press('Enter');
  await page.locator('.filtered-row').first().waitFor({ timeout: 15000 });
  await page.waitForTimeout(800);
  const infoCount = await page.locator('.filtered-row').count();
  assert.ok(infoCount >= 3, `need at least three INFO rows, got ${infoCount}`);
  await dragSelect(page, await filteredRowPoint(page, 0), await filteredRowPoint(page, 2));
  const infoBadge = await readBadge(page);
  console.log(`filtered-info-badge=${JSON.stringify(infoBadge.text)} value=${infoBadge.value}`);
  assert.equal(infoBadge.value, '4', 'the first three INFO lines span four seconds');

  // Re-running the search reloads the filtered rows and drops their selection.
  await page.locator('.search-input').fill('WARN');
  await page.locator('.search-input').press('Enter');
  await page.waitForTimeout(1200);
  assert.equal(await badgeOf(page).count(), 0, 'a reloaded filtered view must hide the badge');

  const warnCount = await page.locator('.filtered-row').count();
  assert.ok(warnCount >= 2, `need at least two WARN rows, got ${warnCount}`);
  await dragSelect(page, await filteredRowPoint(page, 0), await filteredRowPoint(page, 1));
  const warnBadge = await readBadge(page);
  console.log(`filtered-warn-badge=${JSON.stringify(warnBadge.text)} value=${warnBadge.value}`);
  assert.equal(warnBadge.value, '5', 'the WARN lines are five seconds apart');

  // The filtered pane must colour rows with the active highlight rules, using the
  // same rule order as the desktop engine (the default set colours WARN lines).
  const ruleStyle = await page.evaluate(() => {
    const span = document.querySelector('.pane-slot.active .filtered-text [style*="color"]');
    return span ? span.getAttribute('style') : '';
  });
  console.log(`filtered-rule-style=${ruleStyle}`);
  assert.ok(/f9e2af/i.test(ruleStyle), 'the WARN rule colour must reach the filtered view');

  // A single filtered row clears it again.
  const singleRow = await filteredRowPoint(page, 0);
  await page.mouse.click(singleRow.x, singleRow.y);
  await page.waitForTimeout(350);
  assert.equal(await badgeOf(page).count(), 0, 'a single filtered row must hide the badge');

  // Lines without timestamps never show a badge.
  await page.evaluate((target) => window.__klogg.openPath(target), plainLog);
  await page
    .locator('.file-tab-title')
    .filter({ hasText: 'no-timestamps.log' })
    .waitFor({ timeout: 15000 });
  await page.waitForTimeout(1500);
  await selectTerminalRows(page, 0, 2);
  assert.equal(await badgeOf(page).count(), 0, 'lines without timestamps must not show a badge');
  console.log('main-badgeless-ok');

  // Scrolling must carry the highlight with the selected log lines instead of
  // leaving it at the old screen rows, and the badge must keep the interval of
  // the anchored range while only part of it is visible.
  const scrollLog = path.join(userData, 'scroll-timestamps.log');
  const scrollStart = Date.UTC(2026, 9, 25, 10, 0, 0);
  fs.writeFileSync(
    scrollLog,
    Array.from(
      { length: 120 },
      (_, index) =>
        `${new Date(scrollStart + index * 1000).toISOString().slice(0, 19).replace('T', ' ')} INFO line ${index}`,
    ).join('\n'),
  );
  await page.evaluate((target) => window.__klogg.openPath(target), scrollLog);
  await page
    .locator('.file-tab-title')
    .filter({ hasText: 'scroll-timestamps.log' })
    .waitFor({ timeout: 15000 });
  await page.waitForTimeout(1500);

  const selectionState = async () =>
    page.evaluate(() => {
      const info = window.__kloggViewport?.selectionInfo?.();
      return info && info.hasSelection ? { startLine: info.startLine, endLine: info.endLine } : null;
    });

  await selectTerminalRows(page, 0, 5);
  assert.equal((await readBadge(page)).value, '5', 'six one-second lines span five seconds');
  assert.deepEqual(await selectionState(), { startLine: 0, endLine: 5 });

  const viewportBox = await page.locator('.pane-slot.active .viewport').boundingBox();
  assert.ok(viewportBox, 'the log viewport must be laid out');
  await page.mouse.move(viewportBox.x + 300, viewportBox.y + viewportBox.height / 2);

  await page.mouse.wheel(0, 120);
  await page.waitForTimeout(1000);
  assert.deepEqual(
    await selectionState(),
    { startLine: 3, endLine: 5 },
    'the highlight must move with the lines, not stay at the old screen rows',
  );
  const partialBadge = await readBadge(page);
  console.log(`partial-visible-badge=${JSON.stringify(partialBadge.text)}`);
  assert.equal(partialBadge.value, '5', 'the badge keeps the anchored interval');
  if (screenshotPath) {
    const scrolledShot = path.resolve(repoRoot, 'out', 'tmp', 'selection-scrolled.png');
    await page.screenshot({ path: scrolledShot, fullPage: true });
    console.log(`screenshot=${scrolledShot}`);
  }

  for (let step = 0; step < 3; step++) {
    await page.mouse.wheel(0, 120);
    await page.waitForTimeout(250);
  }
  await page.waitForTimeout(1400);
  assert.equal(await selectionState(), null, 'a fully scrolled-out range has no highlight');
  assert.equal(await badgeOf(page).count(), 0, 'and no badge');

  for (let step = 0; step < 4; step++) {
    await page.mouse.wheel(0, -120);
    await page.waitForTimeout(250);
  }
  await page.waitForTimeout(1400);
  assert.deepEqual(
    await selectionState(),
    { startLine: 0, endLine: 5 },
    'scrolling back restores the anchored range',
  );
  assert.equal((await readBadge(page)).value, '5', 'and its interval');
  console.log('scroll-anchor-ok');

  // A selection that grows past the viewport keeps the interval of the whole
  // range: the off-screen endpoint has to be remembered, not re-read from the
  // visible rows.
  const restartPoint = await (await rowGeometry(page)).point(0);
  await page.mouse.click(restartPoint.x, restartPoint.y);
  await page.waitForTimeout(300);
  await selectTerminalRows(page, 0, 2);
  await page.waitForTimeout(400);
  assert.equal((await readBadge(page)).value, '2');

  await page.mouse.move(viewportBox.x + 300, viewportBox.y + viewportBox.height / 2);
  await page.mouse.wheel(0, 120);
  await page.waitForTimeout(1000);
  assert.equal(await selectionState(), null, 'the anchor is off screen now');

  const extendPoint = await (await rowGeometry(page)).point(5);
  await page.keyboard.down('Shift');
  await page.mouse.click(extendPoint.x, extendPoint.y);
  await page.keyboard.up('Shift');
  await page.waitForTimeout(700);
  assert.deepEqual(
    await selectionState(),
    { startLine: 3, endLine: 8 },
    'only the visible part of the extended range is highlighted',
  );
  const extendedBadge = await readBadge(page);
  console.log(`extended-selection-badge=${JSON.stringify(extendedBadge.text)}`);
  assert.equal(
    extendedBadge.value,
    '8',
    'the interval covers the off-screen start line of the selection',
  );

  await page.mouse.wheel(0, -120);
  await page.waitForTimeout(1400);
  assert.deepEqual(await selectionState(), { startLine: 0, endLine: 8 });
  assert.equal((await readBadge(page)).value, '8');
  console.log('extended-selection-ok');

  // Dragging onto the bottom edge keeps scrolling the viewport and grows the
  // selection past the rows that were visible when the drag started.
  const firstVisibleLine = () =>
    page.evaluate(() => window.__kloggViewport?.rowMap?.[0]?.line?.lineNumber ?? -1);
  const autoScrollRestart = await (await rowGeometry(page)).point(0);
  await page.mouse.click(autoScrollRestart.x, autoScrollRestart.y);
  await page.waitForTimeout(300);
  const dragStart = await (await rowGeometry(page)).point(2);
  const hostBox = await page.locator('.pane-slot.active .terminal-host').boundingBox();
  assert.ok(hostBox);
  await page.mouse.move(dragStart.x, dragStart.y);
  await page.mouse.down();
  await page.mouse.move(hostBox.x + 300, hostBox.y + hostBox.height - 4, { steps: 8 });
  await page.waitForTimeout(1800);
  const autoScrolled = await selectionState();
  const autoFirstLine = await firstVisibleLine();
  console.log(`autoscroll-selection=${JSON.stringify(autoScrolled)} firstVisible=${autoFirstLine}`);
  assert.ok(autoFirstLine > 10, 'dragging at the bottom edge must scroll the viewport');
  assert.ok(
    autoScrolled && autoScrolled.endLine >= autoFirstLine,
    'the selection must reach the row the scroll revealed',
  );
  const autoBadge = await readBadge(page);
  console.log(`autoscroll-badge=${JSON.stringify(autoBadge.text)} label=${autoBadge.label}`);
  const autoShot = path.resolve(repoRoot, 'out', 'tmp', 'selection-autoscroll.png');
  await page.screenshot({ path: autoShot, fullPage: true });
  console.log(`screenshot=${autoShot}`);
  await page.mouse.up();
  await page.waitForTimeout(500);
  const settled = await selectionState();
  await page.waitForTimeout(800);
  assert.deepEqual(await selectionState(), settled, 'releasing the pointer must stop the scrolling');
  console.log('autoscroll-main-ok');

  // The filtered view scrolls its own list the same way, and its selection has
  // to survive the window reloads that scrolling triggers.
  await page.locator('.pane-slot.active .search-input').fill('INFO');
  await page.locator('.pane-slot.active .search-input').press('Enter');
  await page.locator('.pane-slot.active .filtered-row').first().waitFor({ timeout: 15000 });
  await page.waitForTimeout(900);
  const filteredScroll = page.locator('.pane-slot.active .filtered-scroll');
  const filteredBox = await filteredScroll.boundingBox();
  const firstFilteredRow = await page
    .locator('.pane-slot.active .filtered-row')
    .nth(1)
    .boundingBox();
  assert.ok(filteredBox && firstFilteredRow, 'the filtered list must be laid out');
  await page.mouse.move(firstFilteredRow.x + 90, firstFilteredRow.y + firstFilteredRow.height / 2);
  await page.mouse.down();
  await page.mouse.move(filteredBox.x + 90, filteredBox.y + filteredBox.height - 4, { steps: 8 });
  await page.waitForTimeout(2000);
  const filteredScrollTop = await filteredScroll.evaluate((element) => element.scrollTop);
  const filteredBadge = await readBadge(page);
  console.log(
    `filtered-autoscroll scrollTop=${Math.round(filteredScrollTop)} badge=${JSON.stringify(filteredBadge.text)}`,
  );
  assert.ok(filteredScrollTop > 0, 'the filtered list must scroll while dragging at its edge');
  assert.ok(
    durationSeconds(filteredBadge.label) >= 30,
    'the filtered selection must keep growing across window reloads',
  );
  await page.mouse.up();
  await page.waitForTimeout(700);
  const filteredSettled = await filteredScroll.evaluate((element) => element.scrollTop);
  await page.waitForTimeout(800);
  assert.equal(
    await filteredScroll.evaluate((element) => element.scrollTop),
    filteredSettled,
    'releasing the pointer must stop the filtered scrolling',
  );
  console.log('autoscroll-filtered-ok');

  // The settings dialog manages named sets: a new rule has to render at once.
  await page.evaluate(() => window.__klogg.openSettings('highlighters'));
  await page.locator('.settings-dialog').waitFor({ timeout: 5000 });
  if (screenshotPath) {
    const dialogShot = path.resolve(repoRoot, 'out', 'tmp', 'settings-highlighters.png');
    await page.screenshot({ path: dialogShot, fullPage: true });
    console.log(`screenshot=${dialogShot}`);
  }
  const ruleCardsBefore = await page.locator('.highlighter-rules .rule-card').count();
  const setRows = await page.locator('.highlighter-sets .set-row').count();
  await page.locator('.highlighter-rules .settings-section-title button').click();
  await page.waitForTimeout(400);
  const ruleCardsAfter = await page.locator('.highlighter-rules .rule-card').count();
  console.log(`settings sets=${setRows} rules=${ruleCardsBefore}->${ruleCardsAfter}`);
  assert.ok(setRows >= 1, 'the active set must be listed');
  assert.equal(ruleCardsAfter, ruleCardsBefore + 1, 'adding a rule must render a new card');
  await page.keyboard.press('Escape');
  await page.waitForTimeout(400);
  assert.equal(await page.locator('.settings-dialog').count(), 0, 'Escape closes the dialog');

  assert.deepEqual(consoleProblems, [], 'the UI must report no console errors');

  console.log('selection-time-ok');
  await app.evaluate(({ app: electronApp }) => electronApp.quit());
  await app.close().catch(() => {});
  cleanup();
})().catch((error) => {
  console.error(error);
  cleanup();
  process.exit(1);
});
