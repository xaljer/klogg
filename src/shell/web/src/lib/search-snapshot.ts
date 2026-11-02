import type { SearchChip } from './types.ts';

/**
 * The parameters of a search that was actually started.
 *
 * The live refresh (the desktop auto-refresh search) re-runs this snapshot. It
 * must never read the pattern input: with a growing file, every refresh would
 * otherwise search for the half-typed text and replace the user's results.
 */
export interface SearchSnapshot {
  expression: string;
  chipMode: boolean;
  chips: SearchChip[];
  regex: boolean;
  caseSensitive: boolean;
  invert: boolean;
  fromLine: number | null;
  toLine: number | null;
  maxResults: number | null;
}

export interface SearchInputs {
  chipMode: boolean;
  chips: SearchChip[];
  pattern: string;
  regex: boolean;
  caseSensitive: boolean;
  invert: boolean;
  fromLine: number | null;
  toLine: number | null;
  maxResults: number | null;
}

/** Snapshot of the current inputs, taken when the user starts a search. */
export function createSearchSnapshot(
  inputs: SearchInputs,
  toExpression: (chips: SearchChip[]) => string,
): SearchSnapshot {
  return {
    expression: inputs.chipMode ? toExpression(inputs.chips) : inputs.pattern.trim(),
    chipMode: inputs.chipMode,
    // The chips are part of the snapshot: a live refresh must not pick up a
    // condition the user added without running the search.
    chips: inputs.chips.map((chip) => ({ ...chip, terms: [...chip.terms] })),
    regex: inputs.regex,
    caseSensitive: inputs.caseSensitive,
    invert: inputs.invert,
    fromLine: inputs.fromLine,
    toLine: inputs.toLine,
    maxResults: inputs.maxResults,
  };
}

/**
 * Pattern highlight of a snapshot. Chip searches highlight every term the
 * engine matched: as regexes when the regex toggle is on, as literal text
 * otherwise — the same split the search parameters use.
 */
export function snapshotHighlight(
  snapshot: SearchSnapshot,
  escape: (text: string) => string,
): { pattern: string; regex: boolean; caseSensitive: boolean } {
  if (!snapshot.chipMode) {
    return {
      pattern: snapshot.expression,
      regex: snapshot.regex,
      caseSensitive: snapshot.caseSensitive,
    };
  }
  const terms = snapshot.chips
    .filter((chip) => chip.type !== 'raw')
    .flatMap((chip) => chip.terms)
    .filter((term) => term.length > 0);
  return {
    pattern: terms.map((term) => (snapshot.regex ? term : escape(term))).join('|'),
    regex: true,
    caseSensitive: snapshot.caseSensitive,
  };
}

/** Engine parameters of a snapshot, in the shape `search.run` expects. */
export function snapshotSearchParams(snapshot: SearchSnapshot): {
  pattern: string;
  caseSensitive: boolean;
  isRegex: boolean;
  isBoolean: boolean;
  invertMatch: boolean;
  fromLine: number | null;
  toLine: number | null;
  maxResults: number | null;
} {
  return {
    pattern: snapshot.expression,
    caseSensitive: snapshot.caseSensitive,
    // A boolean expression always quotes its terms; the regex toggle decides
    // whether the engine reads them as regexes or as plain text, exactly like
    // the desktop boolean search. Forcing plain text used to drop every regex
    // metacharacter from the chip search.
    isRegex: snapshot.regex,
    isBoolean: snapshot.chipMode,
    invertMatch: snapshot.chipMode ? false : snapshot.invert,
    fromLine: snapshot.fromLine,
    // The core takes an exclusive end line.
    toLine: snapshot.toLine !== null ? snapshot.toLine + 1 : null,
    maxResults: snapshot.maxResults,
  };
}
