import fs from 'node:fs';
import path from 'node:path';

import { expect, test, type APIRequestContext, type Page } from '@playwright/test';

const fixtureRoot = path.resolve(process.cwd(), 'e2e/fixtures');
const alphaPath = path.join(fixtureRoot, 'alpha.log');
const betaPath = path.join(fixtureRoot, 'beta.log');

test.beforeEach(async ({ request }) => {
  await clearMarks(request, alphaPath);
  await clearMarks(request, betaPath);
});

async function clearMarks(request: APIRequestContext, filePath: string): Promise<void> {
  const connected = await request.post('/api/session.connect', {
    data: { name: 'e2e-cleanup', role: 'human' },
  });
  expect(connected.ok()).toBe(true);
  const clientId = (await connected.json()).value.clientId as string;
  const headers = { 'X-Klogg-Client-Id': clientId };

  const opened = await request.post('/api/file.open', {
    headers,
    data: { path: filePath },
  });
  expect(opened.ok()).toBe(true);
  const file = (await opened.json()).value as { fileId: number; totalLines: number };
  const lines = Array.from({ length: file.totalLines }, (_, index) => index);
  const removed = await request.post('/api/marks.remove', {
    headers,
    data: { fileId: file.fileId, lines },
  });
  expect(removed.ok()).toBe(true);
  await request.post('/api/file.close', {
    headers,
    data: { fileId: file.fileId },
  });
}

async function installDefaults(page: Page, history: string[] = []): Promise<void> {
  await page.addInitScript(
    ({ history }) => {
      localStorage.setItem(
        'klogg.web.settings',
        JSON.stringify({ language: 'zh-CN', restoreSession: false }),
      );
      localStorage.setItem('klogg.web.searchHistory', JSON.stringify(history));
    },
    { history },
  );
}

async function ensureTextSearch(page: Page): Promise<void> {
  const chip = page.locator('.pane-slot.active .chip-toggle:not(.live-toggle)');
  if ((await chip.count()) === 0) {
    return;
  }
  if (await chip.evaluate((element) => element.classList.contains('active'))) {
    await chip.click();
  }
}

async function openFile(page: Page, filePath: string): Promise<void> {
  await page.goto(`/?file=${encodeURIComponent(filePath)}`);
  await expect(page.locator('.file-tab-title')).toContainText(path.basename(filePath));
  await expect(page.locator('.terminal-host')).toBeVisible();
  await ensureTextSearch(page);
}

async function openSecondFileFromBrowser(page: Page, name: string): Promise<void> {
  await page.locator('.file-tab-new').first().click();
  await page
    .locator('.file-source-tabs button')
    .filter({ hasText: '目录' })
    .click();
  await page.locator('.file-path').fill(fixtureRoot);
  await page.locator('.file-path').press('Enter');
  const row = page.locator('.file-row').filter({ hasText: name });
  await expect(row).toBeVisible();
  await row.dblclick();
  await expect(page.locator('.file-tab-title')).toHaveCount(2);
  await expect(page.locator('.file-dialog')).toHaveCount(0);
  await ensureTextSearch(page);
}

async function setAutoRun(page: Page, enabled: boolean): Promise<void> {
  await page.locator('.settings-button:not(.theme-toggle)').click();
  await page
    .locator('.settings-tabs button')
    .filter({ hasText: '搜索' })
    .click();
  const autoRun = page
    .locator('.settings-row')
    .filter({ hasText: '添加/替换' })
    .locator('input[type=checkbox]');
  if (enabled) {
    await autoRun.check();
  } else {
    await autoRun.uncheck();
  }
  await page.locator('.settings-footer .primary').click();
  await expect(page.locator('.settings-dialog')).toHaveCount(0);
}

async function setFilteredSearchRange(
  page: Page,
  line: number,
  which: 'start' | 'end',
): Promise<void> {
  await page.locator(`.filtered-row[data-line="${line}"]`).click({ button: 'right' });
  await page
    .locator('.context-item')
    .filter({ hasText: which === 'start' ? '设为搜索起始行' : '设为搜索结束行' })
    .click();
}

/** Client rect of the first occurrence of a word inside a text element. */
async function wordRect(
  page: Page,
  selector: string,
  word: string,
): Promise<{ left: number; right: number; top: number; bottom: number }> {
  return page.locator(selector).first().evaluate((element, word) => {
    const text = element.textContent ?? '';
    const wordIndex = text.indexOf(word);
    if (wordIndex < 0) {
      throw new Error(`Word not found in ${selector}: ${word}`);
    }
    const walker = document.createTreeWalker(element, NodeFilter.SHOW_TEXT);
    let offset = 0;
    let startNode: Text | null = null;
    let startOffset = 0;
    let endNode: Text | null = null;
    let endOffset = 0;
    let node = walker.nextNode();
    while (node) {
      const textNode = node as Text;
      const length = textNode.nodeValue?.length ?? 0;
      if (!startNode && wordIndex >= offset && wordIndex <= offset + length) {
        startNode = textNode;
        startOffset = wordIndex - offset;
      }
      if (!endNode && wordIndex + word.length >= offset && wordIndex + word.length <= offset + length) {
        endNode = textNode;
        endOffset = wordIndex + word.length - offset;
        break;
      }
      offset += length;
      node = walker.nextNode();
    }
    if (!startNode || !endNode) {
      throw new Error(`Could not map word offsets for ${word}`);
    }
    const range = document.createRange();
    range.setStart(startNode, startOffset);
    range.setEnd(endNode, endOffset);
    const rect = range.getBoundingClientRect();
    return { left: rect.left, right: rect.right, top: rect.top, bottom: rect.bottom };
  }, word);
}

async function doubleClickWord(page: Page, selector: string, word: string): Promise<void> {
  const rect = await wordRect(page, selector, word);
  await page.mouse.dblclick(
    rect.left + (rect.right - rect.left) / 2,
    rect.top + (rect.bottom - rect.top) / 2,
  );
}

/** Drags across a word inside one row, leaving it as the native selection. */
async function dragWord(page: Page, selector: string, word: string): Promise<void> {
  const rect = await wordRect(page, selector, word);
  const y = rect.top + (rect.bottom - rect.top) / 2;
  await page.mouse.move(rect.left + 1, y);
  await page.mouse.down();
  await page.mouse.move(rect.right - 1, y, { steps: 8 });
  await page.mouse.up();
}

test('opens a file, searches, and highlights double-clicked words', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  const search = page.locator('.search-input');
  await search.fill('INFO');
  await search.press('Enter');

  await expect(page.locator('.filtered-row').first()).toBeVisible();
  await expect(page.locator('.filtered-tabs')).toHaveCount(0);
  await expect.poll(() => page.locator('.filtered-row').count()).toBeGreaterThan(0);

  await doubleClickWord(page, '.filtered-text', 'INFO');
  await expect(page.locator('.filtered-text .word-hit').first()).toBeVisible();
  await expect.poll(() => page.locator('.word-hit-overlay').count()).toBeGreaterThan(0);

  const selected = await page.evaluate(() => window.getSelection()?.toString() ?? '');
  expect(selected).toBe('INFO');
});

test('a dragged within-row selection highlights its matches in the results', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  const search = page.locator('.search-input');
  await search.fill('INFO');
  await search.press('Enter');
  await expect(page.locator('.filtered-row').first()).toBeVisible();
  await expect(page.locator('.filtered-text .word-hit')).toHaveCount(0);

  await dragWord(page, '.filtered-text', 'INFO');

  const hit = page.locator('.filtered-text .word-hit').first();
  await expect(hit).toBeVisible();
  expect(await hit.textContent()).toBe('INFO');
  // The dragged text stays selected after the highlight re-renders the rows.
  expect(await page.evaluate(() => window.getSelection()?.toString() ?? '')).toBe('INFO');
});

test('search history stays aligned, supports fuzzy search and per-entry delete', async ({ page }) => {
  const history = Array.from({ length: 12 }, (_, index) => `e2e-history-${index + 1}-alpha`);
  await installDefaults(page, history);
  await openFile(page, alphaPath);

  await page.locator('.search-history-toggle').click();
  await expect(page.locator('.history-window')).toBeVisible();

  const comboBox = await page.locator('.search-combo').boundingBox();
  const panelBox = await page.locator('.history-window').boundingBox();
  expect(comboBox).not.toBeNull();
  expect(panelBox).not.toBeNull();
  expect(panelBox!.width).toBeCloseTo(comboBox!.width, 0);
  expect(panelBox!.y).toBeGreaterThan(comboBox!.y + comboBox!.height);

  await expect(page.locator('.history-search')).toHaveCount(0);
  const historyInput = page.locator('.history-footer .history-search-input');
  await expect(historyInput).toBeVisible();
  await expect(page.locator('.history-scrollbar-thumb')).toBeVisible();

  const footer = page.locator('.history-footer');
  const footerTop = (await footer.boundingBox())?.y;
  expect(footerTop).not.toBeUndefined();
  await page.locator('.history-list').evaluate((element) => {
    element.scrollTop = 200;
  });
  await expect.poll(async () => (await footer.boundingBox())?.y).toBe(footerTop);

  await historyInput.fill('history-12');
  await expect(page.locator('.history-entry')).toHaveCount(1);

  await page.locator('.history-entry-remove').click();
  const remaining = await page.evaluate(() =>
    JSON.parse(localStorage.getItem('klogg.web.searchHistory') ?? '[]'),
  );
  expect(remaining).not.toContain('e2e-history-12-alpha');

  await page.locator('.history-clear').click();
  await expect(page.locator('.history-window')).toHaveCount(0);
  const cleared = await page.evaluate(() => localStorage.getItem('klogg.web.searchHistory'));
  expect(cleared === null || cleared === '[]').toBeTruthy();
});

test('opens a second file from the file browser', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  await openSecondFileFromBrowser(page, 'beta.log');

  await expect(page.locator('.file-tab')).toHaveCount(2);
  await expect(page.locator('.file-tab.active')).toContainText('beta.log');
  await page.locator('.file-tab').filter({ hasText: 'alpha.log' }).click();
  await expect(page.locator('.file-tab.active')).toContainText('alpha.log');
});

