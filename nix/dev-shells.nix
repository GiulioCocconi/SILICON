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
{ pkgs, sislPackage, preCommitCheck, coderabbit, libraries, nativeInputs }:
let
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

  bun = pkgs.buildPackages.bun;
in
{
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
    packages = devPackages ++ libraries ++ nativeInputs ++ [ pkgs.range-v3 sislPackage ] ++ preCommitCheck.enabledPackages;
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
}
