import { createFileRoute } from "@tanstack/react-router";
import { useState } from "react";
import { SiteHeader } from "@/components/SiteHeader";
import siliconLogo from "@/assets/silicon-icon.svg";

export const Route = createFileRoute("/download")({
  head: () => ({
    meta: [
      { title: "Download SILICON" },
      {
        name: "description",
        content:
          "Download SILICON Windows releases and nightly builds from Cloudsmith, or build SILICON from source on Linux.",
      },
      { property: "og:title", content: "Download SILICON" },
      {
        property: "og:description",
        content:
          "Windows release and nightly build downloads powered by Cloudsmith, plus Linux build instructions.",
      },
      { property: "og:type", content: "website" },
    ],
  }),
  component: Download,
});

const GH = "https://github.com/GiulioCocconi/SILICON";

const cloudsmith = {
  owner: import.meta.env.VITE_CLOUDSMITH_OWNER ?? "",
  releaseRepo: import.meta.env.VITE_CLOUDSMITH_RELEASE_REPO ?? "",
};

const packages = [
  {
    key: "release",
    title: "Windows release",
    badge: "stable",
    packageName: "silicon-release-windows",
    description: "Recommended for normal use. Published when a GitHub release is published.",
    color: "var(--silicon-green)",
  },
  {
    key: "nightly",
    title: "Windows nightly",
    badge: "main snapshot",
    packageName: "silicon-unstable-windows",
    description:
      "Latest main-branch CI build. Use this if you want recent fixes before the next tagged release.",
    color: "var(--silicon-orange)",
  },
];

type CloudsmithPackage = {
  cdn_url?: string;
  filename?: string;
  name?: string;
  uploaded_at?: string;
  uploaded_at_iso?: string;
  version?: string;
};

function isCloudsmithConfigured() {
  return Boolean(cloudsmith.owner && cloudsmith.releaseRepo);
}

function cloudsmithSearchUrl(packageName: string) {
  if (!isCloudsmithConfigured()) {
    return `${GH}/releases`;
  }

  const query = encodeURIComponent(`name:^${packageName}$`);
  return `https://cloudsmith.io/~${cloudsmith.owner}/repos/${cloudsmith.releaseRepo}/packages/?q=${query}`;
}

function cloudsmithApiUrl(packageName: string) {
  const query = encodeURIComponent(`format:raw AND name:^${packageName}$`);
  return `https://api.cloudsmith.io/packages/${cloudsmith.owner}/${cloudsmith.releaseRepo}/?query=${query}`;
}

function getPackageDate(pkg: CloudsmithPackage) {
  return Date.parse(pkg.uploaded_at_iso ?? pkg.uploaded_at ?? "") || 0;
}

async function fetchLatestPackage(packageName: string) {
  const response = await fetch(cloudsmithApiUrl(packageName), {
    headers: { Accept: "application/json" },
  });

  if (!response.ok) {
    throw new Error(`Cloudsmith returned ${response.status}`);
  }

  const payload: unknown = await response.json();
  const items = Array.isArray(payload)
    ? payload
    : typeof payload === "object" && payload !== null && "data" in payload
      ? (payload as { data?: unknown }).data
      : [];

  const packages = Array.isArray(items) ? (items as CloudsmithPackage[]) : [];
  const matchingPackages = packages
    .filter((pkg) => pkg.cdn_url && (!pkg.name || pkg.name === packageName))
    .sort((left, right) => getPackageDate(right) - getPackageDate(left));

  return matchingPackages[0];
}

const buildSteps = `git clone https://github.com/GiulioCocconi/SILICON
cd SILICON
nix build
./result/bin/SILICON`;

const devSteps = `git clone https://github.com/GiulioCocconi/SILICON
cd SILICON
nix develop
cmake -G Ninja -Bbuild -DCMAKE_BUILD_TYPE=Debug
ninja -C build
./build/SILICON`;

