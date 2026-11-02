const assert = require('node:assert/strict');
const path = require('node:path');
const test = require('node:test');

const { resolveUserDataDir } = require('../electron/user-data.cjs');

// Absolute on every platform: a bare drive prefix like "C:" is a relative
// segment outside Windows, and the resolver calls path.resolve on its input.
const apps = path.resolve('apps');
const portableBase = path.resolve('portable');

test('packaged builds keep user data next to the executable', () => {
  const dir = resolveUserDataDir({
    env: {},
    isPackaged: true,
    exePath: path.join(apps, 'klogg', 'klogg.exe'),
  });
  assert.equal(dir, path.join(apps, 'klogg', 'userdata'));
});

test('PORTABLE_EXECUTABLE_DIR wins over the extracted executable location', () => {
  const dir = resolveUserDataDir({
    env: { PORTABLE_EXECUTABLE_DIR: portableBase },
    isPackaged: true,
    exePath: path.join(apps, 'Temp', 'klogg.exe'),
  });
  assert.equal(dir, path.join(portableBase, 'userdata'));
});

test('explicit KLOGG_DESKTOP_USER_DATA wins over portable detection', () => {
  const custom = path.join(apps, 'custom');
  const dir = resolveUserDataDir({
    env: {
      KLOGG_DESKTOP_USER_DATA: custom,
      PORTABLE_EXECUTABLE_DIR: portableBase,
    },
    isPackaged: true,
    exePath: path.join(apps, 'klogg.exe'),
  });
  assert.equal(dir, path.resolve(custom));
});

test('development builds keep the Electron default userData directory', () => {
  assert.equal(resolveUserDataDir({ env: {}, isPackaged: false, exePath: 'klogg.exe' }), '');
  assert.equal(
    resolveUserDataDir({ env: { KLOGG_DESKTOP_USER_DATA: '   ' }, isPackaged: false }),
    '',
  );
});
