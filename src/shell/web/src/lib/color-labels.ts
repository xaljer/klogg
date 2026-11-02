import type { QuickHighlighterDef } from './types.ts';

export type ColorLabelSlots = string[][];

/**
 * Color label state shared by the main view and the search view.
 *
 * The desktop UI keys a label by the selected text itself (a portion of a line
 * or a whole line, never trimmed) and keeps the words of every slot in one
 * collection that both views render from. The helpers below are that model:
 * they hold no view state, so the two views cannot drift.
 */

/** Label key of a selection: the text as selected, without the trailing newline. */
export function colorLabelKey(text: string): string {
  return text.replace(/[\r\n]+$/, '');
}

export function colorLabelOf(slots: ColorLabelSlots, text: string): number {
  if (!text) {
    return -1;
  }
  for (let index = 0; index < slots.length; index++) {
    if (slots[index]?.includes(text)) {
      return index;
    }
  }
  return -1;
}

/**
 * Applies a label the way `ColorLabelsManager::updateColorLabel` does: the text
 * leaves every slot first, and only enters the target slot when it was either
 * unlabelled or labelled elsewhere. Re-applying the label it already has clears
 * it.
 */
export function applyColorLabel(
  slots: ColorLabelSlots,
  label: number,
  text: string,
): ColorLabelSlots {
  if (!text || label < 0) {
    return slots;
  }
  const next = slots.map((words) => words.filter((item) => item !== text));
  const wasLabelledElsewhere = slots.some(
    (words, index) => index !== label && words.includes(text),
  );
  const wasLabelledHere = slots[label]?.includes(text) ?? false;
  if (!wasLabelledHere || wasLabelledElsewhere) {
    next[label] = [...(next[label] ?? []), text];
  }
  return next;
}

/**
 * Colour label cycling (the desktop "next color label" action) only visits the
 * slots marked for the cycle, and re-applying it to the text it already labels
 * removes the text instead of moving it to the next slot.
 */
export function applyNextColorLabel(
  slots: ColorLabelSlots,
  defs: QuickHighlighterDef[],
  lastLabel: number,
  text: string,
): { slots: ColorLabelSlots; label: number } {
  const cycle = defs
    .map((def, index) => ({ def, index }))
    .filter((entry) => entry.def.useInCycle !== false)
    .map((entry) => entry.index);
  if (!text || cycle.length === 0) {
    return { slots, label: lastLabel };
  }
  const nextLabel = cycle.find((index) => index > lastLabel) ?? cycle[0];
  if (colorLabelOf(slots, text) >= 0) {
    return { slots: clearColorLabel(slots, text), label: nextLabel };
  }
  return { slots: applyColorLabel(slots, nextLabel, text), label: nextLabel };
}

export function clearColorLabel(slots: ColorLabelSlots, text: string): ColorLabelSlots {
  if (!text) {
    return slots;
  }
  return slots.map((words) => words.filter((item) => item !== text));
}

export interface ColorLabelRange {
  start: number;
  end: number;
  label: number;
}

/**
 * Plain text, case sensitive occurrences of every labelled word, in the same
 * order the core composes them (colour labels are matched after the search
 * highlight, so they win over it).
 */
export function colorLabelRanges(
  slots: ColorLabelSlots,
  line: string,
  labelCount: number,
): ColorLabelRange[] {
  const ranges: ColorLabelRange[] = [];
  if (!line) {
    return ranges;
  }
  const slotCount = Math.min(slots.length, labelCount);
  for (let label = 0; label < slotCount; label++) {
    for (const word of slots[label] ?? []) {
      if (!word) {
        continue;
      }
      let from = 0;
      while (from <= line.length - word.length) {
        const index = line.indexOf(word, from);
        if (index < 0) {
          break;
        }
        ranges.push({ start: index, end: index + word.length, label });
        from = index + word.length;
      }
    }
  }
  return ranges;
}
