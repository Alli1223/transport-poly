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

Press **Tab** or click **Players** in multiplayer chat to see connected players,
companies and spectators. The host controls pause and speed (including **Space**).
Clients see changes immediately while paused; commands submitted during pause
wait until the host resumes. The dedicated server uses its console controls.

Press **F5** in a game to write `saves/quicksave.tpsave`; load it through the
normal load menu. In multiplayer the host saves the game. Monthly autosaves
continue independently.

Click the clock in the top bar to toggle **Always daylight**. Lighting stays at
midday while the simulation clock and citizens' schedules continue normally.
Click again to restore the day / night cycle. This preference is local to the
client, including in multiplayer.

Build a straight road across straight railway track (or railway across a road)
on flat land to create a level crossing. Road traffic waits at red crossing lights
while a train has reserved the track; trains wait for road vehicles to clear.
Use the road or rail removal tool to remove only that layer. Tram tracks, signals
and stops cannot be added on a crossing.

The railway toolbar has separate **Path signal** and **Block signal** tools.
Path signals reserve the chosen route through a junction. Block signals wait
until all connected branches ahead are clear, up to the next signals. Click
again with the same tool to cycle two-way, one-way, reversed and removed;
selecting the other signal tool changes the type while preserving its direction.

In the water toolbar, drag **Canal** over clear, level land at sea level to make
it navigable. Use the landscaping tools to lower the terrain first. **Reclaim**
turns water into clear land; it protects ships, active sailing steps and dock /
shipyard berths. Canals have company ownership and monthly maintenance. Raised
waterways and locks are not yet implemented.

Road and rail toolbars include **Bridge**, **Tunnel** and **Remove span**.
Drag a straight span between clear, flat ends at the same height (3–64 tiles).
Bridge interiors need lower terrain; tunnel interiors need higher terrain.
Water under bridges remains navigable, with at least three height levels of
clearance. Remove the entire structure with **Remove span** after all vehicles
and reservations have cleared it. Spans reserve their footprint; surface junctions
and buildings cannot share those tiles.

Finances show current-year, current-month and previous-month income and expenses,
with monthly history. Open **Company details** in that window to change the name
or livery. Four quarterly checks with negative cash trigger bankruptcy: vehicles
are liquidated, stops and depots close, and remaining routes become public.
Returning to a non-negative balance resets the debt-quarter counter.

Open **Settings** from either menu for shadows, view distance, VSync, daylight,
camera sensitivity and autosave intervals (0 disables autosaving). Save preferences
to retain them in `settings.cfg`. Starting year and interest apply to new worlds.
Clicking the clock also remembers the daylight preference.

Press **C** or open **Citizens** in the dock to inspect residents. Filter by town,
search by name or ID, and follow a citizen between home, work, shopping and transit.
The inspector shows upcoming activities and a live activity log. Walking citizens
and waiting passengers are drawn near the camera. The vehicle directory also
filters by company, vehicle type and operating status.

The map window's display selector shows station catchments, current passenger
flows, or town zones. Town details include monthly station boardings and the
share of residents covered by recently served stops. Stations have capacity
based on their facilities and size; crowding and long waits reduce their rating.

New worlds include sea-level rivers and lakes. Older saves retain their original
terrain generator so exploring beyond previously visited chunks remains seamless.
Companies pay monthly property maintenance for their road and track pieces,
signals, station tiles and depots; town-owned roads are maintained by the town.
