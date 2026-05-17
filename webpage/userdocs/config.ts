import { defineConfig } from "vitepress";

export default defineConfig({
  title: "SILICON User Documentation",
  description: "User documentation for the SILICON digital logic simulator.",
  base: "/SILICON/docs/",
  cleanUrls: true,
  appearance: false,
  outDir: "../dist/docs",
  cacheDir: "../node_modules/.vitepress-cache/userdocs",
  themeConfig: {
    logo: "/silicon-icon.svg",
    siteTitle: "SILICON Docs",
    nav: [
      { text: "getting started", link: "/" },
      { text: "logiflow", link: "/guide/logiflow" },
      { text: "showcase", link: "/guide/lorem-ipsum" },
      { text: "main site", link: "/../" },
    ],
    sidebar: [
      {
        text: "User Documentation",
        items: [
          { text: "Getting Started", link: "/" },
          { text: "LogiFlow Basics", link: "/guide/logiflow" },
          { text: "Troubleshooting", link: "/guide/troubleshooting" },
          { text: "Formatting Showcase", link: "/guide/lorem-ipsum" },
        ],
      },
    ],
    socialLinks: [
      { icon: "github", link: "https://github.com/GiulioCocconi/SILICON" },
    ],
    footer: {
      message: "Open source digital logic simulation.",
      copyright: "GPL licensed.",
    },
    search: {
      provider: "local",
    },
  },
});
