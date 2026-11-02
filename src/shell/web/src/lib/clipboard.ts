/**
 * Writes the desktop "copy with color" payload to the clipboard: the rich text
 * flavour is what makes the colours survive a paste into a document editor,
 * the plain text flavour is the fallback for everything else. Both are written
 * in one operation so a paste never sees only one of them.
 */
export async function writeRichText(html: string, text: string): Promise<void> {
  const clipboard = typeof navigator === 'undefined' ? undefined : navigator.clipboard;
  if (clipboard?.write && typeof ClipboardItem !== 'undefined') {
    try {
      await clipboard.write([
        new ClipboardItem({
          'text/html': new Blob([html], { type: 'text/html' }),
          'text/plain': new Blob([text], { type: 'text/plain' }),
        }),
      ]);
      return;
    } catch {
      // Permission or format rejection: fall through to the copy event, which
      // Chromium lets the page fill directly.
    }
  }

  let written = false;
  const onCopy = (event: ClipboardEvent): void => {
    if (!event.clipboardData) {
      return;
    }
    event.clipboardData.setData('text/html', html);
    event.clipboardData.setData('text/plain', text);
    event.preventDefault();
    written = true;
  };
  document.addEventListener('copy', onCopy, { once: true });
  try {
    document.execCommand('copy');
  } finally {
    document.removeEventListener('copy', onCopy);
  }
  if (written) {
    return;
  }
  if (!clipboard?.writeText) {
    throw new Error('clipboard unavailable');
  }
  await clipboard.writeText(text);
}
