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
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};

        devPackages = with pkgs; [
          (python3.withPackages (pp: with pp; [ pygithub rich ]))
          vulkan-headers
          libxkbcommon.dev
          clang-tools
          gdb
          ddd
          valgrind
          surelog
          opencode
          doxygen
	  codex
          graphviz
        ];

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

	# TODO: Remove when NixPkgs PR 559059 gets to unstable
        # The pinned Yosys package installs yosys-config with an
        # /usr/bin/env shebang, which is unavailable in pure Nix builds.
        yosysConfig = pkgs.writeShellScriptBin "yosys-config" ''
          exec ${pkgs.bash}/bin/bash ${pkgs.yosys}/bin/yosys-config "$@"
        '';

        nativeInputs = with pkgs; [
          cmake
          ninja
          python3
          yosysConfig
          # Required by the Yosys JSON import validation test.
          yosys
        ];

        bun = pkgs.buildPackages.bun;

        mkSilicon = { release ? false }: pkgs.stdenv.mkDerivation {
            pname = "SILICON";
            version = "0.1.0-pre-alpha";

            src = ./.;

            nativeBuildInputs = nativeInputs ++ [pkgs.qt6.wrapQtAppsHook];

            buildInputs = libraries ++ [pkgs.range-v3];

            cmakeFlags = [
              "-DSILICON_USE_VCPKG=OFF"
              "-DUSING_NIX=ON"
              "-DSILICON_YOSYS_CONFIG_EXECUTABLE=${yosysConfig}/bin/yosys-config"
            ] ++ pkgs.lib.optionals release [
              "-DCMAKE_BUILD_TYPE=Release"
              "-DSILICON_ENABLE_SANITIZERS=OFF"
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
            default = pkgs.mkShell {
              name = "SILICON-dev";
              packages = devPackages ++ libraries ++ nativeInputs;
              hardeningDisable = [ "all" ];
              NIX_LANG_CPP = "TRUE";
            };

            clang = (pkgs.mkShell.override { stdenv = pkgs.llvmPackages_20.libcxxStdenv; }) {
              name = "SILICON-dev-clang";
              packages = devPackages ++ libraries ++ nativeInputs ++ [pkgs.range-v3];
              hardeningDisable = [ "all" ];
            };

            webpage = pkgs.mkShell {
              name = "SILICON-webpage-dev";
              packages = [ bun pkgs.doxygen pkgs.graphviz pkgs.python3 pkgs.opencode ];
            };
          };

          packages.default = mkSilicon { };
          packages.release = mkSilicon { release = true; };
        }
    );
}