test('persists highlight colors and auto-run settings', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  await page.locator('.settings-button:not(.theme-toggle)').click();
  await page
    .locator('.settings-tabs button')
    .filter({ hasText: '视图' })
    .click();
  // The setting covers drag/word selections alike, so its wording is neutral.
  await expect(
    page.locator('.settings-row').filter({ hasText: '选中高亮背景' }),
  ).toHaveCount(1);
  await expect(
    page.locator('.settings-row').filter({ hasText: '选中高亮文字' }),
  ).toHaveCount(1);
  const colors = page.locator('.settings-row input[type=color]');
  await colors.nth(0).evaluate((input) => {
    input.value = '#ff3366';
    input.dispatchEvent(new Event('input', { bubbles: true }));
  });
  await colors.nth(1).evaluate((input) => {
    input.value = '#ffffff';
    input.dispatchEvent(new Event('input', { bubbles: true }));
  });
  await page.locator('.settings-footer .primary').click();
  await expect(page.locator('.settings-dialog')).toHaveCount(0);

  const settings = await page.evaluate(() =>
    JSON.parse(localStorage.getItem('klogg.web.settings') ?? '{}'),
  );
  expect(settings.wordHighlightBackground).toBe('#ff3366');
  expect(settings.wordHighlightForeground).toBe('#ffffff');
  expect(
    await page.evaluate(() =>
      getComputedStyle(document.documentElement).getPropertyValue('--word-highlight-bg').trim(),
    ),
  ).toBe('#ff3366');

  await page.locator('.settings-button:not(.theme-toggle)').click();
  await page
    .locator('.settings-tabs button')
    .filter({ hasText: '搜索' })
    .click();
  const autoRun = page
    .locator('.settings-row')
    .filter({ hasText: '添加/替换' })
    .locator('input[type=checkbox]');
  await autoRun.uncheck();
  await expect(autoRun).not.toBeChecked();
  await page.locator('.settings-footer .primary').click();
  await expect(page.locator('.settings-dialog')).toHaveCount(0);
  expect(
    await page.evaluate(
      () => JSON.parse(localStorage.getItem('klogg.web.settings') ?? '{}').autoRunSearch,
    ),
  ).toBe(false);
});

test('marks a row and keeps the mark visible in the filtered view', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  await page.locator('.gutter-row[data-line="1"]').click();
  await expect(page.locator('.gutter-row.marked')).toHaveCount(1);
  await expect(page.locator('.marked-line-overlay-row')).toHaveCount(1);

  const search = page.locator('.search-input');
  await search.fill('INFO');
  await search.press('Enter');
  await expect(page.locator('.filtered-row').first()).toBeVisible();
  await expect(page.locator('.filtered-row.marked')).toHaveCount(1);
});

test('a mark added after the search joins the marks and matches projection', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  const search = page.locator('.pane-slot.active .search-input');
  await search.fill('INFO');
  await search.press('Enter');
  await expect(page.locator('.filtered-row')).toHaveCount(8);

  // Line 3 is a DEBUG line: it only enters the projection through its mark.
  await page.locator('.gutter-row[data-line="3"]').click();
  await expect(page.locator('.filtered-row')).toHaveCount(9);
  await expect(page.locator('.filtered-row.marked')).toHaveCount(1);
  await expect(page.locator('.filtered-row[data-line="3"]')).toBeVisible();

  // Unmarking it removes it again, without reselecting the visibility.
  await page.locator('.gutter-row[data-line="3"]').click();
  await expect(page.locator('.filtered-row')).toHaveCount(8);
  await expect(page.locator('.filtered-row.marked')).toHaveCount(0);
});

test('keeps search state isolated between tabs', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  await page.locator('.search-input').fill('INFO');
  await page.locator('.search-input').press('Enter');
  await expect(page.locator('.filtered-row')).toHaveCount(8);

  await openSecondFileFromBrowser(page, 'beta.log');
  await expect(page.locator('.pane-slot.active .search-input')).toHaveValue('');
  await expect(page.locator('.pane-slot.active .filtered-row')).toHaveCount(0);

  await page
    .locator('.file-tab')
    .filter({ hasText: 'alpha.log' })
    .click();
  await expect(page.locator('.pane-slot.active .search-input')).toHaveValue('INFO');
  await expect(page.locator('.pane-slot.active .filtered-row')).toHaveCount(8);

  await page
    .locator('.file-tab')
    .filter({ hasText: 'beta.log' })
    .click();
  await expect(page.locator('.pane-slot.active .search-input')).toHaveValue('');
});

async function selectTextInRow(
  page: Page,
  rowIndex: number,
  needle: string,
): Promise<{ x: number; y: number }> {
  const row = page.locator('.pane-slot.active .filtered-row').nth(rowIndex);
  const points = await row.evaluate((element, text) => {
    const span = element.querySelector('.filtered-text');
    if (!span) {
      throw new Error('row has no text span');
    }
    const content = span.textContent ?? '';
    const index = content.indexOf(text);
    if (index < 0) {
      throw new Error(`"${text}" not found in row`);
    }
    const walker = document.createTreeWalker(span, NodeFilter.SHOW_TEXT);
    let offset = 0;
    let startNode: Text | null = null;
    let startOffset = 0;
    let endNode: Text | null = null;
    let endOffset = 0;
    let node = walker.nextNode();
    while (node) {
      const textNode = node as Text;
      const length = textNode.nodeValue?.length ?? 0;
      if (!startNode && index >= offset && index <= offset + length) {
        startNode = textNode;
        startOffset = index - offset;
      }
      if (!endNode && index + text.length >= offset && index + text.length <= offset + length) {
        endNode = textNode;
        endOffset = index + text.length - offset;
        break;
      }
      offset += length;
      node = walker.nextNode();
    }
    if (!startNode || !endNode) {
      throw new Error('cannot map the text offsets');
    }
    const range = document.createRange();
    range.setStart(startNode, startOffset);
    range.setEnd(endNode, endOffset);
    const rect = range.getBoundingClientRect();
    return {
      left: rect.left + 2,
      right: rect.right - 2,
      y: rect.top + rect.height / 2,
    };
  }, needle);

  await page.mouse.move(points.left, points.y);
  await page.mouse.down();
  await page.mouse.move(points.right, points.y, { steps: 8 });
  await page.mouse.up();
  const selected = await page.evaluate(() => window.getSelection()?.toString() ?? '');
  if (!selected) {
    throw new Error('the drag produced no text selection');
  }
  // Right-clicking inside the selection keeps it; clicking elsewhere in the row
  // collapses it, and the menu would then act on the whole row.
  return { x: (points.left + points.right) / 2, y: points.y };
}

async function replaceSearchFromSelection(page: Page, point: { x: number; y: number }): Promise<void> {
  await page.mouse.click(point.x, point.y, { button: 'right' });
  await page
    .locator('.context-item')
    .filter({ hasText: '替换搜索' })
    .click();
}

test('auto-run setting controls replace-search behavior', async ({ page }) => {
  // Two settings round trips plus two selections: more than the default budget.
  test.setTimeout(90_000);
  await installDefaults(page);
  await openFile(page, alphaPath);

  // Auto-run is on by default: replacing the search with a selection runs it.
  await page.locator('.search-input').fill('INFO');
  await page.locator('.search-input').press('Enter');
  await expect(page.locator('.filtered-row')).toHaveCount(8);
  await replaceSearchFromSelection(page, await selectTextInRow(page, 0, '10:00:00'));
  await expect(page.locator('.filtered-row')).toHaveCount(1);

  // With auto-run off the search text changes but the results do not.
  await setAutoRun(page, false);
  await page.locator('.search-input').fill('INFO');
  await page.locator('.search-input').press('Enter');
  await expect(page.locator('.filtered-row')).toHaveCount(8);
  await page.waitForTimeout(600);
  await replaceSearchFromSelection(page, await selectTextInRow(page, 0, '10:00:00'));
  await expect(page.locator('.search-input')).toHaveValue('10:00:00');
  await expect(page.locator('.filtered-row')).toHaveCount(8);
});

test('freezes the main viewport while dragging the splitter', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);
  await page.waitForTimeout(500);

  let renderRequests = 0;
  page.on('request', (request) => {
    if (request.url().endsWith('/api/render.viewport')) {
      renderRequests += 1;
    }
  });

  const initialRows = await page.evaluate(
    () => (window as Window & { __kloggViewport?: { term?: { rows: number } } }).__kloggViewport?.term?.rows,
  );
  const handle = await page.locator('.split-handle').boundingBox();
  expect(handle).not.toBeNull();
  const x = handle!.x + handle!.width / 2;
  await page.mouse.move(x, handle!.y + handle!.height / 2);
  await page.mouse.down();
  for (let index = 0; index < 12; index++) {
    await page.mouse.move(x, index % 2 === 0 ? 300 : 700, { steps: 1 });
  }
  // Terminal sizing is frozen for the whole drag, not merely debounced.
  expect(renderRequests).toBe(0);
  expect(
    await page.evaluate(
      () => (window as Window & { __kloggViewport?: { term?: { rows: number } } }).__kloggViewport?.term?.rows,
    ),
  ).toBe(initialRows);
  await page.mouse.up();
  await page.waitForTimeout(350);

  // One frame after the drag settles is enough; the old implementation
  // requested a frame for every pointer move.
  expect(renderRequests).toBeLessThanOrEqual(2);

  const chrome = await page.evaluate(() => {
    const probe = document.createElement('div');
    probe.style.background = 'var(--bg)';
    document.body.append(probe);
    const background = getComputedStyle(probe).backgroundColor;
    probe.remove();
    const viewport = document.querySelector('.xterm-viewport');
    const hscroll = document.querySelector('.hscroll');
    return {
      background,
      viewport: viewport ? getComputedStyle(viewport).backgroundColor : '',
      hscroll: hscroll ? getComputedStyle(hscroll).backgroundColor : '',
    };
  });
  expect(chrome.viewport).toBe(chrome.background);
  expect(chrome.hscroll).toBe(chrome.background);
});

