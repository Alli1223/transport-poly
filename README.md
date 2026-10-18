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
Building windows and town street lamps light up at dusk and fade back to their
daytime appearance at dawn.

The land rolls in smooth hills and mountains. The landscaping toolbar's brush
raises, lowers, levels and smooths it with a soft edge; pick the brush size in
the tool's panel.

A new game's **Terrain** section shows a picture of the land around the start
and chooses what kind it is: **Continents**, **Mainland** (land almost
everywhere), **Islands**, **Archipelago** (many small islands) or a **Single
island** surrounded by ocean. It also sets how many mountains (none to alpine),
lakes and rivers there are, and the climate: **Temperate**, **Arid** (deserts
with mesas and dunes, and dry steppe), **Arctic** (tundra, snowy taiga and
glaciers), **Tropical** (jungle and savanna) or **Varied** (every biome in
broad regions across the world). Each biome has its own ground and trees: cacti
and shrubs in the desert, acacias on the savanna, palms in the jungle. A new
company always starts on land. The dedicated server takes the same choices:
`--land continents|mainland|islands|archipelago|island`,
`--climate temperate|arid|arctic|tropical|varied`, `--mountains 0-4`,
`--lakes 0-4` and `--rivers 0-3`. Worlds saved before these options keep the
land they were made with.

Railways are free-form, in the style of Transport Fever. With the **Track** tool,
click to start and click again to lay a curved piece; the next piece carries on
from its end. Start on a track or station end to continue it (or to branch off
through a switch), and finish on one to join up. The planner keeps to a minimum
curve radius and a gentle gradient, and shapes the ground under the track into a
level bed with embankments and cuttings. Where the rails would run high above the
ground, or over water, roads and other railways, the track goes onto a bridge;
deep under a hill it goes through a tunnel. The preview colours bridges blue and
tunnels purple, and its tooltip gives the cost, including earthworks. **Remove**
takes away the piece of track under the pointer.

**Station** places a straight platform at any angle: click where it starts (on a
track end to line it up) and again to set its length. While placing, a ghost shows
the platform and the reshaped ground, with the cost of levelling it. **Depot** goes
on an open track end. **Signal** adds path signals where track joins track; click
again for one-way (either direction) and once more to remove them. Trains reserve
their route from signal to signal, slow for curves and climbs, and reverse at
terminus platforms.

Click a placed rail station to open **Station layout and road access**. Use
**Add road entrance**, then click a surface road within 16 tiles. A station gate
and a protected paved approach connect the road to a platform; leave clear land
between them. Rail passengers need a connected entrance. Catchment coverage is
measured from the entrance roads. You can add up to eight entrances, and remove
them from the station window or with the demolition tool.

The same window adds parallel platforms on either side or lets you draw a custom
platform into the existing station. Use **Lay connecting track** to connect new
platform tracks to your railway. The length slider sets new platforms and open-end
extensions to 2–16 tiles. Extending a connected end converts the next complete
straight, level track section into platform; curved sections and junctions must be
rebuilt first. Clear trains from a platform before extending it. Station identity,
waiting passengers and train orders are preserved. A raised walkway links platforms
for passengers changing sides.

To give a train its orders, press **Add stop or depot** in its window and click
a platform, a depot, or (with **Via waypoint** ticked) any track to pass over.
Clicking a platform sends the train to that platform; the platform list on each
order switches between platforms or back to **Any free platform**, where the
train picks a platform no other train is using.

Track can cross a straight road on the level. The road traffic waits while a train
holds the track over the crossing, and trains wait for road vehicles to clear it.
Roads cannot branch off or be removed at a crossing; remove the track first.
Saves from before free-form track keep their grid railways running.

In the water toolbar, drag **Canal** over clear, level land at sea level to make
it navigable. Use the landscaping tools to lower the terrain first. **Reclaim**
turns water into clear land; it protects ships, active sailing steps and dock /
shipyard berths. Canals have company ownership and monthly maintenance. Raised
waterways and locks are not yet implemented.

The road toolbar includes **Bridge**, **Tunnel** and **Remove span**.
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

Gameplay settings also control inflation and recessions for new worlds. Inflation
raises construction, vehicle and upkeep prices and fares by about 2% per year.
Recessions last 6–12 months, reduce fares by 30% and halve town growth. Finance
details show the price index and recession duration; news announces recovery.
Dedicated servers accept `--inflation` and `--no-recessions` for new worlds.

Press **C** or open **Citizens** in the dock to inspect residents. Filter by town,
search by name or ID, and follow a citizen between home, work, shopping and transit.
The inspector shows upcoming activities and a live activity log. Walking citizens
and waiting passengers are drawn near the camera. The vehicle directory also
filters by company, vehicle type and operating status.

