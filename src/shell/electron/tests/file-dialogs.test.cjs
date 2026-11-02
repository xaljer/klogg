const assert = require('node:assert/strict');
const path = require('node:path');
const test = require('node:test');

const {
  buildOpenFilesOptions,
  sanitizeOptions,
} = require('../electron/file-dialogs.cjs');
const { registerFileDialogs } = require('../electron/file-dialogs.cjs');

const text = (en) => en;

// Built with the platform's own separator, because the handler derives the
// remembered directory with path.dirname.
const chosenLog = path.join('C:', 'logs', 'a.log');
const chosenDir = path.join('C:', 'logs');

function fakeIpc() {
  const handlers = new Map();
  return {
    handlers,
    handle(channel, handler) {
      handlers.set(channel, handler);
    },
  };
}

function setup({ result, lastDirectory = '' } = {}) {
  const ipcMain = fakeIpc();
  const dialogCalls = [];
  const remembered = [];
  const revealed = [];
  registerFileDialogs({
    ipcMain,
    dialog: {
      showOpenDialog: async (...args) => {
        dialogCalls.push(args);
        return result ?? { canceled: false, filePaths: [chosenLog] };
      },
    },
    BrowserWindow: { fromWebContents: () => ({ id: 1 }) },
    shell: { showItemInFolder: (target) => revealed.push(target) },
    text,
    getHomeDirectory: () => 'C:\\Users\\me',
    getLastDirectory: () => lastDirectory,
    setLastDirectory: (directory) => remembered.push(directory),
  });
  return { ipcMain, dialogCalls, remembered, revealed };
}

test('dialog options ask the OS picker for files and allow multiple', () => {
  const options = buildOpenFilesOptions(
    { title: 'Pick a log', defaultPath: 'D:\\logs' },
    { text },
  );
  assert.deepEqual(options.properties, ['openFile', 'multiSelections']);
  assert.equal(options.title, 'Pick a log');
  assert.equal(options.defaultPath, 'D:\\logs');
  assert.equal(options.buttonLabel, 'Open');
  assert.equal(options.filters, undefined);
});

test('dialog options fall back to the last/home directory and localise titles', () => {
  const options = buildOpenFilesOptions(null, { text, fallbackDirectory: 'C:\\Users\\me' });
  assert.equal(options.title, 'Open log file');
  assert.equal(options.defaultPath, 'C:\\Users\\me');
});

test('only string dialog hints survive the IPC payload', () => {
  assert.deepEqual(sanitizeOptions({ title: '  Logs ', defaultPath: 7, extra: 'x' }), {
    title: 'Logs',
  });
  assert.deepEqual(sanitizeOptions('nope'), {});
});

test('the open handler returns chosen files and remembers the folder', async () => {
  const { ipcMain, dialogCalls, remembered } = setup();
  const handler = ipcMain.handlers.get('klogg:dialog:open-files');
  const files = await handler({ sender: {} }, { defaultPath: 'D:\\logs' });

  assert.deepEqual(files, [chosenLog]);
  assert.equal(dialogCalls.length, 1);
  assert.equal(dialogCalls[0].length, 2, 'dialog is parented to the window');
  assert.deepEqual(dialogCalls[0][1].properties, ['openFile', 'multiSelections']);
  assert.deepEqual(remembered, [chosenDir]);
});

test('a cancelled dialog yields no files', async () => {
  const { ipcMain } = setup({ result: { canceled: true, filePaths: [] } });
  const handler = ipcMain.handlers.get('klogg:dialog:open-files');
  assert.deepEqual(await handler({ sender: {} }, {}), []);
});

test('the reveal handler forwards the path to the OS file manager', async () => {
  const { ipcMain, revealed } = setup();
  const handler = ipcMain.handlers.get('klogg:dialog:reveal');
  await handler({ sender: {} }, 'D:\\logs\\a.log');
  await handler({ sender: {} }, '');
  assert.deepEqual(revealed, ['D:\\logs\\a.log']);
});