test('splitter can collapse either pane to zero and restores the extent', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);
  await page.waitForTimeout(500);

  const paneState = () =>
    page.evaluate(() => {
      const workspace = document.querySelector('.workspace')!.getBoundingClientRect();
      const main = document.querySelector('.main-pane')!.getBoundingClientRect();
      const bottom = document.querySelector('.bottom-pane')!.getBoundingClientRect();
      return {
        workspaceHeight: workspace.height,
        mainHeight: main.height,
        bottomHeight: bottom.height,
        stored: localStorage.getItem('klogg.web.split'),
      };
    });

  async function dragSplitterTo(y: number): Promise<void> {
    const handle = await page.locator('.split-handle').boundingBox();
    expect(handle).not.toBeNull();
    const x = handle!.x + handle!.width / 2;
    await page.mouse.move(x, handle!.y + handle!.height / 2);
    await page.mouse.down();
    await page.mouse.move(x, y, { steps: 8 });
    await page.mouse.up();
    await page.waitForTimeout(400);
  }

  const workspace = await page.locator('.workspace').boundingBox();
  expect(workspace).not.toBeNull();

  // Search pane full height: the main view is collapsed and the handle remains
  // reachable at the top.
  await dragSplitterTo(workspace!.y - 60);
  let state = await paneState();
  expect(state.mainHeight).toBeLessThanOrEqual(1);
  expect(state.bottomHeight).toBeGreaterThan(state.workspaceHeight - 8);
  expect(Number(state.stored)).toBe(0);

  // Main view full height: the search pane and its search bar are collapsed.
  await dragSplitterTo(workspace!.y + workspace!.height + 60);
  state = await paneState();
  expect(state.mainHeight).toBeGreaterThan(state.workspaceHeight - 8);
  expect(state.bottomHeight).toBeLessThanOrEqual(1);
  expect(Number(state.stored)).toBe(100);

  // The persisted extreme must survive a reload instead of falling back to 62.
  await page.reload();
  await expect(page.locator('.file-tab-title')).toContainText(path.basename(alphaPath));
  await expect(page.locator('.terminal-host')).toBeVisible();
  await page.waitForTimeout(500);
  state = await paneState();
  expect(Number(state.stored)).toBe(100);
  expect(state.mainHeight).toBeGreaterThan(state.workspaceHeight - 8);
  expect(state.bottomHeight).toBeLessThanOrEqual(1);
});

test('passes the search range to the main viewport', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  await page.locator('.search-input').fill('INFO');
  await page.locator('.search-input').press('Enter');
  await expect(page.locator('.filtered-row')).toHaveCount(8);

  await setFilteredSearchRange(page, 2, 'start');
  await expect(page.locator('.filtered-row[data-line="8"]')).toBeVisible();

  const rangedRender = page.waitForRequest(
    (request) => {
      if (!request.url().endsWith('/api/render.viewport')) {
        return false;
      }
      try {
        const body = request.postDataJSON() as {
          options?: { searchStartIndex?: number; searchEndIndex?: number };
        };
        return body.options?.searchStartIndex === 2 && body.options?.searchEndIndex === 9;
      } catch {
        return false;
      }
    },
    { timeout: 5_000 },
  );

  await setFilteredSearchRange(page, 8, 'end');
  await rangedRender;
});

test('the follow shortcut toggles the follow action', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  const follow = page.locator('.pane-slot.active .follow-toggle');
  await expect(follow).not.toHaveClass(/active/);

  await page.locator('.terminal-host').click({ position: { x: 240, y: 60 } });
  await page.keyboard.press('f');
  await expect(follow).toHaveClass(/active/);

  await page.keyboard.press('f');
  await expect(follow).not.toHaveClass(/active/);
});

test('typing does not search until Enter is pressed', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  const searches: string[] = [];
  page.on('request', (request) => {
    if (request.url().endsWith('/api/search.run')) {
      searches.push(request.postDataJSON()?.params?.pattern ?? '');
    }
  });

  const search = page.locator('.pane-slot.active .search-input');
  await search.click();
  await page.keyboard.type('INFO', { delay: 40 });
  await expect(page.locator('.pane-slot.active .filtered-row')).toHaveCount(0);
  expect(searches).toHaveLength(0);

  // Search options are part of the pattern editing, not of running the search.
  await page.locator('.pane-slot.active .searchbar .toggle').nth(1).click();
  await page.locator('.pane-slot.active .searchbar .toggle').nth(1).click();
  await page.waitForTimeout(300);
  expect(searches).toHaveLength(0);

  await search.press('Enter');
  await expect(page.locator('.pane-slot.active .filtered-row')).toHaveCount(8);
  expect(searches).toEqual(['INFO']);
});

test('clicking a view releases the search box focus', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  const search = page.locator('.pane-slot.active .search-input');
  await search.fill('INFO');
  await search.press('Enter');
  await expect(page.locator('.pane-slot.active .filtered-row').first()).toBeVisible();

  await search.click();
  await page.locator('.filtered-text').first().click();
  await expect
    .poll(() => page.evaluate(() => document.activeElement?.classList.contains('search-input')))
    .toBe(false);

  // The single-letter shortcut now marks instead of typing into the search box.
  await page.keyboard.press('m');
  await expect(page.locator('.filtered-row.marked')).toHaveCount(1);
  await expect(search).toHaveValue('INFO');
});

test('the search view supports selecting part of a line', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  const search = page.locator('.pane-slot.active .search-input');
  await search.fill('INFO');
  await search.press('Enter');
  const row = page.locator('.pane-slot.active .filtered-row').first();
  await expect(row).toBeVisible();

  const box = await row.boundingBox();
  expect(box).not.toBeNull();
  await page.mouse.move(box!.x + 90, box!.y + box!.height / 2);
  await page.mouse.down();
  await page.mouse.move(box!.x + 210, box!.y + box!.height / 2, { steps: 10 });
  await page.mouse.up();

  const selected = await page.evaluate(() => window.getSelection()?.toString() ?? '');
  expect(selected.length).toBeGreaterThan(0);
  expect(await page.locator('.filtered-row.selected').count()).toBe(0);

  // A drag across rows still selects whole rows. The delay keeps the second
  // press from being a double click, which selects a word instead.
  await page.waitForTimeout(600);
  const first = await page.locator('.filtered-row').nth(0).boundingBox();
  const third = await page.locator('.filtered-row').nth(2).boundingBox();
  await page.mouse.move(first!.x + 100, first!.y + first!.height / 2);
  await page.mouse.down();
  await page.mouse.move(third!.x + 100, third!.y + third!.height / 2, { steps: 12 });
  await page.mouse.up();
  await expect(page.locator('.filtered-row.selected')).toHaveCount(3);
  expect(await page.evaluate(() => window.getSelection()?.toString() ?? '')).toBe('');
});

test('copy with colors puts the composed HTML and the plain text on the clipboard', async ({
  page,
  context,
}) => {
  await context.grantPermissions(['clipboard-read', 'clipboard-write']);
  await installDefaults(page);
  await openFile(page, alphaPath);

  const search = page.locator('.pane-slot.active .search-input');
  await search.fill('INFO');
  await search.press('Enter');
  await expect(page.locator('.filtered-row')).toHaveCount(8);

  // A whole row: the search highlight makes the copied line contain a span.
  const row = page.locator('.filtered-row').first();
  await row.click({ button: 'right' });
  await page.locator('.context-item').filter({ hasText: '复制（带颜色）' }).click();

  const readClipboard = (): Promise<{ types: string[]; html: string; text: string } | null> =>
    page.evaluate(async () => {
      const items = await navigator.clipboard.read();
      for (const item of items) {
        if (!item.types.includes('text/html')) {
          continue;
        }
        return {
          types: item.types,
          html: await (await item.getType('text/html')).text(),
          text: item.types.includes('text/plain')
            ? await (await item.getType('text/plain')).text()
            : '',
        };
      }
      return null;
    });

  let payload = await readClipboard();
  for (let attempt = 0; attempt < 40 && !payload; attempt++) {
    await page.waitForTimeout(100);
    payload = await readClipboard();
  }

  expect(payload).not.toBeNull();
  expect(payload!.types).toContain('text/plain');
  expect(
    payload!.html.startsWith('<pre style="font-family:monospace; white-space:pre; margin:0">'),
  ).toBe(true);
  expect(payload!.html.endsWith('</pre>')).toBe(true);
  expect(payload!.html).toContain('<span style="color:#');
  expect(payload!.html).not.toContain('\u001b');
  expect(payload!.html).toContain('INFO');
  expect(payload!.text).toContain('INFO');
  expect(payload!.text).not.toContain('\u001b');
});

test('color labels apply from both views and stay visible in both', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  const search = page.locator('.pane-slot.active .search-input');
  await search.fill('INFO');
  await search.press('Enter');
  await expect(page.locator('.filtered-row')).toHaveCount(8);

  const renderOptions: string[][] = [];
  page.on('request', (request) => {
    if (!request.url().endsWith('/api/render.viewport')) {
      return;
    }
    try {
      const options = request.postDataJSON()?.options?.quickHighlighterWords;
      if (Array.isArray(options)) {
        renderOptions.push(options.flat());
      }
    } catch {
      // ignore malformed bodies
    }
  });

  // Apply a label from the search view.
  const row = page.locator('.filtered-row').nth(1);
  const box = await row.boundingBox();
  await page.mouse.move(box!.x + 90, box!.y + box!.height / 2);
  await page.mouse.down();
  await page.mouse.move(box!.x + 150, box!.y + box!.height / 2, { steps: 8 });
  await page.mouse.up();
  const selected = await page.evaluate(() => window.getSelection()?.toString() ?? '');
  expect(selected.length).toBeGreaterThan(0);

  await page.mouse.click(box!.x + 120, box!.y + box!.height / 2, { button: 'right' });
  await expect(page.locator('.context-grid-title')).toHaveText('颜色标签');
  await page.locator('.context-grid-cell').nth(1).click();

  await expect(page.locator('.filtered-text .color-label').first()).toBeVisible();
  await expect
    .poll(() => renderOptions.some((words) => words.includes(selected)))
    .toBe(true);

  // Applying the same label to the same text again removes it from both views.
  await page.waitForTimeout(600);
  await page.mouse.move(box!.x + 90, box!.y + box!.height / 2);
  await page.mouse.down();
  await page.mouse.move(box!.x + 150, box!.y + box!.height / 2, { steps: 8 });
  await page.mouse.up();
  await page.mouse.click(box!.x + 120, box!.y + box!.height / 2, { button: 'right' });
  await page.locator('.context-grid-cell').nth(1).click();
  await expect(page.locator('.filtered-text .color-label')).toHaveCount(0);
});

