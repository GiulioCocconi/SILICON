/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

import { defineConfig } from "vitepress";

import { requirePalette, syntaxTheme } from "../../palette";

requirePalette();

export default defineConfig({
  title: "SILICON User Documentation",
  description: "User documentation for the SILICON digital logic simulator.",
  base: "/SILICON/docs",
  appearance: true,
  markdown: { theme: { light: syntaxTheme("light"), dark: syntaxTheme("dark") } },
  outDir: "../dist/docs",
  cacheDir: "../node_modules/.vitepress-cache/userdocs",
  markdown: {
    math: true,
  },
  themeConfig: {
    logo: "/silicon-icon.svg",
    siteTitle: "SILICON Docs",
    editLink: {
      pattern: "https://github.com/GiulioCocconi/SILICON/edit/main/webpage/userdocs/:path",
      text: "Modify on GitHub",
    },
    nav: [
      { text: "home", link: "/../", target: "_self" },
      { text: "getting started", link: "/" },
      { text: "silicon", link: "/guide/silicon" },
      { text: "lessons", link: "/lessons" },
      { text: "showcase", link: "/guide/lorem-ipsum" },
    ],
    sidebar: {
      "/lessons/": [
        {
          text: "Lessons",
          items: [{ text: "Overview", link: "/lessons/" }],
        },
      ],
      "/guide/contributing/": [
        {
          text: "Contributing to SILICON",
          items: [
            { text: "Overview", link: "/guide/contributing/" },
            { text: "Build and Test Locally", link: "/guide/contributing/build" },
            {
              text: "Contribution Workflow and Git",
              link: "/guide/contributing/workflow",
            },
            { text: "Continuous Integration", link: "/guide/contributing/ci" },
          ],
        },
      ],
      "/": [
        {
          text: "User Documentation",
          items: [
            { text: "Getting Started", link: "/" },
            { text: "SILICON Basics", link: "/guide/silicon" },
            { text: "Silicon Internals", link: "/guide/internals" },
            { text: "Troubleshooting", link: "/guide/troubleshooting" },
            { text: "Formatting Showcase", link: "/guide/lorem-ipsum" },
          ],
        },
      ],
    },
    socialLinks: [{ icon: "github", link: "https://github.com/GiulioCocconi/SILICON" }],
    search: {
      provider: "local",
    },
  },
});
