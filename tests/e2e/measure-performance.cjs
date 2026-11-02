const { execFileSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { _electron: electron } = require('playwright');

const appExe =
  process.env.KLOGG_ELECTRON_BIN ||
  path.resolve(__dirname, '..', '..', 'out', 'release', 'electron', 'win-unpacked', 'klogg.exe');
const perfFile =
  process.env.KLOGG_PERF_FILE ||
  path.resolve(__dirname, '..', '..', 'out', 'testdata', 'random_block_500Mb.txt');
const userData = fs.mkdtempSync(path.join(os.tmpdir(), 'klogg-electron-perf-'));
const treeScript = path.join(__dirname, 'process-tree.ps1');

function memoryOf(rootPid) {
  const output = execFileSync(
    'powershell.exe',
    ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', treeScript, '-RootPid', String(rootPid)],
    { encoding: 'utf8' },
  );
  return JSON.parse(output);
}

async function measureScroll(page, milliseconds = 1600) {
  await page.evaluate(() => {
    window.__kloggFrames = [];
    window.__kloggMeasure = true;
    const tick = (time) => {
      if (!window.__kloggMeasure) {
        return;
      }
      window.__kloggFrames.push(time);
      requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
  });
  const box = await page.locator('.terminal-host').boundingBox();
  if (!box) {
    throw new Error('terminal-host has no bounding box');
  }
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  const deadline = Date.now() + milliseconds;
  while (Date.now() < deadline) {
    await page.mouse.wheel(0, 120);
    await page.waitForTimeout(16);
  }
  const frames = await page.evaluate(() => {
    window.__kloggMeasure = false;
    return window.__kloggFrames;
  });
  if (frames.length < 2) {
    return { frames: frames.length, fps: 0, averageIntervalMs: 0 };
  }
  const span = frames[frames.length - 1] - frames[0];
  return {
    frames: frames.length,
    fps: Number((((frames.length - 1) * 1000) / span).toFixed(1)),
    averageIntervalMs: Number((span / (frames.length - 1)).toFixed(2)),
  };
}

(async () => {
  const started = Date.now();
  const app = await electron.launch({
    executablePath: appExe,
    args: [perfFile],
    env: { ...process.env, KLOGG_DESKTOP_USER_DATA: userData },
    timeout: 60000,
  });
  const page = await app.firstWindow({ timeout: 30000 });
  const startupMs = Date.now() - started;
  await page.locator('.file-tab-title').filter({ hasText: 'random_block_500Mb.txt' }).waitFor({
    timeout: 60000,
  });
  await page.locator('.terminal-host').waitFor({ state: 'visible', timeout: 30000 });
  await page.waitForTimeout(3000);

  const oneWindow = memoryOf(app.process().pid);
  const scroll = await measureScroll(page);

  const secondPromise = app.waitForEvent('window', { timeout: 15000 });
  await page.evaluate(() => window.open(window.location.href, '_blank', 'noopener'));
  const second = await secondPromise;
  await second.locator('.file-tab-title').filter({ hasText: 'random_block_500Mb.txt' }).waitFor({
    timeout: 60000,
  });
  await second.waitForTimeout(2000);
  const twoWindows = memoryOf(app.process().pid);

  console.log(
    JSON.stringify(
      {
        startupMs,
        oneWindow,
        twoWindows,
        scroll,
        file: perfFile,
      },
      null,
      2,
    ),
  );

  await app.evaluate(({ app: electronApp }) => electronApp.quit());
  await app.close().catch(() => {});
  fs.rmSync(userData, { recursive: true, force: true });
})().catch((error) => {
  fs.rmSync(userData, { recursive: true, force: true });
  console.error(error);
  process.exit(1);
});