test('a color label added from the main view reaches the search view at once', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  const search = page.locator('.pane-slot.active .search-input');
  await search.fill('INFO');
  await search.press('Enter');
  await expect(page.locator('.filtered-row')).toHaveCount(8);

  // A whole-line selection in the main view carries the frame's padding unless it
  // is trimmed, and a padded key never matches the text the search view renders.
  const host = await page.locator('.terminal-host').boundingBox();
  expect(host).not.toBeNull();
  await page.mouse.click(host!.x + 240, host!.y + 10);
  await expect(page.locator('.pane-slot.active .filtered-text').first()).toBeVisible();

  // Ctrl+Shift+1 is the "color label 1" shortcut.
  await page.keyboard.press('Control+Shift+1');
  await expect(page.locator('.filtered-text .color-label').first()).toBeVisible();

  // The applied key is the visible line text, not a padded copy of it: the label
  // spans of the row join back into the line, without trailing blanks.
  const labelText = await page.evaluate(() =>
    [...document.querySelectorAll('.filtered-row .filtered-text .color-label')]
      .map((element) => element.textContent ?? '')
      .join(''),
  );
  expect(labelText).toBe('2026-10-25T10:00:00.000Z INFO alpha worker=1 queue=1');
  expect(labelText.endsWith(' ')).toBe(false);

  // The same shortcut toggles the label off again.
  await page.keyboard.press('Control+Shift+1');
  await expect(page.locator('.filtered-text .color-label')).toHaveCount(0);
});

test('chips can be edited in place', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  await page.locator('.pane-slot.active .chip-toggle:not(.live-toggle)').click();
  const chipInput = page.locator('.pane-slot.active .chip-input');
  await chipInput.fill('INFO');
  await chipInput.press('Enter');
  await expect(page.locator('.filtered-row')).toHaveCount(8);

  // A click on the chip text swaps it for an input seeded with the term.
  await page.locator('.pane-slot.active .search-chip-label').first().click();
  const editor = page.locator('.pane-slot.active .search-chip-edit');
  await expect(editor).toBeVisible();
  await expect(editor).toHaveValue('INFO');
  await editor.fill('WARN');
  await editor.press('Enter');
  await expect(page.locator('.pane-slot.active .search-chip-label')).toHaveText('WARN');
  await expect(page.locator('.filtered-row')).toHaveCount(2);

  // Escape leaves the chip untouched.
  await page.locator('.pane-slot.active .search-chip-label').first().click();
  await page.locator('.pane-slot.active .search-chip-edit').fill('DEBUG');
  await page.locator('.pane-slot.active .search-chip-edit').press('Escape');
  await expect(page.locator('.pane-slot.active .search-chip-label')).toHaveText('WARN');
  await expect(page.locator('.filtered-row')).toHaveCount(2);

  // An AND group keeps its type and is re-split on "&".
  await page.locator('.pane-slot.active .search-chip-remove').first().click();
  await expect(page.locator('.pane-slot.active .search-chip')).toHaveCount(0);
  await chipInput.fill('alpha & worker=4');
  await chipInput.press('Enter');
  await expect(page.locator('.pane-slot.active .search-chip.and')).toHaveCount(1);
  await expect(page.locator('.filtered-row')).toHaveCount(1);
  await page.locator('.pane-slot.active .search-chip.and .search-chip-label').click();
  const andEditor = page.locator('.pane-slot.active .search-chip-edit');
  await expect(andEditor).toHaveValue('alpha & worker=4');
  await andEditor.fill('alpha & worker=5');
  await andEditor.press('Enter');
  await expect(page.locator('.pane-slot.active .search-chip.and .search-chip-label')).toHaveText(
    'alpha & worker=5',
  );
  await expect(page.locator('.filtered-row')).toHaveCount(1);
});

test('the color label shortcuts label the search view selection', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);
  const search = page.locator('.pane-slot.active .search-input');
  await search.fill('INFO');
  await search.press('Enter');
  await expect(page.locator('.filtered-row')).toHaveCount(8);

  // A row picked in the search view is the selection the shortcut labels; the
  // main view has no selection of its own here.
  const row = page.locator('.filtered-row').nth(2);
  await row.click();
  await expect(row).toHaveClass(/selected/);
  await page.keyboard.press('Control+Shift+1');

  const labels = page.locator('.filtered-row.selected .color-label');
  await expect(labels.first()).toBeVisible();
  const labelText = await labels
    .evaluateAll((elements) => elements.map((element) => element.textContent ?? '').join(''));
  expect(labelText).toBe('2026-10-25T10:00:04.000Z INFO alpha worker=5 queue=5');

  await page.keyboard.press('Control+Shift+1');
  await expect(page.locator('.filtered-text .color-label')).toHaveCount(0);
});

// ---- helpers for the behaviour coverage below --------------------------
const tempLogs: string[] = [];

/** Writable log under the fixture root: the server only opens files below it. */
function createTempLog(lineCount: number, stepMs = 1000): string {
  const file = path.join(
    fixtureRoot,
    `.tmp-${Date.now()}-${Math.random().toString(36).slice(2, 8)}.log`,
  );
  const start = Date.parse('2026-10-25T10:00:00.000Z');
  const lines = Array.from(
    { length: lineCount },
    (_, index) =>
      `${new Date(start + index * stepMs).toISOString()} INFO temp line ${index + 1} worker=${index % 5}`,
  );
  fs.writeFileSync(file, `${lines.join('\n')}\n`, 'utf8');
  tempLogs.push(file);
  return file;
}

/** A log whose second line is far longer than any viewport, with a tail marker. */
function createLongLineLog(payloadLength: number): string {
  const file = path.join(
    fixtureRoot,
    `.tmp-long-${Date.now()}-${Math.random().toString(36).slice(2, 8)}.log`,
  );
  const payload = 'abcdefghij'.repeat(Math.ceil(payloadLength / 10)).slice(0, payloadLength);
  const lines = [
    '2026-10-25T10:00:00.000Z INFO short line',
    `2026-10-25T10:00:01.000Z WARN ${payload}TAIL-END`,
    '2026-10-25T10:00:02.000Z INFO another short line',
  ];
  fs.writeFileSync(file, `${lines.join('\n')}\n`, 'utf8');
  tempLogs.push(file);
  return file;
}

/**
 * A log wide enough that the filtered view needs a horizontal scrollbar, which
 * is what shrinks its client height after the tail rows render.
 */
function createWideLog(lineCount: number): string {
  const file = path.join(
    fixtureRoot,
    `.tmp-wide-${Date.now()}-${Math.random().toString(36).slice(2, 8)}.log`,
  );
  const lines = Array.from({ length: lineCount }, (_, index) => {
    const padding = 'payload-'.repeat((index % 6) + 2);
    return `${new Date(Date.parse('2026-10-25T10:00:00.000Z') + index * 1000).toISOString()} INFO row ${
      index + 1
    } ${padding}end`;
  });
  fs.writeFileSync(file, `${lines.join('\n')}\n`, 'utf8');
  tempLogs.push(file);
  return file;
}

test.afterAll(() => {
  for (const file of tempLogs) {
    fs.rmSync(file, { force: true });
  }
});

type SearchTotals = { running: boolean; searchTotal: number };

function searchTotals(page: Page): Promise<SearchTotals> {
  return page.evaluate(() => {
    const state = (
      window as Window & {
        __klogg?: { state?: { searchRunning?: boolean; searchTotal?: number } };
      }
    ).__klogg?.state;
    return {
      running: !!state?.searchRunning,
      searchTotal: state?.searchTotal ?? 0,
    };
  });
}

// ---- Qt parity fixes ---------------------------------------------------

test('a right click on a double clicked word keeps the portion in the results', async ({
  page,
  context,
}) => {
  await context.grantPermissions(['clipboard-read', 'clipboard-write']);
  await installDefaults(page);
  await openFile(page, alphaPath);

  const search = page.locator('.pane-slot.active .search-input');
  await search.fill('INFO');
  await search.press('Enter');
  await expect(page.locator('.filtered-row')).toHaveCount(8);

  const rect = await wordRect(page, '.filtered-text', 'INFO');
  const point = {
    x: rect.left + (rect.right - rect.left) / 2,
    y: rect.top + (rect.bottom - rect.top) / 2,
  };
  await page.mouse.dblclick(point.x, point.y);
  expect(await page.evaluate(() => window.getSelection()?.toString() ?? '')).toBe('INFO');

  await page.mouse.click(point.x, point.y, { button: 'right' });
  await expect(page.locator('.context-grid-title')).toBeVisible();
  // The portion stays the menu's subject: no whole-line selection takes over.
  await expect(page.locator('.filtered-row.selected')).toHaveCount(0);

  await page.locator('.context-item').filter({ hasText: '复制（带行号）' }).click();
  await expect.poll(() => page.evaluate(() => navigator.clipboard.readText())).toBe('1: INFO');
});

test('chip mode matches regex terms while the regex toggle is on', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  await page.locator('.pane-slot.active .chip-toggle:not(.live-toggle)').click();
  const chipInput = page.locator('.pane-slot.active .chip-input');
  await chipInput.fill('queue=1[0-2]');
  await chipInput.press('Enter');
  await page.locator('.pane-slot.active .search-action').click();

  // queue=10, 11, 12: only a regex search keeps the character class.
  await expect(page.locator('.pane-slot.active .filtered-row')).toHaveCount(3);
});

test('chip mode keeps terms literal while the regex toggle is off', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  await page.locator('.pane-slot.active label.toggle[title="正则表达式"]').click();
  await page.locator('.pane-slot.active .chip-toggle:not(.live-toggle)').click();
  const chipInput = page.locator('.pane-slot.active .chip-input');
  await chipInput.fill('queue=1[0-2]');
  await chipInput.press('Enter');
  await page.locator('.pane-slot.active .search-action').click();

  await expect.poll(() => searchTotals(page)).toEqual({ running: false, searchTotal: 0 });
  await expect(page.locator('.pane-slot.active .filtered-empty')).toBeVisible();
});

