import { spawn } from "node:child_process";
import { cp, mkdir, rm } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const projectRoot = path.resolve(scriptDir, "..");
const docsRoot = path.join(projectRoot, "userdocs");
const generatedConfigDir = path.join(docsRoot, ".vitepress");
const command = process.argv[2] ?? "build";
const extraArgs = process.argv.slice(3);

async function prepareConfig() {
  await rm(generatedConfigDir, { recursive: true, force: true });
  await mkdir(path.join(generatedConfigDir, "theme"), { recursive: true });
  await cp(path.join(docsRoot, "config.ts"), path.join(generatedConfigDir, "config.ts"));
  await cp(path.join(docsRoot, "theme"), path.join(generatedConfigDir, "theme"), {
    recursive: true,
  });
}

async function cleanupConfig() {
  await rm(generatedConfigDir, { recursive: true, force: true });
}

await prepareConfig();

const vitepressBin = path.join(
  projectRoot,
  "node_modules",
  ".bin",
  process.platform === "win32" ? "vitepress.cmd" : "vitepress",
);

const child = spawn(vitepressBin, [command, "userdocs", ...extraArgs], {
  cwd: projectRoot,
  stdio: "inherit",
});

let cleanupStarted = false;

async function finish(code, signal) {
  if (cleanupStarted) {
    return;
  }

  cleanupStarted = true;
  await cleanupConfig();

  if (signal) {
    process.exit(signal === "SIGINT" ? 130 : 143);
  }

  process.exit(code ?? 0);
}

process.on("SIGINT", () => child.kill("SIGINT"));
process.on("SIGTERM", () => child.kill("SIGTERM"));

child.on("exit", (code, signal) => {
  finish(code, signal).catch((error) => {
    console.error(error);
    process.exit(1);
  });
});

child.on("error", (error) => {
  cleanupConfig()
    .catch((cleanupError) => {
      console.error(cleanupError);
    })
    .finally(() => {
      console.error(error);
      process.exit(1);
    });
});
