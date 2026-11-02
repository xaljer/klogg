# klogg Electron (Windows)

Windows x64 Electron shell around the existing Web UI and the zero-Qt
`klogg_server`.

## Architecture

```text
Electron main process
  ├─ one klogg_server.exe sidecar
  �?   normal mode: 127.0.0.1:<ephemeral>, no token
  �?   server mode: <listenAddress>:<port>, bearer token
  └─ BrowserWindow w1, w2, ...
       http://127.0.0.1:<port>/?window=wN[&token=...]
```

- Settings, favorites, recent files and search history are shared by all
  windows through the Electron session.
- Tabs, active tab, search state and scroll state are scoped by the `window`
  query parameter (`klogg.web.session.wN`).
- The sidecar is never duplicated. Local windows and, in server mode, remote
  browsers all use the same `Session`.
- Window ids and geometry are stored in Electron's `userData` directory, which
  packaged builds keep next to the executable (see [Packaging](#packaging)).

## Build

Build the Web UI and the Qt-free server first:

```powershell
cd src/shell/web
npm ci
npm run build

cd ../..
cmake --preset server
cmake --build out/server --target klogg_server --parallel 2
```

Then:

```powershell
cd src/shell/electron
npm ci
npm test
npm start          # development, uses out/server/output + out/web
npm run pack       # out/release/electron/win-unpacked
npm run dist       # NSIS installer
npm run dist:zip   # portable zip
```

`KLOGG_SERVER_BIN` selects a different server executable when preparing
resources. If the default downloads are unavailable, set both mirrors:

```powershell
$env:ELECTRON_MIRROR = 'https://cdn.npmmirror.com/binaries/electron/'
$env:ELECTRON_BUILDER_BINARIES_MIRROR = 'https://cdn.npmmirror.com/binaries/electron-builder-binaries/'
```

## Server mode

Enable **Server Mode** from the tray icon menu. The same sidecar restarts
with:

```json
{
  "serverMode": true,
  "listenAddress": "0.0.0.0",
  "port": 8080,
  "token": "<generated token>"
}
```

The settings file is `desktop-settings.json` under Electron's `userData`
directory. **Show Server Address�?* in the tray menu displays the local and
external URLs and **Copy Server Address** copies the external URL. Remote
clients must append the token as `?token=...`; the Web UI sends it as an
`Authorization: Bearer` header for RPC and as a query parameter for SSE.

Server mode is one sidecar, not a second server. A standalone
`klogg_server.exe` started separately is a different process/session.

## Tests

```powershell
npm test
```

The helper tests cover server URL parsing, argument construction, token
redaction, window URLs, persisted window state, resource paths, desktop
settings and the portable user-data directory. The server-manager test starts
the real `out/server/output` binary on an ephemeral port, checks `/health`, and
verifies that stop releases the process.

Optional packaged-app checks use Playwright from `src/shell/web` (see tests/e2e/README.md):

```powershell
$env:NODE_PATH = "D:\workspace\klogg\src\web\node_modules"
node tests/e2e/electron-ui-smoke.cjs
node tests/e2e/electron-smoke.cjs
node tests/e2e/measure-performance.cjs
node tests/e2e/measure-web.cjs # needs a server at 127.0.0.1:8080
```

Both smoke scripts use a temporary `KLOGG_DESKTOP_USER_DATA` directory and
accept `KLOGG_ELECTRON_BIN` to point at a packaged executable.

## Packaging

`electron-builder.yml` includes only:

- `electron/**/*`
- `package.json`
- `../../../out/electron/resources/server/` (server, TBB and the Visual C++ runtime DLLs)
- `../../../out/web`
- `zh-CN` and `en-US` Electron locales

It does not include tests, source maps, Qt, build trees, or development
dependencies. Packages are unsigned.

`npm run dist` builds the NSIS installer (`klogg-<version>-setup.exe`); `npm run dist:zip`
adds a zip of the unpacked app (`klogg-<version>-win.zip`) that runs without installation -
unblock it if Windows marks the download as unsafe, then start `klogg.exe` from the
extracted folder.

### Visual C++ runtime

`klogg_server.exe` links the MSVC C++ runtime dynamically, so
`prepare-resources` copies `msvcp140.dll`, `vcruntime140.dll` and
`vcruntime140_1.dll` from the installed Visual Studio redistributable directory
next to the server. Set `KLOGG_VC_RUNTIME_DIR` to an explicit x64
`Microsoft.VC*.CRT` directory when Visual Studio cannot be discovered.

### User data

Packaged builds keep settings, window state and the server log under a
`userdata` folder next to the executable, so a copied folder carries its own
data. Set `KLOGG_DESKTOP_USER_DATA` to override the
directory. If the executable directory is not writable (for example a Program
Files install), the app falls back to Electron's per-user data directory.
