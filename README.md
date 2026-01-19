![Silicon](./resources/banner.png)
An Open Source Suite for simulating Circuits, Finite State Machines and Microcontrollers (WIP)

## TODOs

Since it's a pre-alpha product, there are quite a lot of things to be done:

A roadmap (+ various diagrams/ideas) is available [here](https://www.canva.com/design/DAGqb7QaA-w/_ld_l41b__KKIG6wlUhFLg/view)![Canva](https://img.shields.io/badge/Canva-%2300C4CC.svg?style=for-the-badge&logo=Canva&logoColor=white), written partly in Italian.
_Common_

- [ ] GUI with QT6:
    * [ ] Implement logic for moving graphicalWires,
    * [ ] Use [QSettings](https://doc.qt.io/qt-6/qsettings.html) to save user preferences
- [ ] Multi-OS support
- [ ] Documentation
    * [ ] [Doxygen](https://www.doxygen.nl/index.html)?
    * [ ] [Read the Docs](https://readthedocs.org/)?
- [X] CI/CD
    * [ ] [GitHub Actions](https://github.com/features/actions)
    * [ ] Multi-OS support (_kinda done: windows builds are now supported_)
        * [ ] Deployment (setup packages for Win & Mac). See [here](https://www.qt.io/blog/cmake-deployment-api).

_Logic circuits (Silicon LogiFlow)_

- [ ] Multiplexers & demultiplexers
- [ ] Timed simulation
- [ ] Flip flops & synchronous components
- [x] Wires-to-bus & bus-to-wires
- [x] INPUTS & OUTPUTS!!!!
- [ ] Bus display
- [ ] 7-segment display
- [ ] Verilog support
    * [ ] [Slang](https://github.com/MikePopoloski/slang) for parsing
    * [ ] [Inja](https://github.com/pantor/inja) for reverse parsing (needs template file)
- [ ] File format
    * [ ] Implement [Quine-McCluskey method](https://en.wikipedia.org/wiki/Quine%E2%80%93McCluskey_algorithm) to minimize the combinatorial logic net
    * [ ] Using verilog & Slang?

_FSMs_

- [ ] TBD

_Microcontrollers_

- [ ] TBD

## Using clangd

This project is currently being developed using EMACS. The following packages will be useful:

- [lsp-mode](https://emacs-lsp.github.io/) for clangd integration.
- [dir-config.el](https://github.com/jamescherti/dir-config.el) to load this project specific config.

Clangd uses the file `compile_commands.json`, that should be placed in the project root. CMAKE generates it in the build subdirectory so you need to symlink it:

```
cd <project-root>
ln -s ./build/compile_commands.json compile_commands.json
```

## Compiling

### On Linux

SILICON uses [Nix](https://nixos.org) and [CMake](https://cmake.org) in order to manage dependencies. It's recomended to use [Ninja Build](https://ninja-build.org) as a generator.

If you're not using NixOS as a distro, you can install Nix using the [official instructions](https://nixos.org/download.html),
however I personally recommend using [Lix](https://lix.systems/install), a modern Nix fork, which can be installed running the following command in a Linux system:

```shell
curl -sSf -L https://install.lix.systems/lix | sh -s -- install
```

Then enable the experimental features required by SILICON running:
```shell
echo "experimental-features = nix-command flakes" >> ~/.config/nix/nix.conf
```

Finally, run the commands below to compile the develop edition of SILICON on Linux:

```shell
git clone https://github.com/GiulioCocconi/SILICON
cd SILICON
nix build # Downloads the dependencies and builds the software
```

### On Windows

Only recent versions of the MinGW compiler are supported (tested using version `13.0`), you can install them via this [graphical installer](https://github.com/Vuniverse0/mingwInstaller/releases/tag/1.2.1).

The only thing you need to install is [CMake](https://cmake.org), since the `CMakeLists` take care of installing the dependency manager [vcpkg](https://vcpkg.io).
Since all dependencies are built from source, the first time you run CMake you might need to wait a few minutes (or hours, depending on your compute power).

To start the compilation process, once you have installed the compiler and CMake, run the following commands:
```shell
 git clone https://github.com/GiulioCocconi/silicon
 cd silicon
 mkdir build
 cmake -G "MinGW Makefiles" -Bbuild
 make -C build
```
