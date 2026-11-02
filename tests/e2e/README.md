# Electron end-to-end scripts

These scripts drive the packaged Windows app with Playwright's Electron support.
They are not part of `npm test` (which only runs the unit tests); run them by hand
after packaging:

```bat
build\package\..\..\src\shell\electron\npm run dist    REM or: npm run dist in src/shell/electron
```

## Prerequisites

- A packaged app in `out/release/electron/win-unpacked` (`npm run pack` is enough,
  the installer is not needed).
- `playwright` is installed in the web shell package (`src/shell/web/node_modules`).
- Fixtures under `out/testdata` — generate them with
  `python tests/tools/gen_test_data.py [--big]`.

## Running

```powershell
$env:NODE_PATH = "D:\workspace\klogg\src\shell\web\node_modules"
node tests\e2e\electron-smoke.cjs
```

Two environment quirks on locked-down or already-sandboxed machines:

- `ELECTRON_DISABLE_SANDBOX=1` is required when the parent process is itself
  sandboxed: Chromium cannot create a nested sandbox and exits with `0x80000003`.
- `TEMP`/`TMP` must point at a directory the Chromium renderer can write to
  (it runs at low integrity). Pointing them at a path inside the repository avoids
  "Unable to create cache" errors.
- `ELECTRON_RUN_AS_NODE` must not be set, otherwise Electron starts as plain Node.

## Outputs
Screenshots and temporary browser profiles are written to `out/tmp/` - never to
the source tree - so a run leaves the repository clean.

## Scripts

| Script | What it checks |
|---|---|
| `electron-smoke.cjs` | Server mode: token in the URL, two windows, state restore |
| `electron-ui-smoke.cjs` | Packaged UI opens a log and runs a search |
| `electron-session-smoke.cjs` | Session restore across restarts (needs `recorder_demo.log`) |
| `electron-browse-smoke.cjs` | File browser lists and opens a directory |
| `electron-theme-smoke.cjs`, `electron-500mb-smoke.cjs` | Theme switch / large file open |
| `measure-web.cjs`, `measure-performance.cjs` | Wall-clock and FPS measurements |
| `measure-*.cjs`, `process-tree.ps1` | Helpers for the measurements |
