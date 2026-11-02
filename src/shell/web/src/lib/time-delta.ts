// Interval between the first and the last line of a multi-line selection.
//
// Timestamps are recognized in the line prefix only, because message bodies
// often carry dates and times that do not describe the log record itself.
// Everything is converted to integer microseconds on a fixed UTC basis so a
// difference between two lines never accumulates floating point error.

export type DurationUnit = 'h' | 'm' | 's' | 'ms' | 'us';

export type TimestampZone = 'utc' | 'offset' | 'none';

export interface ParsedTimestamp {
  micros: number;
  formatId: string;
  zone: TimestampZone;
  text: string;
}

export interface DurationPart {
  value: string;
  unit: DurationUnit;
}

export interface SelectionTimeDelta {
  deltaUs: number;
  /** Units from the largest non-zero one down, at most three (h, m, s). */
  parts: DurationPart[];
  formatId: string;
  lineCount: number;
}

// What a view reports for its current row selection: the row texts in display
// order plus the number of source lines they cover (wrapped rows do not count).
export interface SelectionLines {
  lines: string[];
  lineCount: number;
}

export const DURATION_UNIT_LABEL: Record<DurationUnit, string> = {
  h: 'h',
  m: 'm',
  s: 's',
  ms: 'ms',
  us: '\u00b5s',
};

export function formatDurationParts(parts: DurationPart[]): string {
  return parts.map((part) => `${part.value} ${DURATION_UNIT_LABEL[part.unit]}`).join(' ');
}

const TIMESTAMP_SEARCH_LIMIT = 64;
// A selection may start or end on a continuation line (stack frame, wrapped
// detail), so both ends are scanned until a timestamp shows up.
const ENDPOINT_SCAN_LIMIT = 256;
const MICROS_PER_MILLI = 1_000;
const MICROS_PER_SECOND = 1_000_000;

const MONTHS = [
  'jan',
  'feb',
  'mar',
  'apr',
  'may',
  'jun',
  'jul',
  'aug',
  'sep',
  'oct',
  'nov',
  'dec',
];

// Zone-less timestamps are compared on their wall clock components, so a DST
// transition or a different machine timezone cannot skew the interval.
const WALL_CLOCK_BASE_YEAR = 2000;

const EPOCH_MIN_MICROS = Date.UTC(2000, 0, 1) * MICROS_PER_MILLI;
const EPOCH_MAX_MICROS = Date.UTC(2100, 0, 1) * MICROS_PER_MILLI;

function fractionMicros(fraction: string | undefined): number {
  if (!fraction) {
    return 0;
  }
  return Number(fraction.slice(0, 6).padEnd(6, '0'));
}

function offsetMinutes(zone: string | undefined): number {
  if (!zone || zone === 'Z' || zone === 'z') {
    return 0;
  }
  const sign = zone[0] === '-' ? -1 : 1;
  const digits = zone.slice(1).replace(':', '');
  return sign * (Number(digits.slice(0, 2)) * 60 + Number(digits.slice(2, 4)));
}

function zoneKind(zone: string | undefined): TimestampZone {
  if (!zone) {
    return 'none';
  }
  return zone === 'Z' || zone === 'z' ? 'utc' : 'offset';
}

function microsOf(
  year: number,
  month: number,
  day: number,
  hour: number,
  minute: number,
  second: number,
  fraction: string | undefined,
  offset: number,
): number {
  const wall = Date.UTC(year, month - 1, day, hour, minute, second);
  return (wall - offset * 60_000) * MICROS_PER_MILLI + fractionMicros(fraction);
}

interface TimestampMatch {
  index: number;
  micros: number;
  zone: TimestampZone;
  text: string;
}

interface TimestampFormat {
  id: string;
  match(text: string): TimestampMatch | null;
}

const DATE_TIME_BODY =
  '([01]\\d|2[0-3]):([0-5]\\d):([0-5]\\d|60)' +
  '(?:[.,](\\d{1,9}))?' +
  '(Z|[+-](?:[01]\\d|2[0-3]):?[0-5]\\d)?';

function dateTimeFormat(id: string, dateSeparator: string): TimestampFormat {
  const pattern = new RegExp(
    `(\\d{4})${dateSeparator}(0[1-9]|1[0-2])${dateSeparator}(0[1-9]|[12]\\d|3[01])[T ]` +
      DATE_TIME_BODY,
  );
  return {
    id,
    match(text) {
      const found = pattern.exec(text);
      if (!found) {
        return null;
      }
      const zone = found[8];
      return {
        index: found.index,
        micros: microsOf(
          Number(found[1]),
          Number(found[2]),
          Number(found[3]),
          Number(found[4]),
          Number(found[5]),
          Number(found[6]),
          found[7],
          offsetMinutes(zone),
        ),
        zone: zoneKind(zone),
        text: found[0],
      };
    },
  };
}

