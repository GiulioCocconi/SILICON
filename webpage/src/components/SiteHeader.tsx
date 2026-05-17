import type { MouseEvent } from "react";
import siliconLogo from "@/assets/silicon-icon.svg";

const GH = "https://github.com/GiulioCocconi/SILICON";

export function SiteHeader() {
  function scrollToFeatures(event: MouseEvent<HTMLAnchorElement>) {
    event.preventDefault();
    window.location.hash = "/";
    window.setTimeout(() => {
      document.getElementById("features")?.scrollIntoView({ behavior: "smooth" });
    }, 0);
  }

  return (
    <header className="border-b-2 border-foreground bg-background/80 backdrop-blur sticky top-0 z-50">
      <div className="mx-auto max-w-6xl px-4 sm:px-6 py-3 sm:py-4 flex items-center justify-between gap-3">
        <a href="#/" className="flex min-w-0 items-center gap-2 sm:gap-3">
          <img
            src={siliconLogo}
            alt="SILICON logo"
            width={44}
            height={44}
            className="w-10 h-10 sm:w-11 sm:h-11 rounded-xl shrink-0"
          />
          <span className="font-display text-xl sm:text-2xl tracking-wide">SILICON</span>
        </a>
        <nav className="hidden md:flex items-center gap-2 mono text-sm">
          <a href="#/" className="px-3 py-1.5 rounded-md hover:bg-muted">
            home
          </a>
          <a href="#/" className="px-3 py-1.5 rounded-md hover:bg-muted" onClick={scrollToFeatures}>
            features
          </a>
          <a href="internaldocs" className="px-3 py-1.5 rounded-md hover:bg-muted">
            api
          </a>
          <a href="docs/" className="px-3 py-1.5 rounded-md hover:bg-muted">
            docs
          </a>
          <a href="#/download" className="px-3 py-1.5 rounded-md hover:bg-muted">
            download
          </a>
          <a href="blog" className="px-3 py-1.5 rounded-md hover:bg-muted">
            blog
          </a>
        </nav>
        <a
          href={GH}
          target="_blank"
          rel="noreferrer"
          className="silicon-btn shrink-0 px-3 py-2 sm:px-5 sm:py-3 text-xs sm:text-sm"
          style={{ backgroundColor: "var(--silicon-green)" }}
        >
          <span className="sm:hidden">GitHub</span>
          <span className="hidden sm:inline">★ Star on GitHub</span>
        </a>
      </div>
    </header>
  );
}
