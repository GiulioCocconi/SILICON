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
