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
{ pkgs }:
let
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
}
