const {
  app,
  BrowserWindow,
  Menu,
  Tray,
  clipboard,
  dialog,
  ipcMain,
  nativeImage,
  screen,
  shell,
} = require('electron');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { resolveResources } = require('./paths.cjs');
const { resolveUserDataDir } = require('./user-data.cjs');
const {
  defaultDesktopSettings,
  loadDesktopSettings,
  saveDesktopSettings,
} = require('./settings.cjs');
const { createServerManager, localServerUrl } = require('./server.cjs');
const { registerFileDialogs } = require('./file-dialogs.cjs');
const { buildWindowUrl, nextWindowId } = require('./window-url.cjs');
const { loadWindowState, saveWindowState } = require('./window-state.cjs');

const userDataDir = resolveUserDataDir({
  isPackaged: app.isPackaged,
  exePath: process.execPath,
});
if (userDataDir) {
  try {
    fs.mkdirSync(userDataDir, { recursive: true });
    fs.accessSync(userDataDir, fs.constants.W_OK);
    app.setPath('userData', userDataDir);
    app.setPath('sessionData', path.join(userDataDir, 'session'));
  } catch (error) {
    console.warn(`cannot use portable user data directory ${userDataDir}: ${error.message}`);
  }
}

const gotSingleInstanceLock = app.requestSingleInstanceLock();
if (!gotSingleInstanceLock) {
  app.quit();
}

app.commandLine.appendSwitch('disable-features', 'Translate,MediaRouter');

let settings = defaultDesktopSettings();
let settingsPath = '';
let windowsPath = '';
let serverLogPath = '';
let resources = null;
let serverManager = null;
let serverUrl = '';
let localBase = '';
let serverReady = false;
let restarting = false;
let quitting = false;
let winState = { windows: [] };
let saveTimer = null;
let tray = null;
let lastDialogDirectory = '';

registerFileDialogs({
  ipcMain,
  dialog,
  BrowserWindow,
  shell,
  text,
  getHomeDirectory: () => app.getPath('home'),
  getLastDirectory: () => lastDialogDirectory,
  setLastDirectory: (directory) => {
    lastDialogDirectory = directory;
  },
});

// The main process can read the clipboard without the renderer's permission and
// focus restrictions, which matters for the Ctrl+V "open from clipboard" action.
ipcMain.handle('klogg:clipboard:read', () => clipboard.readText());

const windows = new Map();
const pendingFiles = [];

function isChinese() {
  return app.getLocale().toLowerCase().startsWith('zh');
}

function text(en, zh) {
  return isChinese() ? zh : en;
}

function saveWindowStateNow() {
  if (!windowsPath) {
    return;
  }
  try {
    saveWindowState(windowsPath, winState);
  } catch (error) {
    console.error('cannot save window state', error);
  }
}

function scheduleWindowStateSave() {
  if (saveTimer) {
    clearTimeout(saveTimer);
  }
  saveTimer = setTimeout(() => {
    saveTimer = null;
    saveWindowStateNow();
  }, 250);
}

function findWindowState(id) {
  return winState.windows.find((entry) => entry.id === id);
}

function updateWindowState(win, id) {
  const entry = findWindowState(id);
  if (!entry || win.isDestroyed()) {
    return;
  }
  entry.bounds = win.isMaximized() ? win.getNormalBounds() : win.getBounds();
  entry.maximized = win.isMaximized();
  scheduleWindowStateSave();
}

function addWindowState(id, bounds) {
  if (findWindowState(id)) {
    return;
  }
  winState.windows.push({
    id,
    bounds,
    maximized: false,
  });
}

function defaultBounds() {
  const area = screen.getPrimaryDisplay().workArea;
  const width = Math.min(1280, Math.max(720, area.width - 80));
  const height = Math.min(900, Math.max(520, area.height - 80));
  return {
    x: area.x + Math.round((area.width - width) / 2),
    y: area.y + Math.round((area.height - height) / 2),
    width,
    height,
  };
}

function tokenForWindow() {
  return settings.serverMode ? settings.token : '';
}

function isLocalAppUrl(rawUrl) {
  try {
    const url = new URL(rawUrl);
    return !!localBase && url.origin === new URL(localBase).origin;
  } catch {
    return false;
  }
}

