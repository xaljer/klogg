/**
 * Normalizes user typed hex notation to the `#rrggbb` form that
 * `<input type="color">` understands.
 *
 * Accepts an optional leading `#`, three digit shorthand (`#abc`), the regular
 * six digit form, and QColor style eight digit ARGB (`#aarrggbb`) — the alpha is
 * dropped because the native picker has no alpha channel.
 */
export function normalizeHexColor(value: string): string | null {
  const digits = value.trim().replace(/^#/, '');
  if (/^[0-9a-f]{3}$/i.test(digits)) {
    const expanded = digits
      .toLowerCase()
      .split('')
      .map((digit) => digit + digit)
      .join('');
    return `#${expanded}`;
  }
  if (/^[0-9a-f]{6}$/i.test(digits)) {
    return `#${digits.toLowerCase()}`;
  }
  if (/^[0-9a-f]{8}$/i.test(digits)) {
    return `#${digits.slice(2).toLowerCase()}`;
  }
  return null;
}
