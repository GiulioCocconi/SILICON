import { defineConfig } from "vite";
import { tanstackRouter } from "@tanstack/router-plugin/vite";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";
import tsConfigPaths from "vite-tsconfig-paths";

export default defineConfig({
  base: "/SILICON/",
  plugins: [tsConfigPaths(), tailwindcss(), tanstackRouter(), react()],
  build: {
    outDir: "dist",
  },
});
