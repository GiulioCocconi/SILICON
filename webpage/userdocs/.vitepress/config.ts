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

export default defineConfig({
  title: "SILICON User Documentation",
  description: "User documentation for the SILICON digital logic simulator.",
  base: "/SILICON/docs",
  appearance: false,
  outDir: "../dist/docs",
  cacheDir: "../node_modules/.vitepress-cache/userdocs",
  themeConfig: {
    logo: "/silicon-icon.svg",
    siteTitle: "SILICON Docs",
    nav: [
      { text: "home", link: "/../", target: "_self"  },
      { text: "getting started", link: "/" },
      { text: "logiflow", link: "/guide/logiflow" },
      { text: "showcase", link: "/guide/lorem-ipsum" },
    ],
    sidebar: [
      {
        text: "User Documentation",
        items: [
          { text: "Getting Started", link: "/" },
          { text: "LogiFlow Basics", link: "/guide/logiflow" },
          { text: "Silicon Internals", link: "/guide/internals" },
          { text: "Troubleshooting", link: "/guide/troubleshooting" },
          { text: "Formatting Showcase", link: "/guide/lorem-ipsum" },
        ],
      },
    ],
    socialLinks: [
      { icon: "github", link: "https://github.com/GiulioCocconi/SILICON" },
    ],
    search: {
      provider: "local",
    },
  },
});