function primaryLanAddress() {
  const interfaces = os.networkInterfaces();
  for (const addresses of Object.values(interfaces)) {
    for (const address of addresses ?? []) {
      if (address.family === 'IPv4' && !address.internal) {
        return address.address;
      }
    }
  }
  return '127.0.0.1';
}

function externalServerAddress() {
  if (!serverReady) {
    return '';
  }
  if (!settings.serverMode) {
    return localBase;
  }
  let host = settings.listenAddress;
  if (host === '0.0.0.0' || host === '::') {
    host = primaryLanAddress();
  }
  if (host.includes(':') && !host.startsWith('[')) {
    host = `[${host}]`;
  }
  const port = new URL(serverUrl).port;
  return `http://${host}:${port}/?token=${encodeURIComponent(settings.token)}`;
}

function localWindowUrl(windowId, file = '', url = '') {
  if (url) {
    return url;
  }
  return buildWindowUrl(localBase, {
    windowId,
    file,
    token: tokenForWindow(),
  });
}

function applyNavigationPolicy(win) {
  win.webContents.on('will-navigate', (event, url) => {
    if (!isLocalAppUrl(url)) {
      event.preventDefault();
    }
  });
  win.webContents.on('will-attach-webview', (event) => {
    event.preventDefault();
  });
  win.webContents.setWindowOpenHandler(({ url }) => {
    if (isLocalAppUrl(url)) {
      const id = nextWindowId(winState);
      const target = new URL(url);
      target.searchParams.set('window', id);
      if (settings.serverMode) {
        target.searchParams.set('token', settings.token);
      } else {
        target.searchParams.delete('token');
      }
      createWindow({ id, url: target.toString() });
    }
    return { action: 'deny' };
  });
}

function createWindow({ id, file = '', bounds, url = '' }) {
  const existing = windows.get(id);
  if (existing && !existing.isDestroyed()) {
    existing.show();
    existing.focus();
    return existing;
  }

  const saved = findWindowState(id);
  if (!saved) {
    addWindowState(id, bounds ?? defaultBounds());
  }
  const windowBounds = bounds ?? saved?.bounds ?? defaultBounds();

  const win = new BrowserWindow({
    ...windowBounds,
    minWidth: 640,
    minHeight: 420,
    show: false,
    backgroundColor: '#11111b',
    title: 'klogg',
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
      preload: path.join(__dirname, 'preload.cjs'),
      spellcheck: false,
      devTools: !app.isPackaged,
    },
  });

  windows.set(id, win);
  win.setMenu(null);
  applyNavigationPolicy(win);

  win.webContents.on('before-input-event', (event, input) => {
    if (input.type !== 'keyDown' || !input.control || input.alt || input.shift) {
      return;
    }
    const key = input.key.toLowerCase();
    if (key === 'n') {
      event.preventDefault();
      createBlankWindow();
    } else if (key === 'q') {
      event.preventDefault();
      app.quit();
    }
  });

  win.once('ready-to-show', () => {
    if (saved?.maximized) {
      win.maximize();
    }
    win.show();
    win.moveTop();
    win.focus();
  });

  for (const eventName of ['resize', 'move', 'maximize', 'unmaximize']) {
    win.on(eventName, () => updateWindowState(win, id));
  }

  win.on('closed', () => {
    windows.delete(id);
    if (!quitting) {
      winState.windows = winState.windows.filter((entry) => entry.id !== id);
      saveWindowStateNow();
    }
  });

  const target = localWindowUrl(id, file, url);
  win.loadURL(target).catch((error) => {
    if (!quitting) {
      dialog.showErrorBox(
        text('Cannot open klogg', '无法打开 klogg'),
        `${target}\n${error.message}`,
      );
    }
  });

  return win;
}

function createBlankWindow() {
  const id = nextWindowId(winState);
  return createWindow({ id });
}

function focusFirstWindow() {
  const first = windows.values().next().value;
  if (first && !first.isDestroyed()) {
    if (first.isMinimized()) {
      first.restore();
    }
    first.show();
    first.focus();
  } else {
    createBlankWindow();
  }
}

