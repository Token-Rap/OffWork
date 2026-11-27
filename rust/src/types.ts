export type EndMode = "endTime" | "duration";
export type Field = "start" | "end" | "salary" | "lunchStart" | "lunchEnd" | "resignation";

export interface Settings {
  startMinutes: number;
  endMinutes: number;
  lunchStartMinutes: number;
  lunchEndMinutes: number;
  dailySalary: number;
  scalePercent: number;
  opacityPercent: number;
  hideEarned: boolean;
  deductLunch: boolean;
  resignationDate: string;
  showResignation: boolean;
  endMode: EndMode;
}

export type Fields = Record<Field, string>;

export interface UpdateView {
  available: string | null;
  downloading: boolean;
  percent: number;
  error: string | null;
  installs: boolean;
}

export interface View {
  settings: Settings;
  fields: Fields;
  notice: string;
  error: string | null;
  pinSupported: boolean;
  pinned: boolean;
  tray: boolean;
  autostart: { enabled: boolean; otherTarget: string | null; error: string | null };
  version: string;
  update: UpdateView;
}

export interface Tick {
  phase: "Before" | "Working" | "After";
  remaining: number;
  countdown: number;
  onLunch: boolean;
  progress: number;
  earned: number;
  endMinutes: number;
  days: number | null;
  date: string;
  time: string;
}

/** Characters each field accepts, like the C++ text filters. */
export const FILTERS: Record<Field, { chars: RegExp; max: number; separator: string; count: number }> = {
  start: { chars: /[0-9:]/, max: 5, separator: ":", count: 1 },
  end: { chars: /[0-9:]/, max: 5, separator: ":", count: 1 },
  salary: { chars: /[0-9.]/, max: 16, separator: ".", count: 1 },
  lunchStart: { chars: /[0-9:]/, max: 5, separator: ":", count: 1 },
  lunchEnd: { chars: /[0-9:]/, max: 5, separator: ":", count: 1 },
  resignation: { chars: /[0-9-]/, max: 10, separator: "-", count: 2 },
};

/** Applies a field's filter to typed or pasted text (first line only). */
export function filterText(field: Field, text: string, mode: EndMode): string {
  const spec = field === "end" && mode === "duration" ? FILTERS.salary : FILTERS[field];
  let out = "";
  let separators = 0;
  for (const c of text.split(/\r|\n/)[0]) {
    if (!spec.chars.test(c)) continue;
    if (c === spec.separator && ++separators > spec.count) continue;
    out += c;
  }
  return out.slice(0, field === "end" && mode === "duration" ? 5 : spec.max);
}
