export interface TabOrderEntry {
  id: string;
  pinned?: boolean;
}

export interface TabMove {
  id: string;
  targetId: string;
  after: boolean;
}

/**
 * Moves a tab before or after another one, the way dragging a tab in the
 * desktop tab bar does. Pinned and unpinned tabs are separate groups that the
 * session stores in order, so a move across the boundary is rejected instead of
 * silently repinning the tab. The input list is returned unchanged when the move
 * would not change anything.
 */
export function reorderTabList<T extends TabOrderEntry>(list: T[], move: TabMove): T[] {
  const moved = list.find((tab) => tab.id === move.id);
  const target = list.find((tab) => tab.id === move.targetId);
  if (!moved || !target || moved.id === target.id) {
    return list;
  }
  if (Boolean(moved.pinned) !== Boolean(target.pinned)) {
    return list;
  }
  const rest = list.filter((tab) => tab.id !== moved.id);
  const targetIndex = rest.findIndex((tab) => tab.id === move.targetId);
  if (targetIndex < 0) {
    return list;
  }
  const insertAt = move.after ? targetIndex + 1 : targetIndex;
  rest.splice(insertAt, 0, moved);
  // Dropping a tab right next to itself is not a move either.
  if (rest.every((tab, index) => tab === list[index])) {
    return list;
  }
  return rest;
}
