const { contextBridge, ipcRenderer } = require('electron');

function dialogOptions(raw) {
  if (!raw || typeof raw !== 'object') {
    return {};
  }
  const options = {};
  if (typeof raw.title === 'string') {
    options.title = raw.title;
  }
  if (typeof raw.defaultPath === 'string') {
    options.defaultPath = raw.defaultPath;
  }
  return options;
}

// Minimal, explicit bridge: the renderer can ask for a native file selection and
// reveal a file, nothing else.
contextBridge.exposeInMainWorld('kloggDesktop', {
  platform: process.platform,
  openFiles: (options) => ipcRenderer.invoke('klogg:dialog:open-files', dialogOptions(options)),
  revealItem: (filePath) =>
    ipcRenderer.invoke('klogg:dialog:reveal', typeof filePath === 'string' ? filePath : ''),
  readClipboard: () => ipcRenderer.invoke('klogg:clipboard:read'),
});
