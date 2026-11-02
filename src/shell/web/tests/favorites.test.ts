import assert from 'node:assert/strict';
import { beforeEach, test } from 'node:test';

import {
  addFavorite,
  isFavorite,
  loadFavorites,
  removeFavorite,
  toggleFavorite,
} from '../src/lib/favorites.ts';
import { installMemoryStorage } from './support/local-storage.ts';

const memory = installMemoryStorage();

beforeEach(() => {
  memory.clear();
});

test('favorites deduplicate paths and compare Windows paths case-insensitively', () => {
  let favorites = addFavorite('C:\\Logs\\App.LOG', []);
  assert.deepEqual(favorites, ['C:\\Logs\\App.LOG']);
  favorites = addFavorite('c:/logs/app.log', favorites);
  assert.deepEqual(favorites, ['c:/logs/app.log']);
  assert.equal(isFavorite('C:\\LOGS\\APP.LOG', favorites), true);
});

test('favorites toggle adds and removes a path', () => {
  let favorites = toggleFavorite('D:\\logs\\a.log', []);
  assert.deepEqual(favorites, ['D:\\logs\\a.log']);
  favorites = toggleFavorite('D:\\logs\\a.log', favorites);
  assert.deepEqual(favorites, []);
});

test('favorites load only non-empty strings', () => {
  localStorage.setItem('klogg.web.favorites', JSON.stringify(['a.log', 4, '', 'b.log']));
  assert.deepEqual(loadFavorites(), ['a.log', 'b.log']);
});

test('favorites removal does not mutate the input array', () => {
  const favorites = ['a.log', 'b.log'];
  assert.deepEqual(removeFavorite('a.log', favorites), ['b.log']);
  assert.deepEqual(favorites, ['a.log', 'b.log']);
});
