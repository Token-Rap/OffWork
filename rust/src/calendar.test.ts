import { describe, expect, it } from "vitest";
import { addDays, addMonths, format, fromDays, grid, key, parse, toDays, weekday } from "./calendar";

describe("dates", () => {
  it("parses strictly like the Rust core", () => {
    for (const bad of ["", "2026-2-01", "2026-02-29", "1900-02-29", "0000-01-01", "10000-01-01",
      "2026-13-01", "2026-04-31", "2026-01-00", "2026-11-30junk", " 2026-11-30", "2026/11/30"])
      expect(parse(bad), bad).toBeNull();
    for (const ok of ["2000-02-29", "0001-01-01", "9999-12-31", "2028-02-29"])
      expect(format(parse(ok)!)).toBe(ok);
  });
  it("round-trips day numbers across the whole range", () => {
    for (const text of ["0001-01-01", "1601-01-01", "1970-01-01", "2026-11-26", "9999-12-31"]) {
      const v = parse(text)!;
      expect(format(fromDays(toDays(v)))).toBe(text);
    }
    expect(toDays(parse("1970-01-01")!)).toBe(0);
    expect(weekday(parse("2026-11-26")!)).toBe(3); // Thursday
  });
  it("moves by month with clamped days and stays in range", () => {
    expect(format(addMonths(parse("2027-01-31")!, 1))).toBe("2027-02-28");
    expect(format(addMonths(parse("2027-03-15")!, 1))).toBe("2027-04-15");
    expect(format(addMonths(parse("2027-03-15")!, -12))).toBe("2026-03-15");
    expect(format(addMonths(parse("9999-12-01")!, 1))).toBe("9999-12-31");
    expect(format(addDays(parse("0001-01-01")!, -1))).toBe("0001-01-01");
  });
  it("builds Monday-first six-week grids", () => {
    const cells = grid(parse("2026-11-26")!);
    expect(cells).toHaveLength(42);
    expect(format(cells[0]!)).toBe("2026-10-26");
    expect(cells.filter((c) => c && c.m === 11)).toHaveLength(30);
    expect(grid(parse("0001-01-01")!)[0]).not.toBeNull(); // 0001-01-01 is a Monday
  });
});

describe("keyboard", () => {
  const start = parse("2027-03-15")!;
  it("matches the native month calendar keys", () => {
    expect(key(start, "ArrowRight")).toEqual({ focus: parse("2027-03-16") });
    expect(key(start, "ArrowLeft")).toEqual({ focus: parse("2027-03-14") });
    expect(key(start, "ArrowDown")).toEqual({ focus: parse("2027-03-22") });
    expect(key(start, "ArrowUp")).toEqual({ focus: parse("2027-03-08") });
    expect(key(start, "PageDown")).toEqual({ focus: parse("2027-04-15") });
    expect(key(start, "PageUp")).toEqual({ focus: parse("2027-02-15") });
    expect(key(start, "PageDown", true)).toEqual({ focus: parse("2028-03-15") });
    expect(key(start, "Home")).toEqual({ focus: parse("2027-03-15") }); // Monday
    expect(key(start, "End")).toEqual({ focus: parse("2027-03-21") });
    expect(key(start, "Enter")).toEqual({ pick: start });
    expect(key(start, " ")).toEqual({ pick: start });
    expect(key(start, "Escape")).toEqual({ cancel: true });
    expect(key(start, "a")).toBeNull();
  });
});

import { filterText } from "./types";
describe("field filters", () => {
  it("keeps only what the C++ fields accepted", () => {
    expect(filterText("start", "0a8::55x", "endTime")).toBe("08:55");
    expect(filterText("salary", "1,234.5.6", "endTime")).toBe("1234.56");
    expect(filterText("end", "8.5h", "duration")).toBe("8.5");
    expect(filterText("end", "18:30", "endTime")).toBe("18:30");
    expect(filterText("resignation", "2026-11-30-1", "endTime")).toBe("2026-11-30");
    expect(filterText("lunchStart", "12:00\n13:30", "endTime")).toBe("12:00");
  });
});