function fileArguments(argv) {
  const appPath = app.getAppPath();
  const files = [];
  for (let index = 1; index < argv.length; index += 1) {
    const arg = argv[index];
    if (!arg || arg.startsWith('-')) {
      continue;
    }
    try {
      if (path.resolve(arg) === path.resolve(appPath)) {
        continue;
      }
    } catch {
      // Keep non-path arguments out of the file list below.
    }
    if (arg !== '.') {
      files.push(arg);
    }
  }
  return files;
}

function trayTemplate() {
  const isZh = isChinese();
  const newWindowLabel = isZh ? '新建窗口' : 'New Window';
  const exitLabel = isZh ? '退出' : 'Exit';
  const serverModeLabel = isZh ? '服务器模式' : 'Server Mode';
  const copyAddressLabel = isZh ? '复制服务器地址' : 'Copy Server Address';
  const showAddressLabel = isZh ? '显示服务器地址…' : 'Show Server Address…';
  const openSettingsLabel = isZh ? '打开服务器设置文件' : 'Open Server Settings File';
  const restartLabel = isZh ? '重启服务器' : 'Restart Server';

  return [
    { label: newWindowLabel, click: () => createBlankWindow() },
    { type: 'separator' },
    {
      label: serverModeLabel,
      type: 'checkbox',
      checked: settings.serverMode,
      enabled: !restarting,
      click: (item) => {
        settings.serverMode = item.checked;
        saveDesktopSettings(settingsPath, settings);
        void restartServer();
      },
    },
    { label: copyAddressLabel, click: () => copyServerAddress() },
    { label: showAddressLabel, click: () => showServerAddress() },
    { label: openSettingsLabel, click: () => void shell.openPath(settingsPath) },
    { label: restartLabel, enabled: !restarting, click: () => void restartServer() },
    { type: 'separator' },
    { label: exitLabel, click: () => app.quit() },
  ];
}

function updateTrayMenu() {
  if (!tray) {
    return;
  }
  tray.setContextMenu(Menu.buildFromTemplate(trayTemplate()));
}

function createTray() {
  if (tray) {
    return;
  }
  const iconPath = app.isPackaged
    ? path.join(process.resourcesPath, 'tray.png')
    : path.join(__dirname, '..', 'resources', 'tray.png');
  tray = new Tray(nativeImage.createFromPath(iconPath));
  tray.setToolTip('klogg');
  tray.on('double-click', focusFirstWindow);
  updateTrayMenu();
}

function copyServerAddress() {
  const address = externalServerAddress();
  if (!address) {
    return;
  }
  clipboard.writeText(address);
}

async function showServerAddress() {
  const address = externalServerAddress();
  if (!address) {
    return;
  }
  const local = localBase
    ? buildWindowUrl(localBase, {
        windowId: 'w1',
        token: tokenForWindow(),
      })
    : '';
  const detail = settings.serverMode
    ? `${text('Local address', '本地地址')}: ${local}\n${text(
        'External address',
        '外部地址',
      )}: ${address}\n\n${text(
        'The local window and remote browsers share one klogg session.',
        '本地窗口和远程浏览器共用同一个 klogg 会话。',
      )}`
    : `${text('Local address', '本地地址')}: ${local}\n\n${text(
        'Enable Server Mode to expose this session on the network.',
        '启用服务器模式后可在局域网访问这个会话。',
      )}`;
  await dialog.showMessageBox({
    type: 'info',
    title: text('klogg Server', 'klogg 服务器'),
    message: settings.serverMode ? text('Server mode is on', '服务器模式已启用') : text(
      'Local mode',
      '本地模式',
    ),
    detail,
  });
}

function verifyResources() {
  const webIndex = path.join(resources.webRoot, 'index.html');
  if (!fs.existsSync(resources.serverPath)) {
    throw new Error(
      text(
        `klogg_server.exe not found at ${resources.serverPath}`,
        `找不到 klogg_server.exe：${resources.serverPath}`,
      ),
    );
  }
  if (!fs.existsSync(webIndex)) {
    throw new Error(
      text(
        `Web UI not found at ${resources.webRoot}`,
        `找不到 Web UI：${resources.webRoot}`,
      ),
    );
  }
}

