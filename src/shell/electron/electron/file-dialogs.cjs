const path = require('node:path');

const DEFAULT_FILE_TITLE = ['Open log file', '打开日志文件'];
const DEFAULT_BUTTON = ['Open', '打开'];

/**
 * Keeps only the options the renderer is allowed to influence, so a malformed
 * IPC payload cannot inject paths or dialog properties.
 */
function sanitizeOptions(raw) {
  if (!raw || typeof raw !== 'object') {
    return {};
  }
  const options = {};
  for (const key of ['title', 'defaultPath']) {
    const value = raw[key];
    if (typeof value === 'string' && value.trim()) {
      options[key] = value.trim();
    }
  }
  return options;
}

/**
 * `openFile` + `multiSelections` mean the same thing on Windows, macOS and
 * Linux; Electron shows Explorer, Finder or the desktop portal for each, so no
 * platform branch is needed here.
 */
function buildOpenFilesOptions(rawOptions, { text, fallbackDirectory = '' } = {}) {
  const options = sanitizeOptions(rawOptions);
  const dialogOptions = {
    title: options.title || text(...DEFAULT_FILE_TITLE),
    buttonLabel: text(...DEFAULT_BUTTON),
    properties: ['openFile', 'multiSelections'],
  };
  const defaultPath = options.defaultPath || fallbackDirectory;
  if (defaultPath) {
    dialogOptions.defaultPath = defaultPath;
  }
  return dialogOptions;
}

function registerFileDialogs({
  ipcMain,
  dialog,
  BrowserWindow,
  shell,
  text,
  getHomeDirectory = () => '',
  getLastDirectory = () => '',
  setLastDirectory = () => {},
}) {
  ipcMain.handle('klogg:dialog:open-files', async (event, rawOptions) => {
    const options = buildOpenFilesOptions(rawOptions, {
      text,
      fallbackDirectory: getLastDirectory() || getHomeDirectory(),
    });
    const win = BrowserWindow.fromWebContents(event.sender);
    const result = win
      ? await dialog.showOpenDialog(win, options)
      : await dialog.showOpenDialog(options);
    if (result.canceled || !Array.isArray(result.filePaths)) {
      return [];
    }
    if (result.filePaths.length > 0) {
      setLastDirectory(path.dirname(result.filePaths[0]));
    }
    return result.filePaths;
  });

  ipcMain.handle('klogg:dialog:reveal', async (event, filePath) => {
    if (typeof filePath === 'string' && filePath.length > 0) {
      shell.showItemInFolder(filePath);
    }
  });
}

module.exports = {
  buildOpenFilesOptions,
  registerFileDialogs,
  sanitizeOptions,
};
