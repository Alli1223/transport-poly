# transport-poly

A low poly transport simulation game in the spirit of OpenTTD: build bus, tram,
rail and ferry networks across an infinite, procedurally generated world whose
towns are populated by individually simulated citizens. Every resident has a
home, a work place and a favourite shop: five days a week they commute to work,
two days a week they go shopping in the nearest city - and your network is how
they get there. Every resident with a usable connection plans a daily trip to
another town: work or business on weekdays, shopping and visits on free days.
Public transport is their preferred mode, including long journeys and car owners.
Rail stations serve homes and destinations across the town's connected streets,
beyond the local stop catchment. Opening a service attracts residents at home that
same day; closing it refreshes their plans. Stations need connected road entrances.
Passengers keep waiting until a vehicle with space arrives while their service
exists. A full train leaves the remaining passengers queued for the next one.

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

The sky has weather that changes from day to day, the same for every player:
clouds drift over the land with the wind and cast moving shadows, overcast days
dim the sun and grey the sky, and humid mornings fill the valleys with mist
that thins as the sun climbs. Zoom out to see the clouds themselves. Turn it off
with **Weather effects** in Settings.

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
**Add road entrance**, then click a surface road or a cleared plot beside it within 64 tiles. A station gate
and a protected paved approach connect the road to a platform; leave clear land
between them. Rail passengers need a connected entrance. Catchment coverage is
measured from the entrance roads. You can add up to eight entrances, and remove
them from the station window or with the demolition tool.

Use **Demolish** in the road or rail tools, **Building** in landscaping, or
**Clear building for access** in the station window to clear a house or other town
building. Hover to highlight its whole footprint and see the cost, then click.
The base cost is $3,000 plus $150 per resident, adjusted by inflation. Demolition
lowers the town's opinion of your company and may be refused at a poor rating.
You can then extend a road through the plot or place an entrance on the cleared
plot directly beside the existing road. Leave a continuous walkable corridor from
the gate to a platform; any steep ground needs levelling.

The same window adds parallel platforms on either side or lets you draw a custom
platform into the existing station. Use **Lay connecting track** to connect new
platform tracks to your railway. Hover **Add parallel left/right** or **Extend end**
to preview the change on the map before clicking: gold marks the existing platform,
blue shows the proposed addition with a direction arrow, and red marks a blocked
change. The tooltip shows the compass direction, actual length, cost or obstruction.
The length slider sets new platforms and open-end
extensions to 2–16 tiles. Extending a connected end converts the next complete
straight, level track section into platform; curved sections and junctions must be
rebuilt first. Clear trains from a platform before extending it. Station identity,
waiting passengers and train orders are preserved. Compact stone-and-steel footbridges
link both sides of every platform, with visible steps, landings and railings. Their
layout is shared by pedestrians and rendering, and exists even without a road
entrance. Platforms have paving joints, tactile edges, benches, bins, lamps, signs
and partial green canopies. Pedestrian approaches use matching stone paving,
kerbs, pedestrian stencils and a planted entrance gateway, continuing onto the
platform.
Passengers choose stable, scattered waiting spots on both sides of their departure
platform. Platform-specific orders and the approaching train's reserved route
determine where they wait. They walk across the concourse after a platform change.
Boarding and alighting happen together through doors along the carriages, with
through-passengers keeping their seats. People already on the platform can board
while others enter the station; approaching pedestrians do not keep extending the
train's stop.

Bus and tram stops within four tiles of a train station's road entrance can act
as interchanges when pavements join them. Passengers walk between the stops,
wait for the feeder service, and ride toward home or work. Trip choices favour
useful local services over long walks across town. Train arrivals also consider
newly available feeder services when their saved itinerary ends at the railway.

To give a train its orders, press **Add stop or depot** in its window and click
a platform, a depot, or (with **Via waypoint** ticked) any track to pass over.
Clicking a platform sends the train to that platform; the platform list on each
order switches between platforms or back to **Any free platform**, where the
train picks a platform no other train is using.

**Electrify** puts up overhead wires over the track you click or drag along;
Shift-click wires all of your track joined to it, and **Remove wires** takes them
down again. Electrified track has masts beside it with the wires strung over the
line. The Continental Electric and the Regional EMU are electric: a train whose
engines are all electric only runs on electrified track, so wire its whole route
(including the depot track) before sending it out. A diesel or steam engine
anywhere in the train frees it from the wires.

Steam engines turn their driving wheels and rods as they run and chuff steam and
smoke from the chimney, four beats to a turn of the wheels and darker when pulling
hard; they shut off steam to coast into stations and simmer while they stand.
Diesels leave a thin exhaust haze, and electric engines raise their pantographs.

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

