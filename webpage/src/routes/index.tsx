import { createFileRoute } from "@tanstack/react-router";
import { SiteHeader } from "@/components/SiteHeader";
import { SiteFooter } from "@/components/SiteFooter";
import siliconLogo from "@/assets/silicon-icon.svg";
import demoGif from "@/assets/demo.gif";

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
    href: "#/download",
    label: "Download",
    sub: "Releases & nightlies",
    color: "var(--silicon-orange)",
  },
  {
    href: "/wasm",
    label: "Try online",
    sub: "SILICON in the browser",
    color: "var(--silicon-blue)",
  },
  {
    href: "docs/",
    label: "User Docs",
    sub: "Guides & workflows",
    color: "var(--silicon-green)",
  },
  {
    href: GH,
    label: "GitHub",
    sub: "Source code & issues",
    color: "var(--silicon-magenta)",
    external: true,
  },
];

function Index() {
  return (
    <div className="min-h-screen overflow-x-hidden">
      <SiteHeader />

      {/* HERO */}
      <section className="page-shell hero-section">
        <div className="grid md:grid-cols-[1fr_auto] gap-10 items-center">
          <div>
            <div className="hero-kicker mono bg-[var(--silicon-green)]">
              v0.1 · pre-alpha · open source
            </div>
            <h1 className="hero-title font-display">
              Simulate the <br className="hidden sm:block" />
              <span
                className="inline-block max-w-full px-3 py-1 rounded-xl"
                style={{ backgroundColor: "var(--silicon-orange)" }}
              >
                silicon
              </span>{" "}
              behind it all.
            </h1>
            <p className="hero-copy max-w-xl text-muted-foreground">
              <strong className="text-foreground">SILICON</strong> is an open source suite for
              simulating digital <em>circuits</em>, <em>finite state machines</em>, and{" "}
              <em>microcontrollers</em> — built with modern C++ and Qt6.
            </p>
          </div>
          <div className="hidden md:block">
            <div className="silicon-card p-6" style={{ backgroundColor: "var(--silicon-green)" }}>
              <img src={siliconLogo} alt="" width={240} height={240} className="block" />
            </div>
          </div>
        </div>

        {/* Quick links bar */}
        <div className="mt-12 sm:mt-16 grid grid-cols-1 sm:grid-cols-2 md:grid-cols-4 gap-4">
          {links.map((l) => (
            <a
              key={l.label}
              href={l.href}
              target={l.external ? "_blank" : undefined}
              rel={l.external ? "noreferrer" : undefined}
              className="silicon-card p-4 block min-w-0"
            >
              <div
                className="w-10 h-10 rounded-lg border-2 border-foreground mb-3"
                style={{ backgroundColor: l.color }}
              />
              <div className="font-display text-lg break-words">{l.label}</div>
              <div className="mono text-xs text-muted-foreground mt-1 break-words">{l.sub}</div>
            </a>
          ))}
        </div>
      </section>

      {/* FEATURES */}
      <section id="features" className="border-t-2 border-foreground bg-card">
        <div className="page-shell content-section">
          <div className="flex items-end justify-between flex-wrap gap-4 mb-12">
            <h2 className="section-title font-display">Features</h2>
            <p className="mono text-sm text-muted-foreground max-w-md">
              {"// "}A modular suite — pick what you need, ignore the rest.
            </p>
          </div>

          <div className="grid md:grid-cols-2 gap-6">
            {features.map((f) => (
              <article key={f.title} className="silicon-card card-pad">
                <div className="flex items-start gap-4">
                  <div
                    className="w-12 h-12 sm:w-14 sm:h-14 rounded-xl border-2 border-foreground shrink-0"
                    style={{ backgroundColor: f.color }}
                  />
                  <div className="flex-1 min-w-0">
                    <h3 className="card-title font-display mb-2">{f.title}</h3>
                    <p className="text-muted-foreground mb-4">{f.desc}</p>
                    <ul className="grid grid-cols-1 sm:grid-cols-2 gap-x-4 gap-y-1 mono text-sm">
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
        <div className="page-shell content-section grid md:grid-cols-2 gap-10 items-center">
          <div>
            <h2 className="section-title font-display mb-4">Built for hackers.</h2>
            <p className="text-muted-foreground mb-6">
              Modern C++, Qt6 GUI, CMake + Nix dependency management. Cross-platform builds for
              Linux and Windows. Continuous integration via GitHub Actions.
            </p>
            <div className="flex flex-wrap gap-2 mono text-xs">
              {["C++", "Qt6", "CMake", "Nix", "MinGW", "GitHub Actions", "Doxygen"].map((t) => (
                <span
                  key={t}
                  className="px-3 py-1 rounded-md border-2 border-foreground bg-secondary"
                >
                  {t}
                </span>
              ))}
            </div>
          </div>

          <div className="silicon-card p-0 overflow-hidden min-w-0">
            <div
              className="px-4 py-2 mono text-sm border-b-2 border-foreground flex items-center gap-2"
              style={{ backgroundColor: "var(--silicon-orange)" }}
            >
              <span className="w-3 h-3 rounded-full bg-foreground" />
              <span>latch.sil</span>
            </div>
            <img src={demoGif} alt="sr latch demo" className="block w-full h-auto" />
          </div>
        </div>
      </section>

      {/* CTA */}
      <section
        className="border-t-2 border-foreground"
        style={{ backgroundColor: "var(--silicon-magenta)" }}
      >
        <div className="page-shell content-section text-center text-primary-foreground">
          <h2 className="font-display text-3xl sm:text-4xl md:text-6xl mb-4 text-background">
            Ready to wire things up?
          </h2>
          <p className="mono text-sm mb-8 text-background/80">
            $ git clone https://github.com/GiulioCocconi/SILICON
          </p>
          <div className="flex flex-wrap justify-center gap-3">
            <a
              href="#/download"
              className="silicon-btn"
              style={{ backgroundColor: "var(--silicon-green)" }}
            >
              Download SILICON
            </a>
            <a href="docs/" className="silicon-btn">
              Read the Docs
            </a>
          </div>
        </div>
      </section>
      <SiteFooter />
    </div>
  );
}