test('next mark moves the search pane along with the main view', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  // Mark the first and the ninth line from the main view.
  await clickLogRow(page, 0);
  await page.keyboard.press('m');
  await clickLogRow(page, 8);
  await page.keyboard.press('m');

  // The search view opens at the shared current line (the desktop shares one
  // current line between the views), so park it on the first line first.
  await clickLogRow(page, 0);

  const search = page.locator('.pane-slot.active .search-input');
  await search.fill('INFO');
  await search.press('Enter');
  await expect(page.locator('.filtered-row')).toHaveCount(8);
  await expect(page.locator('.pane-slot.active .filtered-row.current')).toHaveAttribute(
    'data-line',
    '0',
  );

  await clickLogRow(page, 0);
  await page.keyboard.press(']');

  await expect.poll(async () => (await paneState(page)).currentLine).toBe(8);
  await expect(page.locator('.pane-slot.active .filtered-row.current')).toHaveAttribute(
    'data-line',
    '8',
  );
});

test('changing the pattern keeps the search view position', async ({ page }) => {
  test.setTimeout(120_000);
  await installDefaults(page);
  const file = createTempLog(600, 1);
  await openFile(page, file);
  // The live refresh would restart the search on its own and race the reload.
  const liveToggle = page.locator('.pane-slot.active .live-toggle');
  if (await liveToggle.evaluate((element) => element.classList.contains('active'))) {
    await liveToggle.click();
  }

  const search = page.locator('.pane-slot.active .search-input');
  await search.fill('temp line');
  await search.press('Enter');
  await expect.poll(() => searchTotals(page)).toEqual({ running: false, searchTotal: 600 });
  // The list loads its first window asynchronously once the search id changes.
  await expect(page.locator('.pane-slot.active .filtered-row')).toHaveCount(200);

  // Move to the middle of the results; the virtual list reloads its window.
  const scroll = page.locator('.pane-slot.active .filtered-scroll');
  const firstRowLine = async () =>
    Number(await page.locator('.pane-slot.active .filtered-row').first().getAttribute('data-line'));
  await scroll.evaluate((element) => {
    element.scrollTop = element.scrollHeight / 2;
    element.dispatchEvent(new Event('scroll'));
  });
  await expect.poll(firstRowLine).toBeGreaterThan(200);

  const target = page.locator('.pane-slot.active .filtered-row').nth(5);
  const targetLine = Number(await target.getAttribute('data-line'));
  expect(targetLine).toBeGreaterThan(250);
  await target.click();
  await expect(page.locator('.pane-slot.active .filtered-row.current')).toHaveAttribute(
    'data-line',
    String(targetLine),
  );

  // A new pattern reopens the results at that line instead of the first row.
  await search.fill('INFO');
  await search.press('Enter');
  await expect.poll(() => searchTotals(page)).toEqual({ running: false, searchTotal: 600 });
  await expect(page.locator('.pane-slot.active .filtered-row')).toHaveCount(200);
  await expect(page.locator('.pane-slot.active .filtered-row.current')).toHaveAttribute(
    'data-line',
    String(targetLine),
  );
  expect(await scroll.evaluate((element) => element.scrollTop)).toBeGreaterThan(0);
});

test('the last result row stays visible at the bottom of the search view', async ({ page }) => {
  test.setTimeout(120_000);
  await installDefaults(page);
  const file = createWideLog(300);
  await openFile(page, file);

  const search = page.locator('.pane-slot.active .search-input');
  await search.fill('INFO');
  await search.press('Enter');
  await expect.poll(() => searchTotals(page)).toEqual({ running: false, searchTotal: 300 });
  await expect(page.locator('.pane-slot.active .filtered-row')).toHaveCount(200);

  const scroll = page.locator('.pane-slot.active .filtered-scroll');
  const bottomRowVisible = async (): Promise<boolean> =>
    page.locator('.pane-slot.active .filtered-row').last().evaluate((element) => {
      const scroller = element.closest('.filtered-scroll');
      if (!scroller) {
        return false;
      }
      const row = element.getBoundingClientRect();
      const box = scroller.getBoundingClientRect();
      // Fully inside the scroll viewport, not merely intersecting it.
      return row.bottom <= box.bottom + 1 && row.top >= box.top - 1;
    });

  await scroll.evaluate((element) => {
    element.scrollTop = element.scrollHeight;
    element.dispatchEvent(new Event('scroll'));
  });
  await expect(page.locator('.pane-slot.active .filtered-row').last()).toHaveAttribute(
    'data-line',
    '299',
  );
  // A horizontal scrollbar that appears with the tail rows shrinks the client
  // height; the anchored scroll must follow or the last row is clipped.
  await expect.poll(bottomRowVisible).toBe(true);

  // Wrapped rows make the window average height inexact; the tail must still
  // end inside the reachable range.
  await page.locator('.pane-slot.active .toolbar button[title="自动折行"]').click();
  await scroll.evaluate((element) => {
    element.scrollTop = element.scrollHeight;
    element.dispatchEvent(new Event('scroll'));
  });
  await expect(page.locator('.pane-slot.active .filtered-row').last()).toHaveAttribute(
    'data-line',
    '299',
  );
  await expect.poll(bottomRowVisible).toBe(true);
});

test('a mark toggle keeps the search view position', async ({ page }) => {
  test.setTimeout(120_000);
  await installDefaults(page);
  const file = createTempLog(600, 1);
  await openFile(page, file);

  const search = page.locator('.pane-slot.active .search-input');
  await search.fill('INFO');
  await search.press('Enter');
  await expect.poll(() => searchTotals(page)).toEqual({ running: false, searchTotal: 600 });
  await expect(page.locator('.pane-slot.active .filtered-row')).toHaveCount(200);

  // Go to the middle of the results and pick a row there.
  const scroll = page.locator('.pane-slot.active .filtered-scroll');
  const firstRowLine = async () =>
    Number(await page.locator('.pane-slot.active .filtered-row').first().getAttribute('data-line'));
  await scroll.evaluate((element) => {
    element.scrollTop = element.scrollHeight / 2;
    element.dispatchEvent(new Event('scroll'));
  });
  await expect.poll(firstRowLine).toBeGreaterThan(200);
  const target = page.locator('.pane-slot.active .filtered-row').nth(5);
  const targetLine = Number(await target.getAttribute('data-line'));
  await target.click();
  await expect(page.locator('.pane-slot.active .filtered-row.current')).toHaveAttribute(
    'data-line',
    String(targetLine),
  );

  // Marking the current row reloads the projection; it must keep the position.
  await page.keyboard.press('m');
  await expect
    .poll(async () => page.locator('.pane-slot.active .filtered-row.current').getAttribute('data-line'))
    .toBe(String(targetLine));
  expect(await scroll.evaluate((element) => element.scrollTop)).toBeGreaterThan(0);
});

test('the main view cursor anchors the next search view reload', async ({ page }) => {
  test.setTimeout(120_000);
  await installDefaults(page);
  const file = createTempLog(600, 1);
  await openFile(page, file);

  const search = page.locator('.pane-slot.active .search-input');
  await search.fill('INFO');
  await search.press('Enter');
  await expect.poll(() => searchTotals(page)).toEqual({ running: false, searchTotal: 600 });
  await expect(page.locator('.pane-slot.active .filtered-row')).toHaveCount(200);

  // Jump far into the file from the main view and click the line that becomes
  // visible: the shared cursor is now that source line.
  const goLine = page.locator('.pane-slot.active .go-line');
  await goLine.fill('400');
  await goLine.press('Enter');
  await expect.poll(async () => (await paneState(page)).firstLine).toBeGreaterThan(300);
  await clickLogRow(page, 0);
  const anchorLine = (await paneState(page)).firstLine;

  // The projection reloads because of the mark; the search view follows the
  // main view's current line (the desktop shares one current line).
  await page.keyboard.press('m');
  await expect
    .poll(async () => page.locator('.pane-slot.active .filtered-row.current').getAttribute('data-line'))
    .toBe(String(anchorLine));
});

test('the bottom scrolls one line past the last row, like the desktop', async ({ page }) => {
  test.setTimeout(120_000);
  await installDefaults(page);
  const file = createTempLog(60, 1);
  await openFile(page, file);

  // Main view: the desktop range ends at totalLines - visibleLines + 1, so the
  // last line can scroll one row up and leaves a blank row at the bottom.
  await clickLogRow(page, 0);
  await page.keyboard.press('End');
  const viewRows = async () =>
    page.evaluate(() => {
      const viewport = (
        window as Window & { __kloggViewport?: { term?: import('xterm').Terminal } }
      ).__kloggViewport;
      const term = viewport?.term;
      const state = (
        window as Window & { __klogg?: { state?: { totalLines?: number; firstLine?: number } } }
      ).__klogg?.state;
      if (!term || !state) {
        throw new Error('viewport missing');
      }
      const rowText = (index: number) =>
        term.buffer.active.getLine(index)?.translateToString(true) ?? '';
      return {
        firstLine: state.firstLine ?? -1,
        totalLines: state.totalLines ?? 0,
        rows: term.rows,
        lastRow: rowText(term.rows - 1).trim(),
        beforeLastRow: rowText(term.rows - 2),
      };
    });
  // The new frame arrives asynchronously; wait for the tail to be painted.
  await expect.poll(async () => (await viewRows()).beforeLastRow).toContain('temp line 60');
  const view = await viewRows();
  expect(view.firstLine).toBe(view.totalLines - view.rows + 1);
  expect(view.lastRow).toBe('');

  // Search view: the same extra row of range below the last result row.
  const search = page.locator('.pane-slot.active .search-input');
  await search.fill('INFO');
  await search.press('Enter');
  await expect.poll(() => searchTotals(page)).toEqual({ running: false, searchTotal: 60 });
  const scroll = page.locator('.pane-slot.active .filtered-scroll');
  await scroll.evaluate((element) => {
    element.scrollTop = element.scrollHeight;
    element.dispatchEvent(new Event('scroll'));
  });
  const lastRow = page.locator('.pane-slot.active .filtered-row').last();
  await expect(lastRow).toHaveAttribute('data-line', '59');
  const gap = await lastRow.evaluate((element) => {
    const scroller = element.closest('.filtered-scroll');
    if (!scroller) {
      return -1;
    }
    return Math.round(
      scroller.getBoundingClientRect().bottom - element.getBoundingClientRect().bottom,
    );
  });
  expect(gap).toBeGreaterThanOrEqual(18);
});

