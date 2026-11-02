const path = require('node:path');

function trimmed(value) {
  return typeof value === 'string' && value.trim() ? value.trim() : '';
}

function resolveUserDataDir({ env = process.env, isPackaged = false, exePath = '' } = {}) {
  const explicit = trimmed(env.KLOGG_DESKTOP_USER_DATA);
  if (explicit) {
    return path.resolve(explicit);
  }

  const portableBase = trimmed(env.PORTABLE_EXECUTABLE_DIR);
  if (portableBase) {
    return path.join(path.resolve(portableBase), 'userdata');
  }

  if (isPackaged && exePath) {
    return path.join(path.dirname(path.resolve(exePath)), 'userdata');
  }

  return '';
}

module.exports = {
  resolveUserDataDir,
};
