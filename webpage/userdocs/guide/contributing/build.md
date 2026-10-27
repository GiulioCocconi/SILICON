# Build and Test Locally

SILICON is a C++23 Qt 6 application built with CMake. The Nix development shell
is the first-class and supported contribution environment: it provides the
project's toolchain, dependencies, and Git hooks together. Tests and benchmarks
are enabled by default. Windows builds use MinGW and the vcpkg integration
bootstrapped by CMake, but they do not provide the complete contributor tooling.

::: warning Debug builds and nightly builds
The commands below reproduce the README's local **Debug development build**.
They do not reproduce the downloadable nightly artifact: pushes to `main` publish
Windows and WASM **Release** snapshots named `silicon-unstable-*`. Pull-request
Windows and WASM snapshots are Debug builds. See [Continuous Integration](./ci.md).
:::

## Linux with Nix

Install [Nix](https://nixos.org/download.html) or
[Lix](https://lix.systems/install/) and enable the `nix-command` and `flakes`
experimental features. From a clone of SILICON, enter the development shell and
build the Debug configuration exactly as described in the README:

```bash
nix develop
mkdir build
cmake -G "Ninja" -Bbuild -DCMAKE_BUILD_TYPE=Debug
ninja -C build
```

The shell supplies CMake, Ninja, Qt, Yosys, GoogleTest, clang tools, GDB, DDD,
Valgrind, Doxygen, and the remaining native dependencies. Debug Linux builds
enable AddressSanitizer and UndefinedBehaviorSanitizer by default

::: warning Keep the development shell active
`nix develop` starts a shell in the current terminal. The repository's
`pre-commit` executable and configured hooks are available **only while this
shell is active**. Run the build, tests, commits, and pre-submission checks from
that shell. After opening a new terminal or leaving the shell, run `nix develop`
again before continuing contribution work.
:::

To configure an already-created build directory again, omit `mkdir build`.
CMake writes executables to the repository's `bin/` directory and libraries to
`lib/`, even though build metadata lives under `build/`.

## Windows with MinGW

Install a recent MinGW toolchain (the README records MinGW 13 as tested) and
[CMake](https://cmake.org/download/). MSVC is explicitly unsupported. CMake
bootstraps vcpkg and builds dependencies from source, so the first configure may
take a long time.

The README's Debug commands are:

```powershell
mkdir build
cmake -G "MinGW Makefiles" -Bbuild -DCMAKE_BUILD_TYPE=Debug
make -C build
```

Some MinGW installations expose the build tool as `mingw32-make` rather than
`make`. The generator-independent equivalent is:

```powershell
cmake --build build
```

The CI environment uses MinGW with Ninja instead of the `MinGW Makefiles`
generator. Both routes configure the same CMake project. Native macOS builds are
currently unsupported.

## Run tests

Run all discovered GoogleTest cases after a successful native build:

```bash
ctest --test-dir build --output-on-failure
```

To rerun a subset, list the discovered names and filter them:

```bash
ctest --test-dir build -N
ctest --test-dir build -R <test-name-pattern> --output-on-failure
```

Tests are not built under Emscripten. Yosys-dependent targets and tests are only
available when Yosys is present. When adding behavior, place focused coverage in
the relevant file under `tests/`; add a new target in `tests/CMakeLists.txt` only
when no existing test executable is a sensible home.

## Useful CMake options

The defaults reflect the contributor configuration:

| Option                      | Default | Purpose                                                        |
| --------------------------- | ------- | -------------------------------------------------------------- |
| `SILICON_BUILD_TESTS`       | `ON`    | Build native GoogleTest targets.                               |
| `SILICON_BUILD_BENCHMARKS`  | `ON`    | Build standalone performance benchmarks.                       |
| `SILICON_ENABLE_SANITIZERS` | `ON`    | Enable address/undefined sanitizers on supported Linux builds. |
| `SILICON_USE_VCPKG`         | `ON`    | Bootstrap/use vcpkg outside Nix.                               |

Disable a feature only when it is irrelevant to the task, and mention the
different configuration in the pull request's test notes.

## Formatting and static analysis

The `.clang-format` file defines the C++ format. The Git hook supplied by the
active development shell checks staged `.cpp` and `.hpp` lines when you commit.
To inspect formatting before committing, stage the intended changes and run:

```bash
python ci/git-clang-format --diff --staged \
  --extensions cpp,hpp --binary clang-format --
```

`.clang-tidy` records the project's static-analysis rules; use the generated
compilation database when running clang tooling. The corresponding CI behavior
is described under [Formatting assistant](./ci.md#formatting-assistant).

## Run the pre-submission policy check

From the active `nix develop` shell, run the same local policy checker used by
the pull-request workflow:

```bash
PR_COMPLIANCE_BASE_REF=upstream/main \
  pre-commit run pr-compliance --hook-stage manual
```

See [PR compliance](./ci.md#pr-compliance)
for the enforced checks and the one identity check that only Actions can run.

## Build the documentation

Use the dedicated Nix development shell from the repository root:

```bash
nix develop .#webpage
cd webpage
bun install --frozen-lockfile
python3 ../ci/theme/generate.py
bun run build:docs
```

For live editing, run `bun run docs:dev`. The complete website and VitePress
documentation build is `bun run build:all`. Doxygen API documentation is built
separately from the repository root with `doxygen Doxyfile`.

To build and preview the homepage, user docs, and API docs together, run
`./webpage/buildwebpage.sh` from the repository root. It generates the palette
CSS, builds all three surfaces, and serves them at
`http://127.0.0.1:8080/SILICON/`. Stop the server with Ctrl+C; its temporary preview
directory is removed automatically.

Before submitting a docs change, confirm that VitePress reports no dead local
links and inspect the relevant pages in the development server.

## Change the shared color palette

Edit `resources/app/palette.json` from the repository root. Its `light` and
`dark` objects use the same `SILICON_*` token names and six-digit hex colors.
Qt embeds this file; rebuild the app after edits. Existing theme settings still
select the light or dark palette.

CI uses the repository’s pinned Doxygen toolchain (`nix develop .#docs`).
It generates `webpage/theme/palette.css` using the Python standard library before
building the homepage, user docs, and Doxygen. The CSS is a build artifact listed
in `.gitignore`; commit the JSON and generator changes only.

For local builds, generate it explicitly from the repository root:

```bash
python3 ci/theme/generate.py
python3 ci/theme/generate.py --check
```

Vite and VitePress refuse to start or build without this file. They consume the
CSS directly, so regenerate it after editing the JSON; running development
servers pick up the changed CSS normally. Doxygen also requires this stylesheet,
and `WARN_AS_ERROR = YES` makes missing files and all documentation warnings
fatal. Build API docs with `doxygen Doxyfile` after generating the CSS.

Website styles map framework variables to the shared tokens; transparent
variations derive from tokens using `color-mix`. Static artwork is maintained
separately.

VitePress uses its built-in appearance switch. Doxygen uses its native
`HTML_COLORSTYLE = TOGGLE` control, with no custom HTML header or injected
controller. The homepage provides System, Light, and Dark choices using browser
APIs; it shares VitePress's preference key on the same origin. Doxygen manages
its own preference. All browser preferences are independent of the Qt setting.
