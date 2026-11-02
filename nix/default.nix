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
{ nixpkgs, git-hooks, sisl, system }:
let
  pkgs = nixpkgs.legacyPackages.${system};
  sislPackage = sisl.packages.${system}.default;
  coderabbit = import ./coderabbit.nix { inherit pkgs; };
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
  preCommitCheck = import ./git-hooks.nix { inherit pkgs git-hooks system; };
  mkSilicon = import ./silicon.nix {
    inherit pkgs sislPackage;
    inherit libraries nativeInputs;
  };
in
{
  devShells = import ./dev-shells.nix {
    inherit pkgs sislPackage preCommitCheck coderabbit libraries nativeInputs;
  };

  checks.pre-commit-check = preCommitCheck;

  packages = {
    default = mkSilicon { };
    pr = mkSilicon { sanitizers = true; };
    nightly = mkSilicon { buildType = "RelWithDebInfo"; };
    release = mkSilicon { buildType = "Release"; };
    inherit coderabbit;
  };
}
