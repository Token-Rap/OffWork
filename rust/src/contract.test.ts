/// <reference types="vite/client" />
// Contract between the widget and the Rust commands (review finding 4):
// every command the widget applies through `call()` must return a View,
// otherwise `show()` would receive null and blank the clock and earnings.
import { describe, expect, it } from "vitest";
import { isView } from "./types";

const svelte = import.meta.glob("./App.svelte", { query: "?raw", import: "default", eager: true }) as Record<string, string>;
const rust = import.meta.glob("../src-tauri/src/*.rs", { query: "?raw", import: "default", eager: true }) as Record<string, string>;

function returnType(name: string): string | null {
  for (const source of Object.values(rust)) {
    const match = new RegExp(`#\\[tauri::command\\]\\s*pub (?:async )?fn ${name}\\s*(?:<[^>]*>)?\\(([\\s\\S]*?)\\)\\s*(?:->\\s*([^{]+))?\\{`).exec(source);
    if (match) return (match[2] ?? "()").trim();
  }
  return null;
}

describe("command contract", () => {
  const app = Object.values(svelte)[0];
  const called = [...new Set([...app.matchAll(/\bcall\(\s*"(\w+)"/g)].map((m) => m[1]))];

  it("finds the commands the widget applies", () => {
    expect(called).toEqual(expect.arrayContaining(["start_update", "calendar_finish", "commit_field", "open_calendar"]));
  });

  it.each(called)("%s returns a View", (name) => {
    const type = returnType(name);
    expect(type, `${name} is not a #[tauri::command]`).not.toBeNull();
    expect(type).toMatch(/^(Result<\s*(crate::commands::)?View\s*,\s*String\s*>|(crate::commands::)?View)$/);
  });

  it("rejects anything that is not a full View", () => {
    for (const bad of [null, undefined, {}, { settings: {} }, "ok", 1]) expect(isView(bad)).toBe(false);
    expect(isView({ settings: {}, fields: {}, update: {}, autostart: {}, version: "0.5.0" })).toBe(true);
  });
});
