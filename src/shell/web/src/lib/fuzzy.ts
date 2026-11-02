export function fuzzyScore(value: string, query: string): number {
  const needle = query.trim().toLowerCase();
  if (!needle) {
    return 1;
  }
  const haystack = value.toLowerCase();
  const substringIndex = haystack.indexOf(needle);
  if (substringIndex >= 0) {
    return 1000 - substringIndex;
  }

  let position = -1;
  let gaps = 0;
  for (const character of needle) {
    const next = haystack.indexOf(character, position + 1);
    if (next < 0) {
      return 0;
    }
    gaps += Math.max(0, next - position - 1);
    position = next;
  }
  return 100 - Math.min(99, gaps);
}

export function fuzzyFilter<T>(items: T[], query: string, key: (item: T) => string): T[] {
  if (!query.trim()) {
    return items;
  }
  return items
    .map((item, index) => ({ item, index, score: fuzzyScore(key(item), query) }))
    .filter((entry) => entry.score > 0)
    .sort((left, right) => right.score - left.score || left.index - right.index)
    .map((entry) => entry.item);
}
