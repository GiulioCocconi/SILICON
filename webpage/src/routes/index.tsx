import { createFileRoute } from "@tanstack/react-router";
import siliconLogo from "@/assets/silicon-icon.svg";

export const Route = createFileRoute("/")({
  head: () => ({
    meta: [
      { title: "SILICON — Open Source Digital Logic Simulator" },
      {
        name: "description",
        content:
          "SILICON is an open source suite for simulating digital circuits, finite state machines and microcontrollers. Built with C++ and Qt6.",
      },
      { property: "og:title", content: "SILICON — Open Source Digital Logic Simulator" },
      {
        property: "og:description",
        content:
          "Simulate circuits, FSMs and microcontrollers with SILICON — an open source digital logic suite.",
      },
      { property: "og:type", content: "website" },
      {
        property: "og:image",
        content:
          "https://raw.githubusercontent.com/GiulioCocconi/SILICON/main/resources/banner.png",
      },
      { name: "twitter:card", content: "summary_large_image" },
      {
        name: "twitter:image",
        content:
          "https://raw.githubusercontent.com/GiulioCocconi/SILICON/main/resources/banner.png",
      },
    ],
  }),
  component: Index,
});

const GH = "https://github.com/GiulioCocconi/SILICON";

const features = [
  {
    color: "var(--silicon-orange)",
    title: "LogiFlow",
    desc: "Design and simulate combinational and sequential logic circuits with an intuitive graphical editor.",
    items: ["Inputs & Outputs", "Wires & Buses", "Multiplexers (WIP)", "Flip-Flops (WIP)"],
  },
  {
    color: "var(--silicon-blue)",
    title: "FSM Suite",
    desc: "Model finite state machines with clean diagrams. State transitions, guards and outputs visualized.",
    items: ["State diagrams", "Mealy & Moore", "Timed simulation", "Coming soon"],
  },
  {
    color: "var(--silicon-green)",
    title: "Microcontrollers",
    desc: "Emulate microcontroller cores and peripherals. Bridge software and hardware design seamlessly.",
    items: ["Core emulation", "Peripherals", "Memory map", "Coming soon"],
  },
  {
    color: "var(--silicon-magenta)",
    title: "Verilog & Tooling",
    desc: "Built on modern C++ and Qt6. Verilog import via Slang, file format minimization via Quine–McCluskey.",
    items: ["Qt6 GUI", "Cross-platform", "Verilog (WIP)", "Open Source"],
  },
];

const links = [
  {
    href: "internaldocs",
    label: "Internal Docs",
    sub: "Doxygen API reference",
    color: "var(--silicon-orange)",
  },
  {
    href: `${GH}/releases`,
    label: "Releases",
    sub: "Download latest builds",
    color: "var(--silicon-blue)",
    external: true,
  },
  {
    href: GH,
    label: "GitHub",
    sub: "Source code & issues",
    color: "var(--silicon-green)",
    external: true,
  },
  {
    href: "blog",
    label: "Blog",
    sub: "News & devlogs",
    color: "var(--silicon-magenta)",
  },
];

