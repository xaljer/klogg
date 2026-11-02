import assert from 'node:assert/strict';
import test from 'node:test';

import {
  computeSelectionTimeDelta,
  formatDuration,
  formatDurationParts,
  parseTimestamp,
} from '../src/lib/time-delta.ts';

function microsOf(text: string): number {
  const parsed = parseTimestamp(text);
  assert.ok(parsed, `expected a timestamp in ${JSON.stringify(text)}`);
  return parsed.micros;
}

function deltaOf(lines: string[], lineCount?: number) {
  return lineCount === undefined
    ? computeSelectionTimeDelta(lines)
    : computeSelectionTimeDelta(lines, lineCount);
}

function labelOf(delta: ReturnType<typeof computeSelectionTimeDelta>): string {
  assert.ok(delta, 'expected a computed interval');
  return formatDurationParts(delta.parts);
}

test('iso8601 accepts T or space, fraction separators and zone suffixes', () => {
  assert.equal(parseTimestamp('2026-10-25T10:00:00.123Z')?.formatId, 'iso8601');
  assert.equal(parseTimestamp('2026-10-25 10:00:00,123')?.formatId, 'iso8601');
  assert.equal(parseTimestamp('2026-10-25T10:00:00.123456789Z')?.formatId, 'iso8601');
  assert.equal(parseTimestamp('2026-10-25T10:00:00.123+08:00')?.zone, 'offset');
  assert.equal(parseTimestamp('2026-10-25T10:00:00.123Z')?.zone, 'utc');
  assert.equal(parseTimestamp('2026-10-25 10:00:00.123')?.zone, 'none');
});

test('iso8601 truncates nanoseconds to microseconds', () => {
  assert.equal(
    microsOf('2026-10-25T10:00:00.123456789Z'),
    microsOf('2026-10-25T10:00:00.123456Z'),
  );
  assert.equal(
    microsOf('2026-10-25T10:00:00.5Z') - microsOf('2026-10-25T10:00:00Z'),
    500_000,
  );
});

test('iso8601 offsets resolve to the same instant', () => {
  assert.equal(
    microsOf('2026-10-25T02:00:00Z'),
    microsOf('2026-10-25T10:00:00+08:00'),
  );
  assert.equal(
    microsOf('2026-10-25T10:00:00-05:30'),
    microsOf('2026-10-25T15:30:00Z'),
  );
});

test('slash dates, CLF, time-only and syslog formats are recognized', () => {
  assert.equal(parseTimestamp('2026/10/25 10:00:00.500 INFO')?.formatId, 'slash-datetime');
  assert.equal(
    parseTimestamp('[25/Oct/2026:10:00:00.250 +0800] GET /')?.formatId,
    'clf',
  );
  assert.equal(parseTimestamp('10:00:00,100 INFO tick')?.formatId, 'time-only');
  assert.equal(parseTimestamp('Oct  5 12:34:56 host app: message')?.formatId, 'syslog');
  assert.equal(parseTimestamp('Oct 5 12:34:56 host app: message')?.formatId, 'syslog');
});

test('clf and syslog read month names case-insensitively', () => {
  assert.equal(
    microsOf('[25/oct/2026:10:00:00 +0000] a'),
    microsOf('[25/Oct/2026:10:00:00 +0000] a'),
  );
  assert.equal(microsOf('OCT  5 12:34:56 a'), microsOf('Oct  5 12:34:56 a'));
});

test('epoch stamps are accepted only at the line start and in a plausible window', () => {
  assert.equal(parseTimestamp('1729831200 INFO')?.formatId, 'epoch-s');
  assert.equal(parseTimestamp('1729831200123 INFO')?.formatId, 'epoch-ms');
  assert.equal(parseTimestamp('1729831200123456 INFO')?.formatId, 'epoch-us');
  assert.equal(parseTimestamp('1729831200123456789 INFO')?.formatId, 'epoch-ns');
  assert.equal(parseTimestamp('[1729831200123] INFO')?.formatId, 'epoch-ms');
  assert.deepEqual(parseTimestamp('1729831200'), parseTimestamp('1729831200 '));
  assert.equal(parseTimestamp('9999999999 INFO'), null, 'year beyond the window');
  assert.equal(parseTimestamp('0000000000 INFO'), null, 'before the window');
  assert.equal(parseTimestamp('id=1729831200 INFO'), null, 'not at the line start');
});