async function startServer() {
  const options = settings.serverMode
    ? {
        listenAddress: settings.listenAddress,
        port: settings.port,
        token: settings.token,
      }
    : {
        listenAddress: '127.0.0.1',
        port: 0,
        token: '',
      };

  serverUrl = await serverManager.start(options);
  localBase = localServerUrl(serverUrl);
  serverReady = true;
}

async function restartServer() {
  if (restarting) {
    return;
  }
  restarting = true;
  serverReady = false;
  updateTrayMenu();

  try {
    settings = loadDesktopSettings(settingsPath);
    saveDesktopSettings(settingsPath, settings);
    await serverManager.stop();
    await startServer();
    for (const [id, win] of windows) {
      if (!win.isDestroyed()) {
        await win.loadURL(
          buildWindowUrl(localBase, {
            windowId: id,
            token: tokenForWindow(),
          }),
        );
      }
    }
    for (const file of pendingFiles.splice(0)) {
      createWindow({ id: nextWindowId(winState), file });
    }
  } catch (error) {
    dialog.showErrorBox(
      text('Cannot start klogg server', '无法启动 klogg 服务器'),
      error instanceof Error ? error.message : String(error),
    );
  } finally {
    restarting = false;
    updateTrayMenu();
  }
}

async function startApplication() {
  Menu.setApplicationMenu(null);
  settingsPath = path.join(app.getPath('userData'), 'desktop-settings.json');
  windowsPath = path.join(app.getPath('userData'), 'windows.json');
  serverLogPath = path.join(app.getPath('userData'), 'server.log');
  settings = loadDesktopSettings(settingsPath);
  saveDesktopSettings(settingsPath, settings);
  winState = loadWindowState(windowsPath);

  resources = resolveResources({
    isPackaged: app.isPackaged,
    resourcesPath: process.resourcesPath,
    appDir: app.getAppPath(),
    serverOverride: process.env.KLOGG_SERVER_BIN || '',
  });
  verifyResources();

  serverManager = createServerManager({
    serverPath: resources.serverPath,
    webRoot: resources.webRoot,
    logFile: serverLogPath,
  });
  serverManager.onExit((code, signal, detail) => {
    if (quitting || restarting) {
      return;
    }
    dialog.showErrorBox(
      text('klogg server stopped', 'klogg 服务器已停止'),
      `${code ?? ''} ${signal ?? ''}\n${detail ?? ''}`,
    );
    app.quit();
  });

  await startServer();
  createTray();

  const files = fileArguments(process.argv);
  if (files.length > 0) {
    pendingFiles.push(...files);
  }

  if (winState.windows.length === 0) {
    const id = nextWindowId(winState);
    const file = pendingFiles.shift() ?? '';
    createWindow({ id, file });
  } else {
    for (const entry of winState.windows) {
      createWindow({ id: entry.id, bounds: entry.bounds });
    }
    for (const file of pendingFiles.splice(0)) {
      const id = nextWindowId(winState);
      createWindow({ id, file });
    }
  }
}

if (gotSingleInstanceLock) {
  app.on('second-instance', (_event, argv) => {
    const files = fileArguments(argv);
    if (!serverReady) {
      pendingFiles.push(...files);
      return;
    }
    if (files.length > 0) {
      for (const file of files) {
        const id = nextWindowId(winState);
        createWindow({ id, file });
      }
    } else {
      focusFirstWindow();
    }
  });

  app.whenReady().then(async () => {
    app.setAppUserModelId('com.github.variar.klogg');
    try {
      await startApplication();
    } catch (error) {
      dialog.showErrorBox(
        text('Cannot start klogg', '无法启动 klogg'),
        error instanceof Error ? error.message : String(error),
      );
      app.quit();
    }
  });

  app.on('window-all-closed', () => {
    app.quit();
  });

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createBlankWindow();
    }
  });

  app.on('before-quit', (event) => {
    if (quitting) {
      return;
    }
    event.preventDefault();
    quitting = true;
    if (saveTimer) {
      clearTimeout(saveTimer);
      saveTimer = null;
    }
    saveWindowStateNow();
    if (tray) {
      tray.destroy();
      tray = null;
    }
    const stop = serverManager ? serverManager.stop() : Promise.resolve();
    stop.finally(() => {
      app.quit();
    });
  });
}