type PaneState = {
  currentLine: number | null;
  firstLine: number;
  selectionRange: string;
  statusMessage: string;
};

function paneState(page: Page): Promise<PaneState> {
  return page.evaluate(
    () => (window as Window & { __klogg?: { state: PaneState } }).__klogg?.state as PaneState,
  );
}

function fontSize(page: Page): Promise<number> {
  return page.evaluate(
    () =>
      (window as Window & { __kloggViewport?: { term?: { options?: { fontSize: number } } } })
        .__kloggViewport?.term?.options?.fontSize ?? 0,
  );
}

function tabTitles(page: Page): Promise<string[]> {
  return page.evaluate(
    () =>
      (window as Window & { __klogg?: { tabs: { path: string }[] } }).__klogg?.tabs.map(
        (tab) => tab.path.split(/[\\/]/).pop() ?? '',
      ) ?? [],
  );
}

async function logRowHeight(page: Page): Promise<number> {
  return page.evaluate(() => {
    const screen = document.querySelector('.pane-slot.active .xterm-screen');
    const viewport = (window as Window & { __kloggViewport?: { term?: { rows: number } } })
      .__kloggViewport;
    const rows = viewport?.term?.rows ?? 1;
    return screen ? screen.getBoundingClientRect().height / rows : 18;
  });
}

/** Clicks a log row by its index in the viewport (0 is the topmost row). */
async function clickLogRow(page: Page, rowIndex: number): Promise<void> {
  const host = page.locator('.pane-slot.active .terminal-host');
  const box = await host.boundingBox();
  expect(box).not.toBeNull();
  const rowHeight = await logRowHeight(page);
  await page.mouse.click(box!.x + 240, box!.y + rowHeight * (rowIndex + 0.5));
}

/** Drags the mouse over a run of log rows, which selects whole source lines. */
async function dragLogRows(page: Page, fromRow: number, toRow: number): Promise<void> {
  const host = page.locator('.pane-slot.active .terminal-host');
  const box = await host.boundingBox();
  expect(box).not.toBeNull();
  const rowHeight = await logRowHeight(page);
  await page.mouse.move(box!.x + 120, box!.y + rowHeight * (fromRow + 0.5));
  await page.mouse.down();
  await page.mouse.move(box!.x + 120, box!.y + rowHeight * (toRow + 0.5), { steps: 12 });
  await page.mouse.up();
}

/**
 * Client coordinates covering a word in the main view. xterm keeps the cell
 * under the pointer out of the selection, so the drag has to leave the word.
 */
async function logWordCells(
  page: Page,
  rowIndex: number,
  word: string,
): Promise<{ startX: number; endX: number; y: number }> {
  return page.evaluate(
    ({ row, word }) => {
      const viewport = (window as Window & { __kloggViewport?: { term?: import('xterm').Terminal } })
        .__kloggViewport;
      const term = viewport?.term;
      const screen = document.querySelector('.pane-slot.active .xterm-screen');
      if (!term || !screen) {
        throw new Error('terminal missing');
      }
      const rect = screen.getBoundingClientRect();
      const text = term.buffer.active.getLine(row)?.translateToString(true) ?? '';
      const index = text.indexOf(word);
      if (index < 0) {
        throw new Error(`word not found in row ${row}: ${word}`);
      }
      const cellWidth = rect.width / term.cols;
      const rowHeight = rect.height / term.rows;
      return {
        startX: rect.left + (index + 0.5) * cellWidth,
        endX: rect.left + (index + word.length + 0.5) * cellWidth,
        y: rect.top + (row + 0.5) * rowHeight,
      };
    },
    { row: rowIndex, word },
  );
}

/** Drives the tab bar's drag handlers without relying on native drag support. */
async function dragTab(page: Page, fromId: string, toId: string, after: boolean): Promise<void> {
  await page.evaluate(
    ({ from, to, after: dropAfter }) => {
      const source = document.querySelector(`.file-tab[data-tab-id="${from}"]`);
      const target = document.querySelector(`.file-tab[data-tab-id="${to}"]`);
      if (!source || !target) {
        throw new Error('tabs not found');
      }
      const data = new DataTransfer();
      const rect = target.getBoundingClientRect();
      const x = dropAfter ? rect.right - 2 : rect.left + 2;
      const y = rect.top + rect.height / 2;
      const fire = (element: Element, type: string): void => {
        element.dispatchEvent(
          new DragEvent(type, {
            bubbles: true,
            cancelable: true,
            dataTransfer: data,
            clientX: x,
            clientY: y,
          }),
        );
      };
      fire(source, 'dragstart');
      fire(target, 'dragover');
      fire(target, 'drop');
      fire(source, 'dragend');
    },
    { from: fromId, to: toId, after },
  );
}

test('navigation shortcuts move the selection and the viewport', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, createTempLog(400));

  // W toggles wrapping.
  await expect(page.locator('.pane-slot.active .viewport')).toHaveClass(/no-wrap/);
  await clickLogRow(page, 10);
  await page.keyboard.press('w');
  await expect(page.locator('.pane-slot.active .viewport')).not.toHaveClass(/no-wrap/);
  await page.keyboard.press('w');
  await expect(page.locator('.pane-slot.active .viewport')).toHaveClass(/no-wrap/);

  // K/J move the selected line by one, like the desktop view's Up/Down.
  expect((await paneState(page)).currentLine).toBe(10);
  await page.keyboard.press('k');
  await expect.poll(async () => (await paneState(page)).currentLine).toBe(9);
  await page.keyboard.press('j');
  await page.keyboard.press('j');
  await expect.poll(async () => (await paneState(page)).currentLine).toBe(11);

  // T jumps back to the beginning of the file.
  const host = page.locator('.pane-slot.active .terminal-host');
  const box = await host.boundingBox();
  await page.mouse.move(box!.x + 400, box!.y + 100);
  for (let index = 0; index < 6; index++) {
    await page.mouse.wheel(0, 240);
    await page.waitForTimeout(30);
  }
  await expect.poll(async () => (await paneState(page)).firstLine).toBeGreaterThan(0);
  await page.keyboard.press('t');
  await expect.poll(async () => (await paneState(page)).firstLine).toBe(0);
});

test('Ctrl+wheel zoom grows the text and tabs can be reordered by drag', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);
  await clickLogRow(page, 1);

  // Wheel up grows the text, wheel down shrinks it (desktop direction).
  const before = await fontSize(page);
  await page.mouse.move(400, 400);
  await page.keyboard.down('Control');
  await page.mouse.wheel(0, -120);
  await expect.poll(() => fontSize(page)).toBe(before + 1);
  await page.mouse.wheel(0, 120);
  await expect.poll(() => fontSize(page)).toBe(before);
  await page.keyboard.up('Control');

  // Zoom keeps going past the old 18 px ceiling and the size is persisted.
  await page.keyboard.down('Control');
  for (let step = 0; step < 10; step++) {
    await page.mouse.wheel(0, -120);
    await page.waitForTimeout(40);
  }
  await page.keyboard.up('Control');
  await expect.poll(() => fontSize(page)).toBeGreaterThan(18);
  await expect
    .poll(async () =>
      page.evaluate(
        () => JSON.parse(localStorage.getItem('klogg.web.settings') ?? '{}').fontSize ?? 0,
      ),
    )
    .toBeGreaterThan(18);

  // Drag the second tab in front of the first one.
  await page.evaluate(
    (target) =>
      (window as Window & { __klogg?: { openPath: (value: string) => void } }).__klogg?.openPath(
        target,
      ),
    betaPath,
  );
  await expect(page.locator('.file-tab')).toHaveCount(2);
  expect(await tabTitles(page)).toEqual(['alpha.log', 'beta.log']);
  const ids = await page.evaluate(
    () =>
      (window as Window & { __klogg?: { tabs: { id: string }[] } }).__klogg?.tabs.map(
        (tab) => tab.id,
      ) ?? [],
  );
  await dragTab(page, ids[1], ids[0], false);
  await expect(page.locator('.file-tab-title')).toHaveText(['beta.log', 'alpha.log']);
  await expect.poll(() => tabTitles(page)).toEqual(['beta.log', 'alpha.log']);
});

test('Ctrl+Shift+C copies the selected line with colors', async ({ page, context }) => {
  await context.grantPermissions(['clipboard-read', 'clipboard-write']);
  await installDefaults(page);
  await openFile(page, alphaPath);

  // The clipboard is shared by the whole system, so drop the rich flavour of
  // whatever a previous test left behind before pressing the shortcut.
  await page.evaluate(() => navigator.clipboard.writeText('klogg-e2e-sentinel'));

  await clickLogRow(page, 0);
  await page.keyboard.press('Control+Shift+C');

  const readClipboard = (): Promise<{ html: string; text: string } | null> =>
    page.evaluate(async () => {
      for (const item of await navigator.clipboard.read()) {
        if (item.types.includes('text/html')) {
          return {
            html: await (await item.getType('text/html')).text(),
            text: await (await item.getType('text/plain')).text(),
          };
        }
      }
      return null;
    });

  let payload = await readClipboard();
  for (let attempt = 0; attempt < 40 && !payload; attempt++) {
    await page.waitForTimeout(100);
    payload = await readClipboard();
  }
  expect(payload).not.toBeNull();
  expect(
    payload!.html.startsWith('<pre style="font-family:monospace; white-space:pre; margin:0">'),
  ).toBe(true);
  expect(payload!.html).not.toContain('\u001b');
  expect(payload!.text).toContain('2026-10-25T10:00:00.000Z INFO');
  expect(payload!.text).not.toContain('\u001b');
});

