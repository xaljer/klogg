import type { SearchChip, SearchChipType } from './types';

export interface ChipSpec {
  type: SearchChipType;
  terms: string[];
}

function chipTerm(text: string): string {
  return text.trim().replace(/^"(.*)"$/s, '$1').replace(/\\"/g, '"');
}

/** Splits on a separator that is outside double quotes; parts keep their quotes. */
function splitTopLevel(text: string, separator: '&' | '|'): string[] {
  const parts: string[] = [];
  let current = '';
  let quoted = false;
  for (const ch of text) {
    if (ch === '"') {
      quoted = !quoted;
      current += ch;
      continue;
    }
    if (!quoted && ch === separator) {
      parts.push(current);
      current = '';
      continue;
    }
    current += ch;
  }
  parts.push(current);
  return parts.map((part) => part.trim()).filter((part) => part.length > 0);
}

/**
 * Parses the compact text the chip input accepts: `a|b` are alternatives,
 * `a&b` is a conjunction and `!x` / `not x` negates a term. Quoted terms keep
 * separators and spaces literal.
 */
export function parseChipText(text: string, negate = false): ChipSpec[] {
  const trimmed = text.trim();
  if (!trimmed) {
    return [];
  }
  const orParts = splitTopLevel(trimmed, '|');
  if (orParts.length > 1) {
    return orParts.flatMap((part) => parseChipText(part, negate));
  }

  let raw = trimmed;
  let isNegate = negate;
  if (!isNegate && /^(?:!|not\s+)/i.test(raw)) {
    raw = raw.replace(/^(?:!|not\s+)/i, '');
    isNegate = true;
  }
  const andParts = splitTopLevel(raw, '&');
  const terms = andParts.map(chipTerm).filter((term) => term.length > 0);
  const type: SearchChipType = terms.length > 1
    ? (isNegate ? 'notAnd' : 'and')
    : isNegate
      ? 'not'
      : 'or';
  return [{ type, terms: terms.length > 0 ? terms : [chipTerm(raw)] }];
}

function quotation(term: string): string {
  return `"${term.replace(/"/g, '\\"')}"`;
}

/** Boolean expression sent to the engine; quoting and precedence live here. */
export function chipsToExpression(chips: SearchChip[]): string {
  const includes: string[] = [];
  const excludes: string[] = [];
  for (const chip of chips) {
    if (chip.type === 'raw') {
      includes.push(`(${chip.terms[0] ?? ''})`);
      continue;
    }
    if (chip.type === 'not' || chip.type === 'notAnd') {
      if (chip.type === 'notAnd' && chip.terms.length > 1) {
        excludes.push(`not((${chip.terms.map(quotation).join(' and ')}))`);
      } else {
        excludes.push(`not(${quotation(chip.terms[0] ?? '')})`);
      }
      continue;
    }
    if (chip.type === 'and' && chip.terms.length > 1) {
      includes.push(`(${chip.terms.map(quotation).join(' and ')})`);
    } else {
      includes.push(quotation(chip.terms[0] ?? ''));
    }
  }
  const parts: string[] = [];
  if (includes.length > 0) {
    parts.push(includes.length > 1 ? `(${includes.join(' or ')})` : includes[0]);
  }
  parts.push(...excludes);
  return parts.join(' and ');
}

/**
 * Quote a term only when the compact parser would otherwise split or negate it,
 * so the common case stays as short as `ERROR|WARN`.
 */
function quoteTerm(term: string): string {
  const trimmed = term.trim();
  if (!trimmed) {
    return '""';
  }
  if (/[\s|&"!]/.test(trimmed) || /^not\b/i.test(trimmed)) {
    return `"${trimmed.replace(/"/g, '\\"')}"`;
  }
  return trimmed;
}

/**
 * Compact text form for search history: the same `a|b`, `a&b`, `!x` syntax the
 * chip input accepts, so history entries stay readable instead of storing the
 * generated boolean expression.
 */
export function chipsToText(chips: SearchChip[]): string {
  const parts: string[] = [];
  for (const chip of chips) {
    if (chip.type === 'raw') {
      const raw = (chip.terms[0] ?? '').trim();
      if (raw) {
        parts.push(raw);
      }
      continue;
    }
    const separator = chip.type === 'and' || chip.type === 'notAnd' ? '&' : '|';
    const terms = chip.terms.map(quoteTerm).filter((term) => term.length > 0);
    if (terms.length === 0) {
      continue;
    }
    const negated = chip.type === 'not' || chip.type === 'notAnd';
    parts.push(`${negated ? '!' : ''}${terms.join(separator)}`);
  }
  return parts.join('|');
}
