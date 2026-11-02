/**
 * Text a within-row drag selection turns into the word-highlight key.
 *
 * A selection the user draws has no word boundaries to snap to, so its edges
 * often catch the spaces around the text that was meant; those would never
 * match another line and are dropped. A selection spanning rows is a row range,
 * not a word, so it yields no key at all.
 */
export function selectionWord(text: string): string {
  const trimmed = text.trim();
  return trimmed.includes('\n') || trimmed.includes('\r') ? '' : trimmed;
}
