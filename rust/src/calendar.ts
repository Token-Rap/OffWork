// Pure civil-date helpers for the resignation calendar: no Date objects, so
// no time zone or DST can move a day. Range 0001-01-01 … 9999-12-31.

export interface Ymd {
  y: number;
  m: number; // 1–12
  d: number;
}

const MIN = { y: 1, m: 1, d: 1 };
const MAX = { y: 9999, m: 12, d: 31 };

export const pad = (n: number, w = 2) => String(n).padStart(w, "0");
export const format = (v: Ymd) => `${pad(v.y, 4)}-${pad(v.m)}-${pad(v.d)}`;

export function daysInMonth(y: number, m: number): number {
  if (m === 2) return y % 4 === 0 && (y % 100 !== 0 || y % 400 === 0) ? 29 : 28;
  return [4, 6, 9, 11].includes(m) ? 30 : 31;
}

/** Strict YYYY-MM-DD, the same rule as the Rust core. */
export function parse(text: string): Ymd | null {
  const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(text);
  if (!match) return null;
  const [y, m, d] = match.slice(1).map(Number);
  if (y < 1 || m < 1 || m > 12 || d < 1 || d > daysInMonth(y, m)) return null;
  return { y, m, d };
}

// Howard Hinnant's days_from_civil / civil_from_days.
export function toDays({ y, m, d }: Ymd): number {
  const yy = m <= 2 ? y - 1 : y;
  const era = Math.floor(yy / 400);
  const yoe = yy - era * 400;
  const doy = Math.floor((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5) + d - 1;
  const doe = yoe * 365 + Math.floor(yoe / 4) - Math.floor(yoe / 100) + doy;
  return era * 146097 + doe - 719468;
}

export function fromDays(z: number): Ymd {
  z += 719468;
  const era = Math.floor(z / 146097);
  const doe = z - era * 146097;
  const yoe = Math.floor((doe - Math.floor(doe / 1460) + Math.floor(doe / 36524) - Math.floor(doe / 146096)) / 365);
  const doy = doe - (365 * yoe + Math.floor(yoe / 4) - Math.floor(yoe / 100));
  const mp = Math.floor((5 * doy + 2) / 153);
  const d = doy - Math.floor((153 * mp + 2) / 5) + 1;
  const m = mp < 10 ? mp + 3 : mp - 9;
  return { y: yoe + era * 400 + (m <= 2 ? 1 : 0), m, d };
}

function clamp(v: Ymd): Ymd {
  const days = toDays(v);
  if (days < toDays(MIN)) return MIN;
  if (days > toDays(MAX)) return MAX;
  return v;
}

export const addDays = (v: Ymd, n: number) => clamp(fromDays(toDays(v) + n));

/** Same day in another month, clamped to that month's length. */
export function addMonths(v: Ymd, n: number): Ymd {
  const index = v.y * 12 + (v.m - 1) + n;
  const y = Math.floor(index / 12);
  const m = index - y * 12 + 1;
  if (y < 1) return MIN;
  if (y > 9999) return MAX;
  return { y, m, d: Math.min(v.d, daysInMonth(y, m)) };
}

/** Monday = 0 … Sunday = 6. */
export const weekday = (v: Ymd) => (((toDays(v) + 3) % 7) + 7) % 7;

/** Six Monday-first weeks covering the month of `v`; null pads out-of-range days. */
export function grid(v: Ymd): (Ymd | null)[] {
  const first = { y: v.y, m: v.m, d: 1 };
  const start = toDays(first) - weekday(first);
  return Array.from({ length: 42 }, (_, i) => {
    const days = start + i;
    return days < toDays(MIN) || days > toDays(MAX) ? null : fromDays(days);
  });
}

export const same = (a: Ymd | null, b: Ymd | null) =>
  !!a && !!b && a.y === b.y && a.m === b.m && a.d === b.d;

export type KeyResult = { focus: Ymd } | { pick: Ymd } | { cancel: true } | null;

/** Keyboard model of the native month calendar the C++ app used. */
export function key(focus: Ymd, k: string, shift = false): KeyResult {
  switch (k) {
    case "ArrowLeft":
      return { focus: addDays(focus, -1) };
    case "ArrowRight":
      return { focus: addDays(focus, 1) };
    case "ArrowUp":
      return { focus: addDays(focus, -7) };
    case "ArrowDown":
      return { focus: addDays(focus, 7) };
    case "Home":
      return { focus: addDays(focus, -weekday(focus)) };
    case "End":
      return { focus: addDays(focus, 6 - weekday(focus)) };
    case "PageUp":
      return { focus: addMonths(focus, shift ? -12 : -1) };
    case "PageDown":
      return { focus: addMonths(focus, shift ? 12 : 1) };
    case "Enter":
    case " ":
      return { pick: focus };
    case "Escape":
      return { cancel: true };
    default:
      return null;
  }
}