test('epoch microseconds survive the 13 to 16 digit step exactly', () => {
  assert.equal(
    microsOf('1729831200123456') - microsOf('1729831200000000'),
    123_456,
  );
  assert.equal(
    microsOf('1729831200123456789') - microsOf('1729831200000000000'),
    123_456,
  );
});

test('the leftmost timestamp wins over a later, shorter match', () => {
  const iso = parseTimestamp('2026-10-25T10:00:00.123Z payload 11:22:33');
  assert.equal(iso?.formatId, 'iso8601');
  assert.equal(iso?.text, '2026-10-25T10:00:00.123Z');

  const trailing = parseTimestamp('INFO 10:00:00 tick');
  assert.equal(trailing?.formatId, 'time-only');
  assert.equal(trailing?.text, '10:00:00');
});

test('lines without a timestamp in the prefix return null', () => {
  assert.equal(parseTimestamp(''), null);
  assert.equal(parseTimestamp('INFO plain message without time'), null);
  assert.equal(parseTimestamp(`${'x'.repeat(66)}2026-10-25T10:00:00Z`), null);
});

test('formatDuration picks the unit from the magnitude and trims zeros', () => {
  assert.deepEqual(formatDuration(999), [{ value: '999', unit: 'us' }]);
  assert.deepEqual(formatDuration(1_000), [{ value: '1', unit: 'ms' }]);
  assert.deepEqual(formatDuration(1_250), [{ value: '1.25', unit: 'ms' }]);
  assert.deepEqual(formatDuration(999_999), [{ value: '999.999', unit: 'ms' }]);
  assert.deepEqual(formatDuration(1_000_000), [{ value: '1', unit: 's' }]);
  assert.deepEqual(formatDuration(1_500_000), [{ value: '1.5', unit: 's' }]);
  assert.deepEqual(formatDuration(1_234_567), [{ value: '1.235', unit: 's' }]);
  assert.deepEqual(formatDuration(59_500_000), [{ value: '59.5', unit: 's' }]);
});

test('formatDuration carries minutes and hours from the sixtieth second on', () => {
  assert.deepEqual(formatDuration(60_000_000), [{ value: '1', unit: 'm' }]);
  assert.deepEqual(formatDuration(70_000_000), [
    { value: '1', unit: 'm' },
    { value: '10', unit: 's' },
  ]);
  assert.deepEqual(formatDuration(90_500_000), [
    { value: '1', unit: 'm' },
    { value: '30.5', unit: 's' },
  ]);
  assert.deepEqual(formatDuration(300_000_000), [{ value: '5', unit: 'm' }]);
  assert.deepEqual(formatDuration(3_599_000_000), [
    { value: '59', unit: 'm' },
    { value: '59', unit: 's' },
  ]);
  assert.deepEqual(formatDuration(3_600_000_000), [{ value: '1', unit: 'h' }]);
  assert.deepEqual(formatDuration(3_661_500_000), [
    { value: '1', unit: 'h' },
    { value: '1', unit: 'm' },
    { value: '1.5', unit: 's' },
  ]);
  // Interior zero parts are kept so a part never reads as a smaller unit.
  assert.deepEqual(formatDuration(3_600_500_000), [
    { value: '1', unit: 'h' },
    { value: '0', unit: 'm' },
    { value: '0.5', unit: 's' },
  ]);
  // Hours never roll over into days: three parts at most, seconds stay exact.
  assert.deepEqual(formatDuration(90_000_000_000), [{ value: '25', unit: 'h' }]);
  assert.equal(formatDurationParts(formatDuration(90_000_000_000)), '25 h');
});

