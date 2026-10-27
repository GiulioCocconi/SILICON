import { accessSync, constants, readFileSync } from "node:fs";

export const themeBootstrap = readFileSync(new URL("./theme/appearance.js", import.meta.url), "utf8");

/** Generated CSS is a required build input, never synthesized by Vite. */
export function requirePalette() {
  try {
    accessSync(new URL("./theme/palette.css", import.meta.url), constants.R_OK);
  } catch {
    throw new Error("Missing webpage/theme/palette.css. Run python3 ci/theme/generate.py from the repository root.");
  }
}

// Shiki accepts CSS variables in custom themes, allowing live palette updates.
export function syntaxTheme(mode: "light" | "dark") {
  const color = (name: string) => `var(--silicon-${name})`;
  return {
    name: `silicon-${mode}`,
    type: mode,
    colors: { "editor.background": color("surface-raised"), "editor.foreground": color("ink") },
    tokenColors: [
      { scope: ["comment", "punctuation.definition.comment"], settings: { foreground: color("grey") } },
      { scope: ["keyword", "storage"], settings: { foreground: color("blue") } },
      { scope: ["entity.name", "support.type"], settings: { foreground: color("violet") } },
      { scope: ["string"], settings: { foreground: color("green") } },
      { scope: ["constant", "support.constant"], settings: { foreground: color("orange") } },
    ],
  };
}
