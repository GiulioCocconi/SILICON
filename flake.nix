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
            vulkan-headers
            libxkbcommon.dev
            clang-tools
            gdb
            ddd
            valgrind
            surelog
            yosys
	    doxygen
	    graphviz
        ];

        libraries = with pkgs; [
            gtest.dev
            qt6.qtbase
            qt6.qtsvg
        ];

        nativeInputs = with pkgs; [
            cmake
            ninja
        ];
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
                 packages = devPackages ++ libraries ++ nativeInputs;
                 hardeningDisable = [ "all" ];
            };
        };

        packages.default = pkgs.stdenv.mkDerivation {
          pname = "SILICON";
          version = "0.1.0-pre-alpha";

          src = ./.;

          nativeBuildInputs = nativeInputs ++ [pkgs.qt6.wrapQtAppsHook];

          buildInputs = libraries ++ [pkgs.range-v3];

          cmakeFlags = [
            "-DSILICON_USE_VCPKG=OFF"
          ];

          doCheck = true;

          meta = with pkgs.lib; {
            description = "Open Source Suite for simulating Circuits, FSMs and Microcontrollers";
            homepage = "https://github.com/GiulioCocconi/SILICON";
            license = licenses.gpl3;
            platforms = platforms.linux;
          };
        };
      }
    );
}