Each station order has a mode selector: normal stop, full load, unload all,
no loading, or pass through. Enable **Via waypoint** when adding orders to pick
a road, track or water tile. Waypoints guide vehicles without loading passengers;
only stopping stations appear in the passenger service graph. Orders also support
depot visits, skipping and sharing through vehicle cloning.

The map window's display selector shows station catchments, current passenger
flows, or town zones. Town details include monthly station boardings and the
share of residents covered by recently served stops. Stations have capacity
based on their facilities and size; crowding and long waits reduce their rating.

New worlds include sea-level rivers and lakes. Older saves retain their original
terrain generator so exploring beyond previously visited chunks remains seamless.
Companies pay monthly property maintenance for their road and track pieces,
signals, station tiles and depots; town-owned roads are maintained by the town.

## Freight, consists and visible loads

Farms produce grain daily. Factories consume grain to make food; workshops
consume food to make goods. Shops accept food and goods. These are the farms,
factories, workshops and shops already generated in towns. Near the camera,
labels show each provider's chain and inventory. Production begins on the next
game day. Stocks are bounded at 4096 units per commodity and station queues are
bounded by facility capacity; uncollected cargo expires after 30 days.

Build stops within catchment of the provider and receiver and give a compatible
vehicle station orders in both directions. Road freight uses ordinary road stops
and depots; rail freight uses platforms; cargo ships use docks and shipyards.
Choose grain lorries / hoppers / bulk coasters, food vans / refrigerated wagons /
coasters, or goods lorries / box wagons / container coasters. Dispatch requires a
complete operating route. Freight can transfer between compatible services; the
company completing delivery earns distance-based income. No-loading, unload-only,
pass-through and full-load orders apply to freight too.

Inside a depot, **Add engine / wagon** offers engines, passenger coaches and
freight wagons. The 2D diagram runs from front on the left to rear on the right.
Drag a vehicle onto another position, or click it and use **Move toward front** /
**Move toward rear**. Engines may go between wagons or at the rear, and trains
can have engines at both ends. **Remove selected** can remove any vehicle while
keeping at least one engine. Stop the train inside its depot before editing. A consist can mix them, up to 12 vehicles, with a powered engine
anywhere in the consist. The vehicle window shows actual loads and capacities per wagon and
per cargo type. The new catalogue also includes an electric locomotive, intercity
coach, regional EMU and heavy freight diesel, subject to introduction dates.

Every nearby walking, waiting, boarding, riding and alighting citizen is drawn
individually. Occupied passenger vehicles use a nearby cutaway view to expose
seated passengers. All 75 people on a train can leave as 75 separate pedestrians.
Walkers follow connected road pavements, cross at junctions, bends and road ends,
and use building doors and the station's entrance path. Trip timing follows the
actual path length. Disconnected destinations use the existing car / lift fallback.
Walking paths and progress survive saves and replan when construction breaks them.
Provider and station inventories and vehicle loads are drawn from simulation
quantities, with grain loads, coloured crates and food / goods markings on closed
vehicles. Station windows also show the freight queue by commodity.

## Optional competitors and sounds

In **Settings > Gameplay**, choose 0-3 AI competitors for new games and enable
**Sounds** for local command confirmation and error tones. Both preferences are
saved with the other settings. Missing audio hardware quietly disables playback.
The dedicated server accepts `--ai 0-3`; headless builds require no audio library.

AI companies build affordable two-stop bus routes over existing connected town
roads. They expand daily into towns they do not yet serve, up to eight buses per
company. They pay normal construction, vehicle, maintenance and finance costs,
and can go bankrupt. This first AI operates buses; it does not build intercity
roads, railways or freight services. Multiplayer clients cannot join AI companies.

## Profiling and validation

Press **F3** to see chunk meshing and worker wait timings. Terrain generation and
meshing use immutable snapshots on up to four workers; OpenGL uploads stay on
the render thread. The simulation remains on its deterministic tick loop.

`transport-poly --seed 20261017 --goto-town 35 --demo-network --profile profile.csv`
records simulation time, CPU frame work, meshing time, worker wait, chunk builds
and visible people per frame. Combine it with `--screenshot smoke.png --frames 450`
for a repeatable run that exits automatically. CPU frame work excludes the display
swap / VSync wait and is not a GPU timing measurement.

The CI workflow builds, tests and packages Windows and Ubuntu clients, and also
builds a headless Ubuntu server with warnings as errors. It can be started manually
after pushing the branch. Save format 18 retains compatibility with versions 1-17;
protocol 22 requires matching client and server builds.

`transport-poly --demo-station --time 12 --no-debug --screenshot station.png --frames 600`
renders a repeatable station scene with road entrances, two platforms, a walkway,
commuters and the station upgrade panel.
