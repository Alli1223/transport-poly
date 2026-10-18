# transport-poly

A low poly transport simulation game in the spirit of OpenTTD: build bus, tram,
rail and ferry networks across an infinite, procedurally generated world whose
towns are populated by individually simulated citizens. Every resident has a
home, a work place and a favourite shop: five days a week they commute to work,
two days a week they go shopping in the nearest city - and your network is how
they get there.

* C++23, OpenGL 3.3 core, Windows and Linux
* Infinite chunked terrain generated deterministically from a seed
* Server-authoritative lockstep multiplayer (OpenTTD style): clients issue
  commands, the server schedules them, every peer runs the same simulation
* Dedicated headless server

See [TODO.md](TODO.md) for the development roadmap and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how the code is organised.

## Building

Requirements: CMake 3.25+, Ninja (or Visual Studio), Git, and a C++23 compiler:
MSVC 19.40+ (Visual Studio 2022 17.10+ / 2026), GCC 14+ or Clang 18+.
Third-party libraries (GLFW, GLM, Dear ImGui) are downloaded automatically at
configure time.

### Windows

From a *x64 Native Tools Command Prompt for VS*:

```bat
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
build\windows-msvc\bin\RelWithDebInfo\transport-poly.exe
```

Or work in Visual Studio 2026: run `scripts\open-vs.bat` once. It generates the
projects with the `windows-vs2026` preset and opens `transport-poly.slnx` (the game is
the startup project, so F5 runs it). After that just open `transport-poly.slnx`;
CMake regenerates the projects by itself when a `CMakeLists.txt` changes.

### Linux

Install the toolchain and the X11/OpenGL development headers, e.g. on Ubuntu 24.04:

```bash
sudo apt install build-essential g++-14 cmake ninja-build git libgl1-mesa-dev xorg-dev
```

Then:

```bash
cmake --preset linux-gcc -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14
cmake --build --preset linux-gcc-release
ctest --preset linux-gcc-release
./build/linux-gcc/bin/RelWithDebInfo/transport-poly
```

To build only the dedicated server and tests (no X11/OpenGL needed) use the
`linux-headless` preset.

### Packaging

`cpack --preset windows-msvc` (or `linux-gcc` / `linux-headless`) after a release
build writes a self-contained zip / tar.gz with the game, the dedicated server and
the font to `build/<preset>/packages/`.

## Running a multiplayer game

```bash
transport-poly-server --port 3979 --seed 12345
```

Clients connect through *Multiplayer → Join* in the main menu.
