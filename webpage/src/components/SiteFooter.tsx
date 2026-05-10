import siliconLogo from "@/assets/silicon-icon.svg";

const GH = "https://github.com/GiulioCocconi/SILICON";

export function SiteFooter() {
  return (
    <footer className="border-t-2 border-foreground bg-background">
      <div className="page-shell footer-section flex flex-wrap items-center justify-between gap-4">
        <div className="flex items-center gap-3">
          <img src={siliconLogo} alt="" width={32} height={32} className="rounded-md" />
          <span className="font-display text-lg">SILICON</span>
          <span className="mono text-xs text-muted-foreground">· open source · GPL</span>
        </div>
        <div className="mono text-xs text-muted-foreground flex flex-wrap gap-4">
          <a href="#/" className="hover:text-foreground">
            home
          </a>
          <a href="#/download" className="hover:text-foreground">
            download
          </a>
          <a href="internaldocs" className="hover:text-foreground">
            internaldocs
          </a>
          <a href="blog" className="hover:text-foreground">
            blog
          </a>
          <a href={GH} target="_blank" rel="noreferrer" className="hover:text-foreground">
            github
          </a>
        </div>
      </div>
    </footer>
  );
}
