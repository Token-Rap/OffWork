import { defineConfig } from "vite";
import { svelte } from "@sveltejs/vite-plugin-svelte";
export default defineConfig({
  plugins: [svelte()],
  clearScreen: false,
  build: { rollupOptions: { input: { main: "index.html", calendar: "calendar.html" } } },
  test: { include: ["src/**/*.test.ts"] },
});
