import path from 'node:path';

import { defineConfig } from '@playwright/test';

const webRoot = process.cwd();
const fixtureRoot = path.resolve(webRoot, 'e2e/fixtures');
const distRoot =
  process.env.KLOGG_WEB_ROOT ?? path.resolve(webRoot, '../../../out/web');
const port = process.env.KLOGG_E2E_PORT ?? '18080';
const channel = process.env.PLAYWRIGHT_CHANNEL;
const serverBin =
  process.env.KLOGG_SERVER_BIN ??
  path.resolve(
    webRoot,
    '../../../out/server/output',
    process.platform === 'win32' ? 'klogg_server.exe' : 'klogg_server',
  );

const serverCommand = `"${serverBin}" --port ${port} --listen 127.0.0.1 --root "${fixtureRoot}" --web-root "${distRoot}"`;

export default defineConfig({
  testDir: './e2e',
  testMatch: '*.spec.ts',
  fullyParallel: false,
  workers: 1,
  timeout: 30_000,
  expect: {
    timeout: 10_000,
  },
  reporter: [['list'], ['html', { open: 'never' }]],
  use: {
    baseURL: `http://127.0.0.1:${port}`,
    ...(channel ? { channel } : {}),
    trace: 'retain-on-failure',
    screenshot: 'only-on-failure',
  },
  webServer: {
    command: serverCommand,
    url: `http://127.0.0.1:${port}/health`,
    reuseExistingServer: !process.env.CI,
    timeout: 120_000,
    stdout: 'pipe',
    stderr: 'pipe',
  },
});