const CLF_PATTERN = new RegExp(
  '(0[1-9]|[12]\\d|3[01])/(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)/(\\d{4})' +
    ':([01]\\d|2[0-3]):([0-5]\\d):([0-5]\\d|60)' +
    '(?:[.,](\\d{1,9}))? ' +
    '([+-](?:[01]\\d|2[0-3]):?[0-5]\\d)',
  'i',
);

const TIME_ONLY_PATTERN = new RegExp(
  '(?<!\\d)([01]\\d|2[0-3]):([0-5]\\d):([0-5]\\d|60)(?:[.,](\\d{1,9}))?(?!\\d)',
);

const SYSLOG_PATTERN = new RegExp(
  '(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) +(0?[1-9]|[12]\\d|3[01]) ' +
    '([01]\\d|2[0-3]):([0-5]\\d):([0-5]\\d)',
  'i',
);

const clfFormat: TimestampFormat = {
  id: 'clf',
  match(text) {
    const found = CLF_PATTERN.exec(text);
    if (!found) {
      return null;
    }
    return {
      index: found.index,
      micros: microsOf(
        Number(found[3]),
        MONTHS.indexOf(found[2].toLowerCase()) + 1,
        Number(found[1]),
        Number(found[4]),
        Number(found[5]),
        Number(found[6]),
        found[7],
        offsetMinutes(found[8]),
      ),
      zone: 'offset',
      text: found[0],
    };
  },
};

const timeOnlyFormat: TimestampFormat = {
  id: 'time-only',
  match(text) {
    const found = TIME_ONLY_PATTERN.exec(text);
    if (!found) {
      return null;
    }
    return {
      index: found.index,
      micros: microsOf(
        WALL_CLOCK_BASE_YEAR,
        1,
        1,
        Number(found[1]),
        Number(found[2]),
        Number(found[3]),
        found[4],
        0,
      ),
      zone: 'none',
      text: found[0],
    };
  },
};

const syslogFormat: TimestampFormat = {
  id: 'syslog',
  match(text) {
    const found = SYSLOG_PATTERN.exec(text);
    if (!found) {
      return null;
    }
    return {
      index: found.index,
      micros: microsOf(
        WALL_CLOCK_BASE_YEAR,
        MONTHS.indexOf(found[1].toLowerCase()) + 1,
        Number(found[2]),
        Number(found[3]),
        Number(found[4]),
        Number(found[5]),
        undefined,
        0,
      ),
      zone: 'none',
      text: found[0],
    };
  },
};

interface EpochSpec {
  id: string;
  digits: number;
  toMicros(value: string): number;
}

const EPOCH_SPECS: EpochSpec[] = [
  { id: 'epoch-s', digits: 10, toMicros: (value) => Number(value) * MICROS_PER_SECOND },
  { id: 'epoch-ms', digits: 13, toMicros: (value) => Number(value) * MICROS_PER_MILLI },
  { id: 'epoch-us', digits: 16, toMicros: (value) => Number(value) },
  // Nanosecond values exceed Number.MAX_SAFE_INTEGER: microseconds are the
  // leading digits and the last three nanoseconds are dropped.
  { id: 'epoch-ns', digits: 19, toMicros: (value) => Number(value.slice(0, 16)) },
];

// Epoch stamps are plain numbers, so they are only accepted at the very start
// of the line and only when they land in a plausible wall clock window.
function epochFormat(spec: EpochSpec): TimestampFormat {
  const pattern = new RegExp(`^\\[?(\\d{${spec.digits}})(?!\\d)`);
  return {
    id: spec.id,
    match(text) {
      const found = pattern.exec(text);
      if (!found) {
        return null;
      }
      const micros = spec.toMicros(found[1]);
      if (micros < EPOCH_MIN_MICROS || micros >= EPOCH_MAX_MICROS) {
        return null;
      }
      return { index: found.index, micros, zone: 'utc', text: found[0] };
    },
  };
}

// Order is the tie breaker: when two formats match at the same offset the more
// specific one (listed first) wins.
const TIMESTAMP_FORMATS: TimestampFormat[] = [
  dateTimeFormat('iso8601', '-'),
  dateTimeFormat('slash-datetime', '/'),
  clfFormat,
  timeOnlyFormat,
  syslogFormat,
  ...EPOCH_SPECS.map(epochFormat),
];