function Download() {
  const [loadingPackage, setLoadingPackage] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  async function downloadLatest(packageName: string) {
    if (!isCloudsmithConfigured()) {
      window.location.href = `${GH}/releases`;
      return;
    }

    setError(null);
    setLoadingPackage(packageName);

    try {
      const latestPackage = await fetchLatestPackage(packageName);

      if (!latestPackage?.cdn_url) {
        throw new Error("No downloadable Cloudsmith package was found.");
      }

      window.location.href = latestPackage.cdn_url;
    } catch (cause) {
      setError(
        cause instanceof Error
          ? `${cause.message}. Opening the Cloudsmith package list instead.`
          : "Could not resolve the latest Cloudsmith package. Opening the package list instead.",
      );
      window.location.href = cloudsmithSearchUrl(packageName);
    } finally {
      setLoadingPackage(null);
    }
  }

  return (
    <div className="min-h-screen">
      <SiteHeader />

      <main>
        <section className="mx-auto max-w-6xl px-6 pt-16 pb-20">
          <div className="grid lg:grid-cols-[1fr_360px] gap-10 items-start">
            <div>
              <div className="mono text-xs uppercase tracking-widest mb-4 inline-block px-2 py-1 border-2 border-foreground rounded-md bg-[var(--silicon-blue)]">
                Cloudsmith artifacts
              </div>
              <h1 className="font-display text-5xl md:text-7xl leading-[1.05] mb-6">
                Download <br />
                <span
                  className="inline-block px-3 py-1 rounded-xl"
                  style={{ backgroundColor: "var(--silicon-orange)" }}
                >
                  SILICON
                </span>
              </h1>
              <p className="text-lg max-w-2xl text-muted-foreground">
                Windows binaries are published from CI to Cloudsmith. Tagged releases are the stable
                channel; nightly builds track the latest successful build from{" "}
                <strong className="text-foreground">main</strong>.
              </p>
              {!isCloudsmithConfigured() && (
                <div className="mt-6 silicon-card p-4 bg-[var(--silicon-orange)]">
                  <p className="mono text-sm">
                    Cloudsmith repository variables are missing from this web build. Set{" "}
                    <code>VITE_CLOUDSMITH_OWNER</code> and <code>VITE_CLOUDSMITH_RELEASE_REPO</code>{" "}
                    to enable direct downloads.
                  </p>
                </div>
              )}
            </div>

            <aside className="silicon-card p-6">
              <h2 className="font-display text-2xl mb-3">Release notes</h2>
              <p className="text-muted-foreground mb-5">
                GitHub remains the source of tagged release notes, changelogs, and source archives.
              </p>
              <a
                href={`${GH}/releases`}
                target="_blank"
                rel="noreferrer"
                className="silicon-btn w-full justify-center"
                style={{ backgroundColor: "var(--silicon-magenta)" }}
              >
                Open GitHub releases
              </a>
            </aside>
          </div>

          <div className="mt-14 grid md:grid-cols-2 gap-6">
            {packages.map((pkg) => (
              <article key={pkg.key} className="silicon-card p-6">
                <div
                  className="w-14 h-14 rounded-xl border-2 border-foreground mb-5"
                  style={{ backgroundColor: pkg.color }}
                />
                <div className="flex items-start justify-between gap-4 mb-3">
                  <h2 className="font-display text-3xl">{pkg.title}</h2>
                  <span className="mono text-xs px-2 py-1 rounded-md border-2 border-foreground bg-secondary">
                    {pkg.badge}
                  </span>
                </div>
                <p className="text-muted-foreground mb-6">{pkg.description}</p>
                <div className="flex flex-wrap gap-3">
                  <a
                    href={cloudsmithSearchUrl(pkg.packageName)}
                    className="silicon-btn"
                    style={{ backgroundColor: pkg.color }}
                    onClick={(event) => {
                      event.preventDefault();
                      void downloadLatest(pkg.packageName);
                    }}
                  >
                    {loadingPackage === pkg.packageName ? "Finding latest..." : "Download ZIP"}
                  </a>
                  <a
                    href={cloudsmithSearchUrl(pkg.packageName)}
                    target="_blank"
                    rel="noreferrer"
                    className="silicon-btn"
                  >
                    View in Cloudsmith
                  </a>
                </div>
                <p className="mono text-xs text-muted-foreground mt-4">
                  package: {pkg.packageName}
                </p>
              </article>
            ))}
          </div>

          <div className="mt-8 max-w-3xl">
            <p className="text-sm text-muted-foreground leading-relaxed">
              Package repository hosting is graciously provided by{" "}
              <a
                href="https://cloudsmith.com"
                target="_blank"
                rel="noreferrer"
                className="text-foreground underline underline-offset-4 hover:opacity-80"
              >
                Cloudsmith
              </a>
              . Cloudsmith is the only fully hosted, cloud-native, universal package management
              solution, that enables your organization to create, store and share packages in any
              format, to any place, with total confidence.
            </p>
          </div>

          {error && <p className="mt-6 mono text-sm text-muted-foreground">{error}</p>}
        </section>

        <section className="border-t-2 border-foreground bg-card">
          <div className="mx-auto max-w-6xl px-6 py-20">
            <div className="flex items-end justify-between flex-wrap gap-4 mb-12">
              <h2 className="font-display text-4xl md:text-5xl">Build on Linux</h2>
              <p className="mono text-sm text-muted-foreground max-w-md">
                {"// "}Nix is the supported path because it pins Qt6, CMake, Ninja, and the native
                libraries.
              </p>
            </div>

            <div className="grid lg:grid-cols-2 gap-6">
              <article className="silicon-card p-6">
                <h3 className="font-display text-2xl mb-3">Release-style build</h3>
                <p className="text-muted-foreground mb-4">
                  Use this if you only want a reproducible local build from the current repository
                  state.
                </p>
                <pre className="mono text-sm overflow-auto rounded-xl border-2 border-foreground bg-secondary p-4">
                  <code>{buildSteps}</code>
                </pre>
              </article>

              <article className="silicon-card p-6">
                <h3 className="font-display text-2xl mb-3">Development build</h3>
                <p className="text-muted-foreground mb-4">
                  Use this when editing the codebase and rebuilding incrementally.
                </p>
                <pre className="mono text-sm overflow-auto rounded-xl border-2 border-foreground bg-secondary p-4">
                  <code>{devSteps}</code>
                </pre>
              </article>
            </div>

            <div className="mt-8 silicon-card p-6">
              <h3 className="font-display text-2xl mb-3">Before running Nix</h3>
              <p className="text-muted-foreground mb-4">
                On non-NixOS distributions, install Nix or Lix first and enable flakes. The
                repository README has the full setup notes.
              </p>
              <div className="flex flex-wrap gap-3">
                <a
                  href={`${GH}#on-linux`}
                  target="_blank"
                  rel="noreferrer"
                  className="silicon-btn"
                  style={{ backgroundColor: "var(--silicon-blue)" }}
                >
                  Linux build docs
                </a>
                <a
                  href={`${GH}/blob/main/flake.nix`}
                  target="_blank"
                  rel="noreferrer"
                  className="silicon-btn"
                >
                  Inspect flake.nix
                </a>
              </div>
            </div>
          </div>
        </section>
      </main>

      <footer className="border-t-2 border-foreground bg-background">
        <div className="mx-auto max-w-6xl px-6 py-10 flex flex-wrap items-center justify-between gap-4">
          <div className="flex items-center gap-3">
            <img src={siliconLogo} alt="" width={32} height={32} className="rounded-md" />
            <span className="font-display text-lg">SILICON</span>
            <span className="mono text-xs text-muted-foreground">. downloads . Cloudsmith</span>
          </div>
          <div className="mono text-xs text-muted-foreground flex flex-wrap gap-4">
            <a href="#/" className="hover:text-foreground">
              home
            </a>
            <a href="internaldocs" className="hover:text-foreground">
              internaldocs
            </a>
            <a
              href={`${GH}/releases`}
              target="_blank"
              rel="noreferrer"
              className="hover:text-foreground"
            >
              github releases
            </a>
            <a href={GH} target="_blank" rel="noreferrer" className="hover:text-foreground">
              github
            </a>
          </div>
        </div>
      </footer>
    </div>
  );
}
