const path = require('node:path');

// The bundled server keeps its platform naming, so the packaged app finds it on
// Windows and on the Linux AppImage alike.
const serverExeName = process.platform === 'win32' ? 'klogg_server.exe' : 'klogg_server';

function resolveResources({ isPackaged, resourcesPath, appDir, serverOverride = '' }) {
  if (isPackaged) {
    return {
      serverPath: path.join(resourcesPath, 'server', serverExeName),
      webRoot: path.join(resourcesPath, 'web'),
    };
  }

  const repoRoot = path.resolve(appDir, '..', '..', '..');
  return {
    serverPath:
      serverOverride || path.join(repoRoot, 'out', 'server', 'output', serverExeName),
    webRoot: path.join(repoRoot, 'out', 'web'),
  };
}

module.exports = {
  resolveResources,
  serverExeName,
};
