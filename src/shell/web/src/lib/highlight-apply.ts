// Client-side application of highlight rules, mirroring the desktop engine's
// HighlighterSet::matchLine: rules are ordered by priority (the first matching
// "whole line" rule wins), `onlyMatch` rules colour their matches only, and
// `variateColors` (only meaningful together with onlyMatch) shifts each match by
// a factor derived from its own text.

import type { HighlightRule } from './types';

export interface RuleSpan {
  start: number;
  end: number;
  fore: string;
  back: string;
}

export interface CompiledRule {
  rule: HighlightRule;
  matcher: RegExp | null;
}

// Kept local on purpose: this module stays dependency free so the algorithm can
// be exercised directly by the unit tests.
function htmlColor(apiColor: string): string {
  if (/^#[0-9a-f]{8}$/i.test(apiColor)) {
    return `#${apiColor.slice(3)}`;
  }
  return /^#[0-9a-f]{6}$/i.test(apiColor) ? apiColor : '';
}

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let index = 0; index < 256; index++) {
    let value = index;
    for (let bit = 0; bit < 8; bit++) {
      value = value & 1 ? 0xedb88320 ^ (value >>> 1) : value >>> 1;
    }
    table[index] = value >>> 0;
  }
  return table;
})();

/** Reflected CRC-32, matching Crc32::calculate in src/utils/include/crc32.h. */
export function crc32(text: string): number {
  let crc = 0xffffffff;
  for (let index = 0; index < text.length; index++) {
    crc = (crc >>> 8) ^ CRC_TABLE[(crc ^ text.charCodeAt(index)) & 0xff];
  }
  return (crc ^ 0xffffffff) >>> 0;
}

/**
 * Deterministic factor for one match, within [100 - variance, 100 + variance].
 * The desktop seeds std::minstd_rand0 with the match CRC and draws from a
 * std::uniform_int_distribution, which is implementation defined; this uses the
 * same generator and range so the variation looks identical without claiming
 * bit-for-bit equality with any particular C++ runtime.
 */
export function varianceFactor(match: string, variance: number): number {
  const bounded = Math.min(100, Math.max(0, Math.round(variance)));
  const span = bounded * 2 + 1;
  let state = crc32(match) % 2147483647;
  if (state === 0) {
    state = 1;
  }
  state = (16807 * state) % 2147483647;
  return 100 - bounded + (state % span);
}

/** QColor::darker semantics: scale RGB by 100/factor, keep alpha. */
export function darker(htmlColor: string, factor: number): string {
  if (!/^#[0-9a-f]{6}$/i.test(htmlColor) || factor <= 0) {
    return htmlColor;
  }
  const channels = [1, 3, 5].map((offset) => {
    const value = Number.parseInt(htmlColor.slice(offset, offset + 2), 16);
    return Math.min(255, Math.floor((value * 100) / factor));
  });
  return `#${channels.map((value) => value.toString(16).padStart(2, '0')).join('')}`;
}

function escapeLiteral(text: string): string {
  return text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

export function compileRules(rules: HighlightRule[]): CompiledRule[] {
  return rules.map((rule) => {
    const pattern = rule.pattern.trim();
    if (!rule.enabled || !pattern) {
      return { rule, matcher: null };
    }
    const source = rule.useRegex ? rule.pattern : escapeLiteral(rule.pattern);
    try {
      return { rule, matcher: new RegExp(source, rule.caseSensitive ? 'g' : 'gi') };
    } catch {
      return { rule, matcher: null };
    }
  });
}

interface Painted {
  fore: string;
  back: string;
}

function matchRanges(matcher: RegExp, text: string): { start: number; end: number; text: string }[] {
  const ranges: { start: number; end: number; text: string }[] = [];
  matcher.lastIndex = 0;
  let match = matcher.exec(text);
  while (match) {
    if (match[0].length === 0) {
      matcher.lastIndex++;
    } else {
      ranges.push({ start: match.index, end: match.index + match[0].length, text: match[0] });
    }
    match = matcher.exec(text);
  }
  return ranges;
}

/**
 * Sorted, non-overlapping spans of the text that the active rules colour.
 * Regions keep the first (highest priority) rule that painted them.
 */
export function applyHighlightRules(text: string, compiled: CompiledRule[]): RuleSpan[] {
  if (!text || compiled.length === 0) {
    return [];
  }
  const painted: (Painted | null)[] = new Array(text.length).fill(null);

  const paint = (start: number, end: number, colors: Painted): void => {
    for (let index = start; index < end && index < painted.length; index++) {
      if (painted[index] === null) {
        painted[index] = colors;
      }
    }
  };

  for (const { rule, matcher } of compiled) {
    if (!matcher) {
      continue;
    }
    const ranges = matchRanges(matcher, text);
    if (ranges.length === 0) {
      continue;
    }
    const fore = htmlColor(rule.color);
    const back = rule.useThemeBack ? '' : htmlColor(rule.backColor);
    if (rule.onlyMatch) {
      for (const range of ranges) {
        const colors =
          rule.variateColors && rule.colorVariance > 0
            ? (() => {
                const factor = varianceFactor(range.text, rule.colorVariance);
                return { fore: darker(fore, factor), back: back ? darker(back, factor) : '' };
              })()
            : { fore, back };
        paint(range.start, range.end, colors);
      }
      continue;
    }
    // A whole-line rule paints everything not already owned by a higher
    // priority rule and stops: lower priority rules never repaint a line.
    paint(0, text.length, { fore, back });
    break;
  }

  const spans: RuleSpan[] = [];
  let index = 0;
  while (index < painted.length) {
    const colors = painted[index];
    if (!colors) {
      index++;
      continue;
    }
    let end = index + 1;
    while (end < painted.length) {
      const next = painted[end];
      if (!next || next.fore !== colors.fore || next.back !== colors.back) {
        break;
      }
      end++;
    }
    spans.push({ start: index, end, fore: colors.fore, back: colors.back });
    index = end;
  }
  return spans;
}