test('a growing file extends the executed search and keeps follow pinned', async ({ page }) => {
  await installDefaults(page);
  const file = createTempLog(12);
  await openFile(page, file);

  const searchPatterns: string[] = [];
  const requests: string[] = [];
  const extendRequests: string[] = [];
  page.on('request', (request) => {
    const url = request.url();
    if (url.endsWith('/api/search.run')) {
      const body = request.postDataJSON() as { params?: { pattern?: string } };
      searchPatterns.push(body.params?.pattern ?? '');
    } else if (url.endsWith('/api/search.extend')) {
      extendRequests.push(url);
    } else if (url.endsWith('/api/file.reload') || url.endsWith('/api/file.checkChanges')) {
      requests.push(url.split('/api/')[1]);
    }
  });

  const search = page.locator('.pane-slot.active .search-input');
  await search.fill('INFO');
  await search.press('Enter');
  await expect(page.locator('.pane-slot.active .filtered-row')).toHaveCount(12);

  const follow = page.locator('.pane-slot.active .follow-toggle');
  await follow.click();
  await expect(follow).toHaveClass(/active/);

  // The file grows while the user edits the pattern without running it again.
  const growth = setInterval(() => {
    fs.appendFileSync(file, `${new Date().toISOString()} INFO live line\n`, 'utf8');
  }, 120);
  try {
    await search.click();
    await page.keyboard.type('WARN', { delay: 60 });
    await page.waitForTimeout(2500);
  } finally {
    clearInterval(growth);
  }

  // Growth uses the incremental check, not a full reload.
  expect(requests).toContain('file.checkChanges');
  expect(requests).not.toContain('file.reload');
  // Growth extends the same SearchId instead of starting a fresh search.
  await expect.poll(() => extendRequests.length).toBeGreaterThan(0);
  expect(searchPatterns).toEqual(['INFO']);
  // Nothing searched for the half typed pattern, and the input kept the text.
  await expect(search).toHaveValue('INFOWARN');

  const totalLines = fs.readFileSync(file, 'utf8').trimEnd().split('\n').length;
  await expect
    .poll(async () => {
      const last = page.locator('.pane-slot.active .filtered-row').last();
      return (await last.count()) > 0 ? Number(await last.getAttribute('data-line')) : -1;
    })
    .toBe(totalLines - 1);
});

test('filtered projection keeps a capped scrollbar for 400k matches', async ({ page }) => {
  test.setTimeout(120_000);
  await installDefaults(page);
  const lineCount = 420_000;
  const file = createTempLog(lineCount, 1);
  await openFile(page, file);

  const search = page.locator('.pane-slot.active .search-input');
  await search.fill('INFO');
  await search.press('Enter');

  const liveState = () =>
    page.evaluate(() => {
      const state = (
        window as Window & {
          __klogg?: {
            state?: { searchRunning?: boolean; totalLines?: number; searchTotal?: number };
          };
        }
      ).__klogg?.state;
      return {
        running: !!state?.searchRunning,
        totalLines: state?.totalLines ?? 0,
        searchTotal: state?.searchTotal ?? 0,
      };
    });
  await expect.poll(liveState).toEqual({
    running: false,
    totalLines: lineCount,
    searchTotal: lineCount,
  });

  const scroll = page.locator('.pane-slot.active .filtered-scroll');
  const scrollHeight = await scroll.evaluate((element) => element.scrollHeight);
  expect(scrollHeight).toBeGreaterThan(7_000_000);
  expect(scrollHeight).toBeLessThanOrEqual(8_100_000);

  await scroll.evaluate((element) => {
    element.scrollTop = Math.round(element.scrollHeight / 2);
    element.dispatchEvent(new Event('scroll'));
  });
  await page.waitForTimeout(700);
  const middleLine = Number(
    await page.locator('.pane-slot.active .filtered-row').first().getAttribute('data-line'),
  );
  expect(middleLine).toBeGreaterThan(180_000);
  expect(middleLine).toBeLessThan(240_000);

  await scroll.evaluate((element) => {
    element.scrollTop = element.scrollHeight;
    element.dispatchEvent(new Event('scroll'));
  });
  await expect
    .poll(async () => {
      const last = page.locator('.pane-slot.active .filtered-row').last();
      return (await last.count()) > 0 ? Number(await last.getAttribute('data-line')) : -1;
    })
    .toBe(lineCount - 1);

  await page.locator('.pane-slot.active .follow-toggle').click();
  await expect
    .poll(async () => {
      const last = page.locator('.pane-slot.active .filtered-row').last();
      return (await last.count()) > 0 ? Number(await last.getAttribute('data-line')) : -1;
    })
    .toBe(lineCount - 1);
});

test('truncating the file refreshes the live search to the new geometry', async ({ page }) => {
  await installDefaults(page);
  const file = createTempLog(50);
  await openFile(page, file);

  const search = page.locator('.pane-slot.active .search-input');
  await search.fill('INFO');
  await search.press('Enter');
  await expect(page.locator('.pane-slot.active .filtered-row')).toHaveCount(50);

  const liveState = () =>
    page.evaluate(() => {
      const state = (
        window as Window & {
          __klogg?: { state?: { totalLines?: number; searchTotal?: number } };
        }
      ).__klogg?.state;
      return {
        totalLines: state?.totalLines ?? 0,
        searchTotal: state?.searchTotal ?? 0,
      };
    });

  fs.writeFileSync(
    file,
    '2026-10-25T10:00:00.000Z INFO rewritten one\n2026-10-25T10:00:01.000Z INFO rewritten two\n',
    'utf8',
  );
  await expect.poll(liveState).toEqual({ totalLines: 2, searchTotal: 2 });
  await expect(page.locator('.pane-slot.active .filtered-row')).toHaveCount(2);
  const rewrittenLines = await page
    .locator('.pane-slot.active .filtered-row')
    .evaluateAll((rows) => rows.map((row) => Number((row as HTMLElement).dataset.line)));
  expect(rewrittenLines).toEqual([0, 1]);

  fs.appendFileSync(
    file,
    '2026-10-25T10:00:02.000Z INFO grown three\n' +
      '2026-10-25T10:00:03.000Z INFO grown four\n' +
      '2026-10-25T10:00:04.000Z INFO grown five\n',
    'utf8',
  );
  await expect.poll(liveState).toEqual({ totalLines: 5, searchTotal: 5 });
  await expect(page.locator('.pane-slot.active .filtered-row')).toHaveCount(5);
});

test('the color label menu clears one word and all labels', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);
  const search = page.locator('.pane-slot.active .search-input');
  await search.fill('INFO');
  await search.press('Enter');
  await expect(page.locator('.filtered-row')).toHaveCount(8);

  const box = await page.locator('.terminal-host').boundingBox();
  const labelPoint = { x: box!.x + 240, y: box!.y + 10 };
  const openLabels = async (): Promise<void> => {
    await page.mouse.click(labelPoint.x, labelPoint.y, { button: 'right' });
    await expect(page.locator('.context-grid-title')).toHaveText('颜色标签');
  };
  const labels = page.locator('.filtered-text .color-label');

  // Whole line selection, then apply label 1 from the context menu.
  await clickLogRow(page, 0);
  await openLabels();
  await page.locator('.context-grid-cell').nth(1).click();
  await expect(labels.first()).toBeVisible();

  // "None" (the first cell) removes the word from every slot.
  await openLabels();
  await page.locator('.context-grid-cell').first().click();
  await expect(labels).toHaveCount(0);

  // Apply again, then "Clear all" (the last cell) empties every slot.
  await openLabels();
  await page.locator('.context-grid-cell').nth(2).click();
  await expect(labels.first()).toBeVisible();
  await openLabels();
  await page.locator('.context-grid-cell').last().click();
  await expect(labels).toHaveCount(0);
});

test('the F10 follow binding and the focus release cover the whole pane', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  const follow = page.locator('.pane-slot.active .follow-toggle');
  await clickLogRow(page, 1);
  await page.keyboard.press('F10');
  await expect(follow).toHaveClass(/active/);
  await page.keyboard.press('F10');
  await expect(follow).not.toHaveClass(/active/);

  // Clicking the splitter or the overview releases the search box focus.
  const search = page.locator('.pane-slot.active .search-input');
  const isSearchFocused = (): Promise<boolean> =>
    page.evaluate(() => document.activeElement?.classList.contains('search-input') ?? false);

  await search.click();
  await expect.poll(isSearchFocused).toBe(true);
  const handle = await page.locator('.split-handle').boundingBox();
  await page.mouse.click(handle!.x + handle!.width / 2, handle!.y + handle!.height / 2);
  await expect.poll(isSearchFocused).toBe(false);

  await search.click();
  const overview = await page.locator('.overview, .overview-widget, canvas').first().boundingBox();
  await page.mouse.click(overview!.x + overview!.width / 2, overview!.y + overview!.height / 2);
  await expect.poll(isSearchFocused).toBe(false);
});

test('the selection interval badge follows the selected lines', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, createTempLog(30, 1000));

  // A single line has no interval to show.
  await clickLogRow(page, 0);
  await expect(page.locator('.pane-slot.active .selection-time')).toHaveCount(0);

  // Four lines, one second apart: the badge shows the distance between the ends.
  await dragLogRows(page, 0, 3);
  await expect(page.locator('.pane-slot.active .selection-time')).toHaveAttribute(
    'data-label',
    '3 s',
  );

  // A burst of records written in the same millisecond is a zero interval and
  // must still show, otherwise the badge looks broken on real logs.
  await page.evaluate(
    (target) =>
      (window as Window & { __klogg?: { openPath: (value: string) => void } }).__klogg?.openPath(
        target,
      ),
    createTempLog(30, 0),
  );
  await expect(page.locator('.file-tab')).toHaveCount(2);
  await expect(page.locator('.pane-slot.active .terminal-host')).toBeVisible();
  await dragLogRows(page, 0, 3);
  await expect(page.locator('.pane-slot.active .selection-time')).toHaveAttribute(
    'data-label',
    '0 s',
  );
});