function Index() {
  return (
    <div className="min-h-screen">
      {/* NAV */}
      <header className="border-b-2 border-foreground bg-background/80 backdrop-blur sticky top-0 z-50">
        <div className="mx-auto max-w-6xl px-6 py-4 flex items-center justify-between">
          <a href="/" className="flex items-center gap-3">
            <img
              src={siliconLogo}
              alt="SILICON logo"
              width={44}
              height={44}
              className="rounded-xl"
            />
            <span className="font-display text-2xl tracking-wide">SILICON</span>
          </a>
          <nav className="hidden md:flex items-center gap-2 mono text-sm">
            <a href="#features" className="px-3 py-1.5 rounded-md hover:bg-muted">
              features
            </a>
            <a href="internaldocs" className="px-3 py-1.5 rounded-md hover:bg-muted">
              docs
            </a>
            <a
              href={`${GH}/releases`}
              className="px-3 py-1.5 rounded-md hover:bg-muted"
              target="_blank"
              rel="noreferrer"
            >
              releases
            </a>
            <a href="blog" className="px-3 py-1.5 rounded-md hover:bg-muted">
              blog
            </a>
          </nav>
          <a
            href={GH}
            target="_blank"
            rel="noreferrer"
            className="silicon-btn text-sm"
            style={{ backgroundColor: "var(--silicon-green)" }}
          >
            ★ Star on GitHub
          </a>
        </div>
      </header>

      {/* HERO */}
      <section className="mx-auto max-w-6xl px-6 pt-16 pb-20">
        <div className="grid md:grid-cols-[1fr_auto] gap-10 items-center">
          <div>
            <div className="mono text-xs uppercase tracking-widest mb-4 inline-block px-2 py-1 border-2 border-foreground rounded-md bg-[var(--silicon-green)]">
              v0.1 · pre-alpha · open source
            </div>
            <h1 className="font-display text-5xl md:text-7xl leading-[1.05] mb-6">
              Simulate the <br />
              <span className="inline-block px-3 py-1 rounded-xl" style={{ backgroundColor: "var(--silicon-orange)" }}>
                silicon
              </span>{" "}
              behind it all.
            </h1>
            <p className="text-lg max-w-xl text-muted-foreground mb-8">
              <strong className="text-foreground">SILICON</strong> is an open source suite for simulating
              digital <em>circuits</em>, <em>finite state machines</em>, and{" "}
              <em>microcontrollers</em> — built with modern C++ and Qt6.
            </p>
            <div className="flex flex-wrap gap-3">
              <a
                href={`${GH}/releases`}
                target="_blank"
                rel="noreferrer"
                className="silicon-btn"
                style={{ backgroundColor: "var(--silicon-blue)", color: "white" }}
              >
                ⬇ Download
              </a>
              <a href="internaldocs" className="silicon-btn">
                📖 Read the docs
              </a>
              <a
                href={GH}
                target="_blank"
                rel="noreferrer"
                className="silicon-btn"
              >
                {"</>"} View source
              </a>
            </div>
          </div>
          <div className="hidden md:block">
            <div
              className="silicon-card p-6"
              style={{ backgroundColor: "var(--silicon-green)" }}
            >
              <img
                src={siliconLogo}
                alt=""
                width={240}
                height={240}
                className="block"
              />
            </div>
          </div>
        </div>

        {/* Quick links bar */}
        <div className="mt-16 grid grid-cols-2 md:grid-cols-4 gap-4">
          {links.map((l) => (
            <a
              key={l.label}
              href={l.href}
              target={l.external ? "_blank" : undefined}
              rel={l.external ? "noreferrer" : undefined}
              className="silicon-card p-4 block"
            >
              <div
                className="w-10 h-10 rounded-lg border-2 border-foreground mb-3"
                style={{ backgroundColor: l.color }}
              />
              <div className="font-display text-lg">{l.label}</div>
              <div className="mono text-xs text-muted-foreground mt-1">{l.sub}</div>
            </a>
          ))}
        </div>
      </section>

      {/* FEATURES */}
      <section id="features" className="border-t-2 border-foreground bg-card">
        <div className="mx-auto max-w-6xl px-6 py-20">
          <div className="flex items-end justify-between flex-wrap gap-4 mb-12">
            <h2 className="font-display text-4xl md:text-5xl">Features</h2>
            <p className="mono text-sm text-muted-foreground max-w-md">
              {"// "}A modular suite — pick what you need, ignore the rest.
            </p>
          </div>

          <div className="grid md:grid-cols-2 gap-6">
            {features.map((f) => (
              <article key={f.title} className="silicon-card p-6">
                <div className="flex items-start gap-4">
                  <div
                    className="w-14 h-14 rounded-xl border-2 border-foreground shrink-0"
                    style={{ backgroundColor: f.color }}
                  />
                  <div className="flex-1">
                    <h3 className="font-display text-2xl mb-2">{f.title}</h3>
                    <p className="text-muted-foreground mb-4">{f.desc}</p>
                    <ul className="grid grid-cols-2 gap-x-4 gap-y-1 mono text-sm">
                      {f.items.map((it) => (
                        <li key={it} className="flex items-center gap-2">
                          <span className="text-foreground">▸</span> {it}
                        </li>
                      ))}
                    </ul>
                  </div>
                </div>
              </article>
            ))}
          </div>
        </div>
      </section>

      {/* TRUTH TABLE / TECH */}
      <section className="border-t-2 border-foreground">
        <div className="mx-auto max-w-6xl px-6 py-20 grid md:grid-cols-2 gap-10 items-center">
          <div>
            <h2 className="font-display text-4xl md:text-5xl mb-4">Built for hackers.</h2>
            <p className="text-muted-foreground mb-6">
              Modern C++, Qt6 GUI, CMake + Nix dependency management. Cross-platform
              builds for Linux and Windows. Continuous integration via GitHub Actions.
            </p>
            <div className="flex flex-wrap gap-2 mono text-xs">
              {["C++", "Qt6", "CMake", "Nix", "MinGW", "GitHub Actions", "Doxygen"].map(
                (t) => (
                  <span
                    key={t}
                    className="px-3 py-1 rounded-md border-2 border-foreground bg-secondary"
                  >
                    {t}
                  </span>
                ),
              )}
            </div>
          </div>

          <div className="silicon-card p-0 overflow-hidden">
            <div
              className="px-4 py-2 mono text-sm border-b-2 border-foreground flex items-center gap-2"
              style={{ backgroundColor: "var(--silicon-orange)" }}
            >
              <span className="w-3 h-3 rounded-full bg-foreground" />
              <span>truth_table.v</span>
            </div>
            <pre className="p-5 mono text-sm leading-relaxed overflow-x-auto">
{`// AND gate · 2 inputs
A  B  | Y
─────────
0  0  | 0
0  1  | 0
1  0  | 0
1  1  | 1

CLK ↑  Ø → 1`}
            </pre>
          </div>
        </div>
      </section>

      {/* CTA */}
      <section className="border-t-2 border-foreground" style={{ backgroundColor: "var(--silicon-magenta)" }}>
        <div className="mx-auto max-w-6xl px-6 py-20 text-center text-primary-foreground">
          <h2 className="font-display text-4xl md:text-6xl mb-4 text-background">
            Ready to wire things up?
          </h2>
          <p className="mono text-sm mb-8 text-background/80">
            $ git clone https://github.com/GiulioCocconi/SILICON
          </p>
          <div className="flex flex-wrap justify-center gap-3">
            <a
              href={`${GH}/releases`}
              target="_blank"
              rel="noreferrer"
              className="silicon-btn"
              style={{ backgroundColor: "var(--silicon-green)" }}
            >
              Get latest release
            </a>
            <a href="internaldocs" className="silicon-btn">
              Browse the API
            </a>
          </div>
        </div>
      </section>

      {/* FOOTER */}
      <footer className="border-t-2 border-foreground bg-background">
        <div className="mx-auto max-w-6xl px-6 py-10 flex flex-wrap items-center justify-between gap-4">
          <div className="flex items-center gap-3">
            <img src={siliconLogo} alt="" width={32} height={32} className="rounded-md" />
            <span className="font-display text-lg">SILICON</span>
            <span className="mono text-xs text-muted-foreground">
              · open source · GPL
            </span>
          </div>
          <div className="mono text-xs text-muted-foreground flex flex-wrap gap-4">
            <a href="internaldocs" className="hover:text-foreground">internaldocs</a>
            <a href={`${GH}/releases`} target="_blank" rel="noreferrer" className="hover:text-foreground">releases</a>
            <a href={GH} target="_blank" rel="noreferrer" className="hover:text-foreground">github</a>
            <a href="blog" className="hover:text-foreground">blog</a>
          </div>
        </div>
      </footer>
    </div>
  );
}
