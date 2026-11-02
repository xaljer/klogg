const fs = require('node:fs');
const path = require('node:path');

function normalizeWindow(entry) {
  if (!entry || typeof entry !== 'object' || typeof entry.id !== 'string' || !entry.id) {
    return null;
  }
  const bounds = entry.bounds;
  if (
    !bounds ||
    typeof bounds !== 'object' ||
    !['x', 'y', 'width', 'height'].every(
      (key) => typeof bounds[key] === 'number' && Number.isFinite(bounds[key]),
    )
  ) {
    return null;
  }
  return {
    id: entry.id,
    bounds: {
      x: Math.round(bounds.x),
      y: Math.round(bounds.y),
      width: Math.max(480, Math.round(bounds.width)),
      height: Math.max(360, Math.round(bounds.height)),
    },
    maximized: entry.maximized === true,
  };
}

function loadWindowState(filePath) {
  try {
    const parsed = JSON.parse(fs.readFileSync(filePath, 'utf8'));
    if (!parsed || !Array.isArray(parsed.windows)) {
      return { windows: [] };
    }
    return {
      windows: parsed.windows.map(normalizeWindow).filter(Boolean),
    };
  } catch {
    return { windows: [] };
  }
}

function saveWindowState(filePath, state) {
  const windows = Array.isArray(state?.windows)
    ? state.windows.map(normalizeWindow).filter(Boolean)
    : [];
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, `${JSON.stringify({ windows }, null, 2)}\n`, 'utf8');
}

module.exports = {
  loadWindowState,
  saveWindowState,
};
