export interface DesktopDialogOptions {
  title?: string;
  defaultPath?: string;
}

/** Bridge exposed by the Electron preload script; absent in a plain browser. */
export interface DesktopBridge {
  platform: string;
  openFiles(options?: DesktopDialogOptions): Promise<string[]>;
  revealItem(path: string): Promise<void>;
  /** Reads the system clipboard through the main process (no page permission). */
  readClipboard?(): Promise<string>;
}

export function desktopBridge(): DesktopBridge | null {
  if (typeof window === 'undefined') {
    return null;
  }
  const bridge = (window as { kloggDesktop?: Partial<DesktopBridge> }).kloggDesktop;
  if (!bridge || typeof bridge.openFiles !== 'function') {
    return null;
  }
  return bridge as DesktopBridge;
}

/**
 * Folder a native dialog should start in, derived from a file reference. Empty
 * for URLs and for references without a parent path, so the dialog falls back
 * to its own default.
 */
export function dialogDirectory(reference: string): string {
  if (!reference || /^https?:\/\//i.test(reference)) {
    return '';
  }
  const trimmed = reference.replace(/[\\/]+$/, '');
  if (!trimmed) {
    return '';
  }
  if (reference.length !== trimmed.length) {
    // Already a directory; keep it, restoring the root separator on Windows.
    return /^[A-Za-z]:$/.test(trimmed) ? `${trimmed}\\` : trimmed;
  }
  const cut = Math.max(trimmed.lastIndexOf('\\'), trimmed.lastIndexOf('/'));
  if (cut <= 0) {
    return '';
  }
  const parent = trimmed.slice(0, cut);
  return /^[A-Za-z]:$/.test(parent) ? `${parent}\\` : parent;
}
