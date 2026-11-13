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
{ pkgs, sislPackage, libraries, nativeInputs }:
{ buildType ? "Debug", sanitizers ? false }: pkgs.stdenv.mkDerivation {
  pname = "SILICON";
  version = "0.1.0-beta.3";

  # CI selects pr, nightly, or release and reads its buildType with
  # `nix eval --raw .#<package>.buildType` for Windows and WASM.
  # This keeps their CMake build type aligned with the Linux package.
  passthru = { inherit buildType; };

  src = ../.;

  nativeBuildInputs = nativeInputs ++ [ pkgs.qt6.wrapQtAppsHook ];

  buildInputs = libraries ++ [ pkgs.range-v3 sislPackage ];

  cmakeFlags = [
    "-DSILICON_USE_VCPKG=OFF"
    "-DUSING_NIX=ON"
    "-DCMAKE_BUILD_TYPE=${buildType}"
    "-DSILICON_ENABLE_SANITIZERS=${if sanitizers then "ON" else "OFF"}"
  ];

  doCheck = false;

  hardeningDisable = pkgs.lib.optionals (buildType == "Debug") [ "fortify" ];

  meta = with pkgs.lib; {
    description = "Open Source Suite for simulating Circuits, FSMs and Microcontrollers";
    homepage = "https://github.com/GiulioCocconi/SILICON";
    license = licenses.gpl3;
    platforms = platforms.linux;
  };
}
