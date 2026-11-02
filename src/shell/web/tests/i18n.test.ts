import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { join, resolve } from 'node:path';
import test from 'node:test';

const sourceRoot = resolve(import.meta.dirname, '../src');
const i18nPath = join(sourceRoot, 'lib', 'i18n.ts');

function sourceFiles(directory: string): string[] {
  return readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    const path = join(directory, entry.name);
    if (entry.isDirectory()) {
      return sourceFiles(path);
    }
    return entry.name.endsWith('.svelte') || entry.name.endsWith('.ts') ? [path] : [];
  });
}

test('all referenced i18n keys exist in both languages', () => {
  const i18n = readFileSync(i18nPath, 'utf8');
  const [zhText, enText] = i18n.split('const en: Record<string, string> = {', 2);
  const keyPattern = /^\s*'([^']+)':/gm;
  const zhKeys = new Set([...zhText.matchAll(keyPattern)].map((match) => match[1]));
  const enKeys = new Set([...enText.matchAll(keyPattern)].map((match) => match[1]));

  const required = new Set<string>();
  for (const path of sourceFiles(sourceRoot)) {
    if (path === i18nPath) {
      continue;
    }
    const source = readFileSync(path, 'utf8');
    for (const match of source.matchAll(/\$t\(\s*'([^']+)'/g)) {
      required.add(match[1]);
    }
    for (const match of source.matchAll(/\bt\(\s*'([^']+)'/g)) {
      required.add(match[1]);
    }
    for (const match of source.matchAll(/labelKey:\s*'([^']+)'/g)) {
      required.add(match[1]);
    }
  }

  assert.deepEqual([...required].filter((key) => !zhKeys.has(key)).sort(), []);
  assert.deepEqual([...required].filter((key) => !enKeys.has(key)).sort(), []);
  assert.deepEqual([...zhKeys].filter((key) => !enKeys.has(key)).sort(), []);
  assert.deepEqual([...enKeys].filter((key) => !zhKeys.has(key)).sort(), []);
});
