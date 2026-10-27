import { defineConfig } from "vite";
import { tanstackRouter } from "@tanstack/router-plugin/vite";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";
import tsConfigPaths from "vite-tsconfig-paths";

import { requirePalette, themeBootstrap } from "./palette";

requirePalette();

export default defineConfig({
  base: "/SILICON/",
  plugins: [{
    name: "silicon-theme-bootstrap",
    transformIndexHtml() {
      return [{ tag: "script", children: themeBootstrap, injectTo: "head" }];
    },
  }, tsConfigPaths(), tailwindcss(), tanstackRouter(), react()],
  build: {
    outDir: "dist",
  },
});
