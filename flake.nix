#  Copyright (c) 2026. Giulio Cocconi
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <http://www.gnu.org/licenses/>.
{
  description = "SILICON - Open Source Suite for simulating Circuits, FSMs and Microcontrollers";

  inputs = {
    nixpkgs.url = "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.zst";
    flake-utils.url = "github:numtide/flake-utils";
    sisl = {
      url = "github:GiulioCocconi/SISL";
      inputs.nixpkgs.follows = "nixpkgs";
    };
    git-hooks = {
      url = "github:cachix/git-hooks.nix";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

outputs = { self, nixpkgs, flake-utils, git-hooks, sisl }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        sislPackage = sisl.packages.${system}.default;

        preCommitCheck = git-hooks.lib.${system}.run {
          src = ./.;
          hooks = {
            format-code = {
              enable = true;
              name = "Check staged C++ formatting";
              entry = "${pkgs.python3}/bin/python ${./ci/git-clang-format} --diff --staged --extensions cpp,hpp --binary ${pkgs.clang-tools}/bin/clang-format --";
              files = "\\.(cpp|hpp)$";
              stages = [ "pre-commit" ];
            };

            pr-compliance = {
              enable = true;
              name = "Check PR compliance";
              entry = "${pkgs.python3}/bin/python ${./ci/check-pr-compliance.py}";
              pass_filenames = false;
              stages = [ "manual" ];
              always_run = true;
            };
          };
        };

        coderabbit = let
          version = "0.9.0";
          hashes = {
            x86_64-linux = "sha256-VuTJnenREQbD5r8xA0hk2490bLl8nq+UcK3PpkYdpm8=";
            aarch64-linux = "sha256-rY9wB0KYG+uHWRYNu9eGGDrz12yX0VlePWQJnk8TySQ=";
          };
        in pkgs.stdenv.mkDerivation {
          pname = "coderabbit";
          inherit version;

          src = pkgs.fetchurl {
            url = "https://cli.coderabbit.ai/releases/${version}/coderabbit-linux-${if pkgs.stdenv.hostPlatform.isx86_64 then "x64" else "arm64"}.zip";
            hash = hashes.${pkgs.stdenv.hostPlatform.system} or (throw "coderabbit: unsupported system ${pkgs.stdenv.hostPlatform.system}");
          };

          nativeBuildInputs = [ pkgs.unzip ];

          # Stripping/shrinking rewrites the ELF and drops Bun's appended
          # payload, breaking the CLI.
          dontStrip = true;
          dontShrink = true;

          unpackPhase = ''
            runHook preUnpack
            unzip "$src"
            runHook postUnpack
          '';

          # The CLI is a Bun single-file executable: its payload is appended
          # after the ELF, so patching the interpreter with patchelf would strip
          # it and the binary would fall back to plain Bun. Invoke it through
          # the NixOS dynamic loader instead.
          installPhase = ''
            runHook preInstall
            mkdir -p "$out/libexec" "$out/bin"
            install -Dm755 coderabbit "$out/libexec/coderabbit"
            cat > "$out/bin/coderabbit" <<EOF
            #!${pkgs.runtimeShell}
            exec ${pkgs.stdenv.cc.bintools.dynamicLinker} "$out/libexec/coderabbit" "\$@"
            EOF
            chmod +x "$out/bin/coderabbit"
            ln -s coderabbit "$out/bin/cr"
            runHook postInstall
          '';

          meta = with pkgs.lib; {
            description = "CodeRabbit CLI for AI code review";
            homepage = "https://cli.coderabbit.ai";
            platforms = [ "x86_64-linux" "aarch64-linux" ];
            mainProgram = "coderabbit";
          };
        };


        lightCIPackages = with pkgs; [
          (python3.withPackages (pp: with pp; [ pygithub ]))
          git-cliff
        ];

        devPackages = with pkgs; [
          vulkan-headers
          libxkbcommon.dev
          clang-tools
          doxygen
	  coderabbit
          gdb
          ddd
          opencode
	  codex
          graphviz
        ] ++ lightCIPackages;

        libraries = with pkgs; [
          gtest.dev
          qt6.qtbase
          qt6.qtsvg
          boost
          libzip
          pegtl
          nlohmann_json
          tomlplusplus
	        ogdf
        ];

        nativeInputs = with pkgs; [
          cmake
          ninja
          python3
          yosys
        ];

        bun = pkgs.buildPackages.bun;

        mkSilicon = { buildType ? "Debug", sanitizers ? false }: pkgs.stdenv.mkDerivation {
            pname = "SILICON";
            version = "0.1.0-beta.2";

            src = ./.;

            nativeBuildInputs = nativeInputs ++ [pkgs.qt6.wrapQtAppsHook];

            buildInputs = libraries ++ [pkgs.range-v3 sislPackage];

            cmakeFlags = [
              "-DSILICON_USE_VCPKG=OFF"
              "-DUSING_NIX=ON"
              "-DCMAKE_BUILD_TYPE=${buildType}"
              "-DSILICON_ENABLE_SANITIZERS=${if sanitizers then "ON" else "OFF"}"
            ];

            doCheck = true;

            meta = with pkgs.lib; {
              description = "Open Source Suite for simulating Circuits, FSMs and Microcontrollers";
              homepage = "https://github.com/GiulioCocconi/SILICON";
              license = licenses.gpl3;
              platforms = platforms.linux;
            };
          };
      in
        {
          devShells = {
            lightCI = pkgs.mkShell {
              name = "SILICON-light-CI";
              packages = lightCIPackages;
            };

            default = pkgs.mkShell {
              name = "SILICON-dev";
              packages = devPackages ++ libraries ++ nativeInputs ++ [ sislPackage ] ++ preCommitCheck.enabledPackages;
              inherit (preCommitCheck) shellHook;
              hardeningDisable = [ "all" ];
              NIX_LANG_CPP = "TRUE";
            };

            clang = (pkgs.mkShell.override { stdenv = pkgs.llvmPackages_20.libcxxStdenv; }) {
              name = "SILICON-dev-clang";
              packages = devPackages ++ libraries ++ nativeInputs ++ [pkgs.range-v3 sislPackage] ++ preCommitCheck.enabledPackages;
              inherit (preCommitCheck) shellHook;
              hardeningDisable = [ "all" ];
            };

            docs = pkgs.mkShell {
              name = "SILICON-docs-CI";
              packages = [ pkgs.python3 pkgs.doxygen pkgs.graphviz ];
            };

            webpage = pkgs.mkShell {
              name = "SILICON-webpage-dev";
              packages = [ bun pkgs.doxygen pkgs.graphviz pkgs.python3 pkgs.opencode ] ++ preCommitCheck.enabledPackages;
              inherit (preCommitCheck) shellHook;
            };
          };

          checks.pre-commit-check = preCommitCheck;

          packages.default = mkSilicon { };
          packages.pr = mkSilicon { sanitizers = true; };
          packages.nightly = mkSilicon { buildType = "RelWithDebInfo"; };
          packages.release = mkSilicon { buildType = "Release"; };
          packages.coderabbit = coderabbit;
        }
    );
}
