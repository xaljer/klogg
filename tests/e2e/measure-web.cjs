const path = require('node:path');
const { chromium } = require('playwright');
const url = process.env.KLOGG_WEB_URL || 'http://127.0.0.1:8080/';
const file = process.env.KLOGG_PERF_FILE || path.resolve(__dirname, '..', '..', 'out', 'testdata', 'random_block_500Mb.txt');

(async () => {
  const browser = await chromium.launch({ channel: process.env.PLAYWRIGHT_CHANNEL || 'msedge', headless: true });
  const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
  await page.goto(`${url}?file=${encodeURIComponent(file)}`);
  await page.locator('.file-tab-title').filter({ hasText: 'random_block_500Mb.txt' }).waitFor({ timeout: 60000 });
  await page.locator('.terminal-host').waitFor({ state: 'visible', timeout: 30000 });
  await page.waitForTimeout(3000);
  await page.evaluate(() => {
    window.__kloggFrames = [];
    window.__kloggMeasure = true;
    const tick = (time) => { if (!window.__kloggMeasure) return; window.__kloggFrames.push(time); requestAnimationFrame(tick); };
    requestAnimationFrame(tick);
  });
  const box = await page.locator('.terminal-host').boundingBox();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  const deadline = Date.now() + 1600;
  while (Date.now() < deadline) { await page.mouse.wheel(0, 120); await page.waitForTimeout(16); }
  const frames = await page.evaluate(() => { window.__kloggMeasure = false; return window.__kloggFrames; });
  const span = frames[frames.length - 1] - frames[0];
  console.log(JSON.stringify({ frames: frames.length, fps: Number((((frames.length - 1) * 1000) / span).toFixed(1)), averageIntervalMs: Number((span / (frames.length - 1)).toFixed(2)) }, null, 2));
  await browser.close();
})().catch((error) => { console.error(error); process.exit(1); });
