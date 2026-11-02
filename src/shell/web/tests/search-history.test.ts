import assert from 'node:assert/strict';
import { beforeEach, test } from 'node:test';

import {
  clearSearchHistory,
  loadSearchHistory,
  rememberSearch,
  removeSearchHistory,
} from '../src/lib/search-history.ts';
import { installMemoryStorage } from './support/local-storage.ts';

const memory = installMemoryStorage();

beforeEach(() => {
  memory.clear();
});

test('search history trims, deduplicates and moves entries to the front', () => {
  assert.deepEqual(rememberSearch('  INFO  '), ['INFO']);
  rememberSearch('WARN');
  assert.deepEqual(loadSearchHistory(), ['WARN', 'INFO']);
  rememberSearch('INFO');
  assert.deepEqual(loadSearchHistory(), ['INFO', 'WARN']);
});

test('search history ignores blank patterns', () => {
  rememberSearch('INFO');
  assert.deepEqual(rememberSearch('   '), ['INFO']);
});

test('search history removal keeps the rest of the list', () => {
  rememberSearch('one');
  rememberSearch('two');
  assert.deepEqual(removeSearchHistory('one'), ['two']);
  assert.deepEqual(loadSearchHistory(), ['two']);
});

test('search history clear removes the storage key', () => {
  rememberSearch('one');
  clearSearchHistory();
  assert.deepEqual(loadSearchHistory(), []);
  assert.equal(localStorage.getItem('klogg.web.searchHistory'), null);
});
