const fs = require('node:fs');
const path = require('node:path');

const desktopRoot = path.resolve(__dirname, '..');
const repoRoot = path.resolve(desktopRoot, '..', '..', '..');
const serverExeName = process.platform === 'win32' ? 'klogg_server.exe' : 'klogg_server';
const serverSource =
  process.env.KLOGG_SERVER_BIN ||
  path.join(repoRoot, 'out', 'server', 'output', serverExeName);
const webIndex = path.join(repoRoot, 'out', 'web', 'index.html');
const targetDir = path.join(repoRoot, 'out', 'electron', 'resources', 'server');
const target = path.join(targetDir, serverExeName);

if (!fs.existsSync(serverSource)) {
  console.error(`${serverExeName} not found: ${serverSource}`);
  console.error('Build it with: cmake --preset server && cmake --build out/server --target klogg_server');
  process.exit(1);
}
if (!fs.existsSync(webIndex)) {
  console.error(`Web UI build not found: ${webIndex}`);
  console.error('Build it with: cd src/shell/web && npm run build');
  process.exit(1);
}

function findFileIn(directory, name) {
  const pending = [directory];
  while (pending.length > 0) {
    const current = pending.pop();
    for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
      const fullPath = path.join(current, entry.name);
      if (entry.isDirectory()) {
        pending.push(fullPath);
      } else if (entry.name.toLowerCase() === name.toLowerCase()) {
        return fullPath;
      }
    }
  }
  return '';
}

function findRuntimeDll(name) {
  for (const root of [path.dirname(serverSource), path.join(repoRoot, 'out', 'server')]) {
    if (fs.existsSync(root)) {
      const found = findFileIn(root, name);
      if (found) {
        return found;
      }
    }
  }
  return '';
}

// The server links the MSVC runtime; it has to ship next to klogg_server.exe, so
// collect the redistributable DLLs from the active Visual Studio installation.
function findMsvcRedistDir() {
  const candidates = [];
  if (process.env.VCToolsRedistDir) {
    candidates.push(process.env.VCToolsRedistDir);
  }
  if (process.env.VCINSTALLDIR) {
    candidates.push(path.join(process.env.VCINSTALLDIR, 'Redist', 'MSVC'));
  }
  for (const candidate of candidates) {
    if (!fs.existsSync(candidate)) {
      continue;
    }
    const pending = [candidate];
    while (pending.length > 0) {
      const current = pending.pop();
      for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
        const fullPath = path.join(current, entry.name);
        if (entry.isDirectory()) {
          if (/^Microsoft\.VC14\d\.CRT$/i.test(entry.name) && /[\\/]x64[\\/]/i.test(fullPath)) {
            return fullPath;
          }
          pending.push(fullPath);
        }
      }
    }
  }
  return '';
}

fs.mkdirSync(targetDir, { recursive: true });
fs.copyFileSync(serverSource, target);
if (process.platform !== 'win32') {
  // npm and tar both drop the executable bit on some paths.
  fs.chmodSync(target, 0o755);
}
console.log(`server: ${serverSource}`);

// The MSVC runtime and TBB DLLs only exist on Windows; on Linux the server links
// them statically (or takes them from the host libc).
if (process.platform === 'win32') {
  const tbbDll = findRuntimeDll('tbb12.dll');
  if (tbbDll) {
    fs.copyFileSync(tbbDll, path.join(targetDir, 'tbb12.dll'));
    console.log(`runtime: ${tbbDll}`);
  }

  const redistDir = findMsvcRedistDir();
  if (redistDir) {
    for (const name of ['msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll']) {
      const source = path.join(redistDir, name);
      if (fs.existsSync(source)) {
        fs.copyFileSync(source, path.join(targetDir, name));
      } else {
        console.warn(`missing in redist: ${name}`);
      }
    }
    console.log(`msvc redist: ${redistDir}`);
  } else {
    console.warn('MSVC redistributable not found (VCINSTALLDIR/VCToolsRedistDir unset)');
  }
}

console.log(`target: ${target}`);
console.log(`web:    ${path.dirname(webIndex)}`);