test('a multi-line selection reports the interval between its ends', () => {
  const lines = [
    '2026-10-25 10:00:00.000 INFO start',
    '    at foo(bar.ts:1)',
    '2026-10-25 10:00:01.500 ERROR boom',
    '    at baz(qux.ts:2)',
  ];
  const delta = deltaOf(lines);
  assert.ok(delta);
  assert.equal(delta.deltaUs, 1_500_000);
  assert.equal(formatDurationParts(delta.parts), '1.5 s');
  assert.equal(delta.formatId, 'iso8601');
  assert.equal(delta.lineCount, 4);
});

test('continuation lines at both ends are skipped when looking for timestamps', () => {
  const lines = [
    '    at foo(bar.ts:1)',
    '2026-10-25 10:00:00.000 INFO start',
    '2026-10-25 10:00:00.250 INFO middle',
    '2026-10-25 10:00:00.500 INFO end',
    '    at baz(qux.ts:2)',
  ];
  const delta = deltaOf(lines);
  assert.ok(delta);
  assert.equal(delta.deltaUs, 500_000);
  assert.equal(formatDurationParts(delta.parts), '500 ms');
});

test('every supported format produces a delta', () => {
  assert.equal(labelOf(deltaOf(['2026-10-25T10:00:00Z a', '2026-10-25T10:00:02Z b'])), '2 s');
  assert.equal(labelOf(deltaOf(['2026/10/25 10:00:00 a', '2026/10/25 10:00:03 b'])), '3 s');
  assert.equal(
    labelOf(deltaOf(['[25/Oct/2026:10:00:00 +0800] a', '[25/Oct/2026:10:00:04 +0800] b'])),
    '4 s',
  );
  assert.equal(
    labelOf(deltaOf(['10:00:00.000 a', '10:00:00.250 b'])),
    '250 ms',
    'sub-second deltas keep milliseconds',
  );
  assert.equal(deltaOf(['Oct  5 12:00:00 a', 'Oct  5 12:00:05 b'])?.deltaUs, 5_000_000);
  assert.equal(labelOf(deltaOf(['1729831200000 a', '1729831201500 b'])), '1.5 s');
});

test('zoned ends compare on the absolute instant', () => {
  const delta = deltaOf(['2026-10-25T10:00:00Z a', '2026-10-25T18:00:01+08:00 b']);
  assert.ok(delta);
  assert.equal(delta.deltaUs, 1_000_000);
});

test('a single line, a missing timestamp or a negative span produces no delta', () => {
  assert.equal(deltaOf(['2026-10-25 10:00:00 only line']), null);
  assert.equal(
    deltaOf(['2026-10-25 10:00:00 a', '2026-10-25 10:00:01 b'], 1),
    null,
    'source line count below two',
  );
  assert.equal(deltaOf(['plain a', 'plain b']), null);
  assert.equal(deltaOf(['plain a', '2026-10-25 10:00:00 b']), null);
  assert.equal(
    deltaOf(['2026-10-25 10:00:02 a', '2026-10-25 10:00:01 b']),
    null,
    'end before start',
  );
});

test('lines sharing one timestamp report a zero interval', () => {
  const delta = deltaOf(['2026-10-25 10:00:00 a', '2026-10-25 10:00:00 b']);
  assert.ok(delta);
  assert.equal(delta.deltaUs, 0);
  assert.deepEqual(delta.parts, [{ value: '0', unit: 's' }]);
  assert.equal(labelOf(delta), '0 s');
  // Several lines in the same millisecond, the shape a burst of log records has.
  assert.deepEqual(formatDuration(0), [{ value: '0', unit: 's' }]);
});

test('mixed formats or mixed zone awareness produce no delta', () => {
  assert.equal(
    deltaOf(['2026-10-25 10:00:00.000 a', '2026/10/25 10:00:01.000 b']),
    null,
    'different formats',
  );
  assert.equal(
    deltaOf(['2026-10-25T10:00:00Z a', '2026-10-25 10:00:01 b']),
    null,
    'zoned against zone-less',
  );
  assert.equal(
    deltaOf(['1729831200 a', '1729831201000 b']),
    null,
    'different epoch units',
  );
});