Hover over a vehicle or a person on the map to highlight it and see what it is
doing; click to open it. Every carriage of a train can be clicked, zoomed in or
out. A person's card says where they are going and why (work, shopping, home, a
business trip or a day out in another town), lists their journey step by step
(walk to the station, wait, ride, change, walk on) and draws their route on the
map, with a ring around their destination. A vehicle's window lists who is
aboard and the towns they are bound for.

Press **L** or open **Lines** in the dock for your lines: vehicles that share
orders, or call at the same stops in the same order, form a numbered, coloured
line. The window shows each line's vehicles, passengers carried and profit, how
full it is, a diagram of its stops with its vehicles between them, and the
passengers waiting at each stop; the chosen line is drawn on the map along the
track or roads it uses.

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
flows, town zones, or every line in its colour. Town details include monthly station boardings and the
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

### Countryside industries and town needs

New worlds have industries in the countryside, suited to the land they stand on:
grain, sheep and dairy farms on open lowland, logging camps in the forests, and
quarries, iron mines and coal mines in the hills and mountains. Their produce goes
to processing plants:

| Raw material | Plant | Makes |
| --- | --- | --- |
| Milk (dairy farm) | Cheese dairy | Cheese |
| Wool (sheep farm) | Textile mill | Cloth |
| Logs (logging camp) | Sawmill | Planks |
| Stone (quarry) | Brickworks | Bricks |
| Iron ore and coal (mines) | Steel mill | Steel |
| Steel and planks | Goods factory | Goods |
| Planks and cloth | Furniture factory | Furniture |

A raw producer has a production level from 1 to 4; a plant with two inputs works
only while it has both. Each cargo travels in one class of vehicle, and every cargo
of the class shares its space: hoppers, grain lorries and bulk coasters take grain,
stone, coal and ore; refrigerated vehicles take food, milk and cheese; box vans take
goods, wool, cloth and furniture; flat wagons, timber lorries and deck cargo
coasters take logs, planks, bricks and steel. Click an industry to see what it
makes, its stock, how much of it was transported last month and which stations
reach it.

Towns need goods delivered to keep growing: a village asks for one of food, planks
or cheese, and as it grows it asks for more and more elaborate things (bricks,
cloth, goods, furniture, steel), chosen at random. Deliveries count at the town's
shops, offices and civic buildings. Each need met speeds the town's growth, and a
town whose needs are not all met stops growing when it reaches the size of its next
need. The town window shows each need and this month's deliveries against it.
Worlds saved before industries keep growing as they did.

The depot's train builder has separate **Engines**, **Passenger carriages** and
**Freight wagons** menus. Each lists the available models with a miniature preview
of the actual vehicle, capacity, speed, purchase price and annual running cost.
One click buys an engine or adds a carriage to the selected stopped train. Buying
an engine selects the new train and opens the carriage menu. Select a train's
consist to change which train receives purchases; **Add engine** adds another
locomotive to that train. The 2D consist runs from front on the left to rear on the right.
Drag a vehicle onto another position, or click it and use **Move toward front** /
**Move toward rear**. Engines may go between wagons or at the rear, and trains
can have engines at both ends. **Remove selected** can remove any vehicle while
keeping at least one engine. Stop the train inside its depot before editing. A consist can mix them, up to 12 vehicles, with a powered engine
anywhere in the consist. The vehicle window shows actual loads and capacities per wagon and
per cargo type. The new catalogue also includes an electric locomotive, intercity
coach, regional EMU and heavy freight diesel, subject to introduction dates.
Passenger choices also include suburban, compartment, panorama, dining, sleeper
and double-deck carriages with distinct bodies, seating capacities and running costs.

Every nearby walking, waiting, boarding, riding and alighting citizen is drawn
individually. Articulated trams turn their sections independently, following their
recent route through corners with joined flexible bellows; passengers and picking
follow the individual sections. Trains and trams retain their full roofs and show seated passengers
through transparent windows in hollow bodies. Double-deck coaches seat passengers
on both levels. All 75 people on a train can leave as 75 separate pedestrians.
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
after pushing the branch. Save format 25 retains compatibility with versions 1-24;
protocol 33 requires matching client and server builds. Older saves adopt daily
connected-town demand when loaded; current saves preserve planned departure times.

`transport-poly --demo-station --time 12 --no-debug --screenshot station.png --frames 600`
renders a repeatable station scene with road entrances, two platforms, a walkway,
commuters and the station upgrade panel.

`transport-poly --preview-passengers --time 12 --no-debug --screenshot passengers.png --frames 50`
shows occupied carriages and a tram from outside, including both decks of a coach.

`transport-poly --preview-station-details --time 12 --no-debug --screenshot station-details.png --frames 50`
shows the platform furniture, compact footbridges and matching pedestrian approach
without the station panel.