export function parseTimestamp(text: string): ParsedTimestamp | null {
  if (!text) {
    return null;
  }
  const truncated = text.length > TIMESTAMP_SEARCH_LIMIT;
  const head = truncated ? text.slice(0, TIMESTAMP_SEARCH_LIMIT) : text;

  let best: TimestampMatch | null = null;
  let bestFormat = '';
  for (const format of TIMESTAMP_FORMATS) {
    const found = format.match(head);
    if (!found) {
      continue;
    }
    // A match touching the cut is probably a timestamp that continues past it.
    if (truncated && found.index + found.text.length >= TIMESTAMP_SEARCH_LIMIT) {
      continue;
    }
    if (best === null || found.index < best.index) {
      best = found;
      bestFormat = format.id;
    }
  }

  if (!best) {
    return null;
  }
  return { micros: best.micros, formatId: bestFormat, zone: best.zone, text: best.text };
}

function trimTrailingZeros(value: number): string {
  return value.toFixed(3).replace(/\.?0+$/, '');
}

// Largest non-zero unit first, at most three parts (h, m, s). Only the smallest
// shown unit keeps decimals, so a minute-scale interval reads "1 m 10.235 s"
// instead of a bare "70 s". Trailing zero parts are dropped, interior ones are
// kept ("1 h 0 m 5 s") so a part never reads as a smaller unit than it is.
export function formatDuration(deltaUs: number): DurationPart[] {
  // Lines written in the same millisecond are a valid interval, not a missing
  // one: show it as "0 s" instead of hiding the badge.
  if (deltaUs <= 0) {
    return [{ value: '0', unit: 's' }];
  }
  if (deltaUs < MICROS_PER_MILLI) {
    return [{ value: String(Math.round(deltaUs)), unit: 'us' }];
  }
  if (deltaUs < MICROS_PER_SECOND) {
    return [{ value: trimTrailingZeros(deltaUs / MICROS_PER_MILLI), unit: 'ms' }];
  }
  if (deltaUs < 60 * MICROS_PER_SECOND) {
    return [{ value: trimTrailingZeros(deltaUs / MICROS_PER_SECOND), unit: 's' }];
  }

  const totalSeconds = deltaUs / MICROS_PER_SECOND;
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds - hours * 3600) / 60);
  const seconds = totalSeconds - hours * 3600 - minutes * 60;
  const parts: DurationPart[] = [];
  if (hours > 0) {
    parts.push({ value: String(hours), unit: 'h' });
  }
  if (hours > 0 || minutes > 0) {
    parts.push({ value: String(minutes), unit: 'm' });
  }
  parts.push({ value: trimTrailingZeros(seconds), unit: 's' });
  while (parts.length > 1 && parts[parts.length - 1].value === '0') {
    parts.pop();
  }
  return parts;
}

interface EndpointTimestamp {
  parsed: ParsedTimestamp;
  index: number;
}

function findEndpointTimestamp(lines: string[], backwards: boolean): EndpointTimestamp | null {
  const count = Math.min(lines.length, ENDPOINT_SCAN_LIMIT);
  for (let step = 0; step < count; step++) {
    const index = backwards ? lines.length - 1 - step : step;
    const parsed = parseTimestamp(lines[index]);
    if (parsed) {
      return { parsed, index };
    }
  }
  return null;
}

export function computeSelectionTimeDelta(
  lines: string[],
  lineCount: number = lines.length,
): SelectionTimeDelta | null {
  if (lineCount < 2 || lines.length < 2) {
    return null;
  }

  const start = findEndpointTimestamp(lines, false);
  const end = findEndpointTimestamp(lines, true);
  if (!start || !end) {
    return null;
  }
  // The scan found the same line from both ends: the selection holds a single
  // timestamp (continuation lines around one record), so there is no interval.
  if (start.index === end.index) {
    return null;
  }
  // Different formats or mixing zoned with zone-less stamps means the two
  // values are not on a comparable basis.
  if (
    start.parsed.formatId !== end.parsed.formatId ||
    (start.parsed.zone === 'none') !== (end.parsed.zone === 'none')
  ) {
    return null;
  }

  const deltaUs = end.parsed.micros - start.parsed.micros;
  // Lines of one millisecond share a timestamp, which is a real zero interval;
  // an end before the start means the file is not ordered by time and the two
  // values are not a duration.
  if (deltaUs < 0) {
    return null;
  }

  return {
    deltaUs,
    parts: formatDuration(deltaUs),
    formatId: start.parsed.formatId,
    lineCount,
  };
}