test('a double clicked word outranks a color label and clears on the next click', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);
  const box = await page.locator('.terminal-host').boundingBox();
  const rowHeight = await logRowHeight(page);
  const firstRow = { x: box!.x + 240, y: box!.y + rowHeight * 0.5 };

  // Label the first line from the main view.
  await page.mouse.click(firstRow.x, firstRow.y);
  await page.mouse.click(firstRow.x, firstRow.y, { button: 'right' });
  await expect(page.locator('.context-grid-title')).toHaveText('颜色标签');
  await page.locator('.context-grid-cell').nth(1).click();
  await expect(page.locator('.pane-slot.active .toolbar-message')).toContainText('颜色标签');

  // Double click "INFO" on the labelled line. The overlay carries the word text
  // and a solid colour, so the label colour cannot show through.
  const point = await page.evaluate(() => {
    const viewport = (window as Window & { __kloggViewport?: { term?: import('xterm').Terminal } })
      .__kloggViewport;
    const term = viewport?.term;
    const screen = document.querySelector('.pane-slot.active .xterm-screen');
    if (!term || !screen) {
      throw new Error('terminal missing');
    }
    const rect = screen.getBoundingClientRect();
    const text = term.buffer.active.getLine(0)?.translateToString(true) ?? '';
    const index = text.indexOf('INFO');
    const cellWidth = rect.width / term.cols;
    const rowHeightPx = rect.height / term.rows;
    return { x: rect.left + index * cellWidth + 2, y: rect.top + rowHeightPx / 2 };
  });
  await page.mouse.dblclick(point.x, point.y);

  const overlay = page.locator('.word-hit-overlay').first();
  await expect(overlay).toBeVisible();
  expect(await overlay.textContent()).toBe('INFO');
  const background = await overlay.evaluate((element) => getComputedStyle(element).backgroundColor);
  expect(background.startsWith('rgb(')).toBe(true);

  // A right press keeps the marker: the context menu acts on the selection the
  // marker belongs to, so dropping it there would lose that selection.
  await page.mouse.click(point.x, point.y, { button: 'right' });
  await expect(page.locator('.context-grid-title')).toBeVisible();
  await page.keyboard.press('Escape');
  await expect(page.locator('.word-hit-overlay').first()).toBeVisible();

  // A left click in the pane drops the marker.
  await page.mouse.click(box!.x + 600, box!.y + rowHeight * 5.5);
  await expect(page.locator('.word-hit-overlay')).toHaveCount(0);
});

test('a dragged within-row selection highlights its matches in the main view', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);
  const cells = await logWordCells(page, 0, 'INFO');

  await page.mouse.move(cells.startX, cells.y);
  await page.mouse.down();
  await page.mouse.move(cells.endX, cells.y, { steps: 8 });
  await page.mouse.up();

  const overlay = page.locator('.word-hit-overlay').first();
  await expect(overlay).toBeVisible();
  expect(await overlay.textContent()).toBe('INFO');
  expect(
    await page.evaluate(
      () =>
        (window as Window & { __kloggViewport?: { term?: { getSelection(): string } } })
          .__kloggViewport?.term?.getSelection() ?? '',
    ),
  ).toBe('INFO');
});

test('a row drag leaves no word highlight behind', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  await dragLogRows(page, 0, 3);
  // The drag was a row range, so the toolbar reports it and no word was picked.
  await expect(page.locator('.pane-slot.active .toolbar-info.selection')).toBeVisible();
  await expect(page.locator('.word-hit-overlay')).toHaveCount(0);
});

test('an unwrapped long line scrolls horizontally to its end', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, createLongLineLog(6000));

  // The desktop view sizes its horizontal scroll bar from the longest line; the
  // fixed 4000 column cap used to truncate longer lines.
  const slider = page.locator('.pane-slot.active .hscroll input[type=range]');
  await expect(slider).toBeVisible();
  expect(Number(await slider.getAttribute('max'))).toBeGreaterThan(4000);

  await page.evaluate(() => {
    const range = document.querySelector<HTMLInputElement>('.pane-slot.active .hscroll input');
    if (!range) {
      throw new Error('horizontal slider missing');
    }
    range.value = range.max;
    range.dispatchEvent(new Event('input', { bubbles: true }));
  });

  const longLineText = (): Promise<string> =>
    page.evaluate(() => {
      const term = (window as Window & { __kloggViewport?: { term?: import('xterm').Terminal } })
        .__kloggViewport?.term;
      const line = term?.buffer.active.getLine(1);
      return line ? line.translateToString(true) : '';
    });
  await expect.poll(longLineText).toContain('TAIL-END');
});

test('Ctrl+A selects the whole file in the main view', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, createTempLog(400));

  await clickLogRow(page, 5);
  await page.keyboard.press('Control+a');
  await expect.poll(async () => (await paneState(page)).selectionRange).toBe('选中 400 行');
});

test('Ctrl+A selects every match in the search view', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, createTempLog(400));
  const search = page.locator('.pane-slot.active .search-input');
  await search.fill('INFO');
  await search.press('Enter');
  await expect(page.locator('.filtered-row').first()).toBeVisible();

  await page.locator('.filtered-row').nth(2).click();
  await page.keyboard.press('Control+a');
  await expect.poll(async () => (await paneState(page)).selectionRange).toBe('选中 400 行');
});

test('Ctrl+A inside the search box selects the query text', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, createTempLog(20));
  const search = page.locator('.pane-slot.active .search-input');
  await search.fill('INFO');
  await search.press('Enter');
  await page.waitForTimeout(300);

  await search.click();
  await page.keyboard.press('Control+a');
  const selected = await page.evaluate(() => {
    const input = document.querySelector<HTMLInputElement>('.pane-slot.active .search-input');
    return input ? input.value.slice(input.selectionStart ?? 0, input.selectionEnd ?? 0) : '';
  });
  expect(selected).toBe('INFO');
  // The log selection is untouched by the input's own select-all.
  expect((await paneState(page)).selectionRange).toBe('');
});

test('copy with colors keeps every line of a whole-file selection', async ({ page, context, request }) => {
  await context.grantPermissions(['clipboard-read', 'clipboard-write']);
  await installDefaults(page);
  const total = 12000;
  await openFile(page, createTempLog(total));
  await clickLogRow(page, 3);

  const chunks: number[] = [];
  page.on('request', (event) => {
    if (event.url().endsWith('/api/render.linesHtml')) {
      const body = event.postDataJSON() as { lines?: number[] };
      chunks.push(body.lines?.length ?? 0);
    }
  });

  await page.keyboard.press('Control+a');
  await page.evaluate(() => navigator.clipboard.writeText('sentinel'));
  await page.keyboard.press('Control+Shift+C');

  await expect.poll(async () => chunks.reduce((sum, count) => sum + count, 0), { timeout: 30000 }).toBe(total);
  expect(chunks.length).toBeGreaterThan(1);

  const payload = await page.evaluate(async () => {
    for (const item of await navigator.clipboard.read()) {
      if (item.types.includes('text/html')) {
        return {
          html: await (await item.getType('text/html')).text(),
          text: await (await item.getType('text/plain')).text(),
        };
      }
    }
    return null;
  });
  expect(payload).not.toBeNull();
  expect(payload!.text.split('\n').length).toBe(total);
  expect(payload!.html.split('<pre').length - 1).toBe(1);
  expect(payload!.html).toContain('INFO temp line 12000');
  expect(payload!.html).toContain('INFO temp line 1 worker');
  void request;
});

test('marking a whole-file selection marks every line', async ({ page, request }) => {
  await installDefaults(page);
  const total = 12000;
  const file = createTempLog(total);
  await openFile(page, file);
  await clickLogRow(page, 3);

  const chunks: number[] = [];
  page.on('request', (event) => {
    if (event.url().endsWith('/api/marks.add')) {
      const body = event.postDataJSON() as { lines?: number[] };
      chunks.push(body.lines?.length ?? 0);
    }
  });

  await page.keyboard.press('Control+a');
  await page.keyboard.press('m');

  await expect.poll(async () => chunks.reduce((sum, count) => sum + count, 0), { timeout: 30000 }).toBe(total);
  expect(chunks.length).toBeGreaterThan(1);

  // The engine really holds all of them, not just the last chunk.
  const connected = await request.post('/api/session.connect', {
    data: { name: 'e2e-marks', role: 'human' },
  });
  const clientId = (await connected.json()).value.clientId as string;
  const opened = await request.post('/api/file.open', {
    headers: { 'X-Klogg-Client-Id': clientId },
    data: { path: file },
  });
  const fileId = (await opened.json()).value.fileId as number;
  const listed = await request.post('/api/marks.list', {
    headers: { 'X-Klogg-Client-Id': clientId },
    data: { fileId },
  });
  expect((await listed.json()).value.marks.length).toBe(total);
  for (let from = 0; from < total; from += 5000) {
    await request.post('/api/marks.remove', {
      headers: { 'X-Klogg-Client-Id': clientId },
      data: {
        fileId,
        lines: Array.from({ length: Math.min(5000, total - from) }, (_, index) => from + index),
      },
    });
  }
});

test('the settings dialog shows two shortcut slots with the single key first', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  await page.locator('.settings-button:not(.theme-toggle)').click();
  await page.locator('.settings-tabs button').filter({ hasText: '快捷键' }).click();

  const nextMatch = page.locator('.shortcut-row').filter({ hasText: '下一个匹配' });
  const inputs = nextMatch.locator('.shortcut-input');
  await expect(inputs).toHaveCount(2);
  await expect(inputs.nth(0)).toHaveValue('N');
  await expect(inputs.nth(1)).toHaveValue('F3');

  const goTop = page.locator('.shortcut-row').filter({ hasText: '跳到文件顶部' });
  await expect(goTop.locator('.shortcut-input')).toHaveCount(2);
  await expect(goTop.locator('.shortcut-input').nth(0)).toHaveValue('T');
  await expect(goTop.locator('.shortcut-input').nth(1)).toHaveValue('Ctrl+Home');
});

test('toolbar buttons run their action without event arguments', async ({ page }) => {
  await installDefaults(page);
  await openFile(page, alphaPath);

  // A click used to pass the event as the first argument, which turned the
  // reload into the silent variant and left the status line empty.
  await page.locator('.pane-slot.active .toolbar button[title="重载"]').click();
  await expect(page.locator('.pane-slot.active .toolbar-message')).toContainText('重载');
});
