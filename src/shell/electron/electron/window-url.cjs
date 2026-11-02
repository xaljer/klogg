function buildWindowUrl(serverUrl, { windowId, file = '', token = '' }) {
  const url = new URL(serverUrl);
  url.searchParams.delete('window');
  url.searchParams.delete('file');
  url.searchParams.delete('token');
  url.searchParams.set('window', windowId);
  if (file) {
    url.searchParams.set('file', file);
  }
  if (token) {
    url.searchParams.set('token', token);
  }
  return url.toString();
}

function replaceWindowId(rawUrl, windowId) {
  const url = new URL(rawUrl);
  url.searchParams.set('window', windowId);
  return url.toString();
}

function nextWindowId(state) {
  let highest = 0;
  for (const entry of state?.windows ?? []) {
    const match = /^w(\d+)$/.exec(entry?.id ?? '');
    if (match) {
      highest = Math.max(highest, Number(match[1]));
    }
  }
  return `w${highest + 1}`;
}

module.exports = {
  buildWindowUrl,
  nextWindowId,
  replaceWindowId,
};
