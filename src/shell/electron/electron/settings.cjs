const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

function newToken() {
  return crypto.randomBytes(24).toString('hex');
}

function defaultDesktopSettings() {
  return {
    serverMode: false,
    listenAddress: '0.0.0.0',
    port: 8080,
    token: newToken(),
  };
}

function normalizePort(value) {
  const port = Number(value);
  return Number.isInteger(port) && port >= 1 && port <= 65535 ? port : 8080;
}

function normalizeSettings(value) {
  const defaults = defaultDesktopSettings();
  return {
    serverMode: value?.serverMode === true,
    listenAddress:
      typeof value?.listenAddress === 'string' && value.listenAddress.trim()
        ? value.listenAddress.trim()
        : defaults.listenAddress,
    port: normalizePort(value?.port),
    token:
      typeof value?.token === 'string' && value.token.length > 0 ? value.token : defaults.token,
  };
}

function loadDesktopSettings(filePath) {
  try {
    return normalizeSettings(JSON.parse(fs.readFileSync(filePath, 'utf8')));
  } catch {
    return defaultDesktopSettings();
  }
}

function saveDesktopSettings(filePath, settings) {
  const normalized = normalizeSettings(settings);
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, `${JSON.stringify(normalized, null, 2)}\n`, 'utf8');
}

module.exports = {
  defaultDesktopSettings,
  loadDesktopSettings,
  saveDesktopSettings,
};
