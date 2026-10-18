# transport-poly — Development TODO

A low poly, infinitely generated transport tycoon with OpenTTD-style gameplay
(buses, trams, trains and boats), individually simulated citizens and
server-authoritative lockstep multiplayer. C++23 + OpenGL 3.3, Windows & Linux.

Legend: `[ ]` todo · `[x]` done · `[~]` basic version done, more to do later

The list is worked through roughly top to bottom. Every item should end in a
buildable, tested commit. Simulation code must stay deterministic (integer /
fixed-point maths only, deterministic containers and RNG) because multiplayer
relies on every peer computing exactly the same game state.

## M0 — Project foundation
- [x] 0.1 Repository hygiene: .gitignore, .gitattributes, .editorconfig, .clang-format
- [x] 0.2 Root CMake project (C++23, warning flags, warnings-as-errors option) + CMakePresets (Windows MSVC/VS 2026, Linux GCC/Clang)
- [x] 0.3 Third-party dependencies via FetchContent pinned to release tags: GLFW, GLM, Dear ImGui
- [x] 0.4 Minimal OpenGL 3.3 core function loader (no code generator required)
- [x] 0.5 Target layout: tp_core, tp_sim, tp_net, tp_render, transport-poly (client), transport-poly-server (dedicated), tp_tests
- [x] 0.6 Tiny unit test framework + CTest integration
- [~] 0.7 GitHub Actions CI: build + test on Windows and Ubuntu (workflow written; runs once the repo is pushed)
- [x] 0.8 README with build instructions; docs/ARCHITECTURE.md

## M1 — Core library
- [x] 1.1 Integer aliases, assertions (`TP_ASSERT`), utility helpers
- [x] 1.2 Logging (console + file, levels, std::format based)
- [x] 1.3 Deterministic RNG (SplitMix64 seeding, PCG32 streams, unbiased bounded ints)
- [x] 1.4 Integer hashing (noise lattice hashing, FNV-1a state checksums)
- [x] 1.5 Fixed-point number type (16.16) and integer maths helpers (isqrt, clamp, lerp)
- [x] 1.6 Binary serialisation (little-endian writer/reader, varints, strings, bounds checked) with a symmetrical archive visitor
- [x] 1.7 Strong ID types and object pools with stable ids and deterministic iteration
- [x] 1.8 Game calendar: ticks → minute / hour / day / weekday / month / year, date formatting
- [x] 1.9 Money type (int64) and formatting
- [x] 1.10 Name generator (syllable based town / station / company names)

## M2 — Infinite world & terrain
- [x] 2.1 Tile / chunk coordinate types, floor-div helpers, directions & neighbours
- [x] 2.2 Deterministic integer value noise + fractal sum with bounded slope (guarantees OpenTTD-like corner height rules everywhere, across chunk borders)
- [x] 2.3 Terrain generator: continents & sea, plains, hills, mountains; ground types (grass, sand, rock, snow); forests
- [x] 2.4 Rivers & lakes (deterministic, crossing chunk borders; sea-level basins, old saves retain their generator)
- [x] 2.5 Chunk storage: corner heights, ground, trees, road / tram / rail layers, structures, owner
- [x] 2.6 Active chunk map; activation = deterministic generation (pure function of seed + coordinate)
- [x] 2.7 Tile slope classification (flat, inclined, steep) and foundations
- [x] 2.8 Terraform: raise / lower corners with neighbour propagation, cost, restrictions
- [x] 2.9 Tree planting / removal, clearing cost

## M3 — Rendering foundation
- [x] 3.1 Window + GL 3.3 core context (GLFW), vsync, DPI awareness, resize
- [x] 3.2 Shader program wrapper (embedded GLSL), per-frame uniform block
- [x] 3.3 Mesh / vertex buffer / instancing abstractions
- [x] 3.4 RTS camera: pan, orbit, zoom, tilt, smoothing, follows terrain height
- [x] 3.5 Camera-relative rendering for infinite coordinates (no float precision loss far from origin)
- [x] 3.6 Flat-shaded low poly terrain chunk mesher with vertex colours
- [x] 3.7 Chunk streaming around the camera (mesh cache, rebuild on change, frustum culling)
- [x] 3.8 Directional sun + hemispheric ambient lighting, distance fog, sky gradient
- [x] 3.9 Low poly animated water
- [x] 3.10 Shadow mapping (directional, PCF)
- [~] 3.11 Day / night cycle bound to the game clock, lit windows and street lights at night (sky + sun follow the clock; lit windows later)
- [x] 3.12 Mouse picking: ray vs heightfield, tile / corner hit info
- [~] 3.13 Procedural low poly model library (houses, apartments, offices, shops, factories, trees, stations, depots, vehicles, people) (trees, 15 building types, buses, trams, stops, depots)
- [~] 3.14 Instanced renderers for trees, buildings, vehicles, people (trees, buildings, vehicles)
- [x] 3.15 Selection highlight + ghost previews for build tools
- [x] 3.16 Screenshot key

## M4 — Game state, commands, companies
- [x] 4.1 `World` game state container and fixed tick loop (30 ticks/s × game speed)
- [x] 4.2 Command framework: typed commands (std::variant), serialisation, test / execute phases, cost, error messages
- [x] 4.3 Command queue scheduled by tick; single player runs through the same server path as multiplayer
- [x] 4.4 Companies: create, colour, name, money, loan, bankruptcy (four debt quarters, fleet liquidation, public routes and disabled company commands)
- [x] 4.5 Finances: income / expense categories, monthly & yearly ledgers, interest, maintenance (60 monthly and 20 yearly historical ledgers; costs booked after period rollover)
- [x] 4.6 World state hash for desync detection (complete canonical serialized simulation state; render revisions and rebuildable caches excluded)
- [x] 4.7 Determinism test: two worlds + same command stream ⇒ identical hash over many ticks
- [x] 4.8 Exploration: clients request chunk activation as the camera moves; server rate-limits

## M5 — Towns, buildings & people
- [x] 5.1 Deterministic town sites per region, size classes (village / town / city)
- [x] 5.2 Town founding: name, centre, initial road layout (grid / organic)
- [x] 5.3 Building catalogue: houses, terraces, apartments, offices, retail, workshops/factories with capacities (residents / jobs / shoppers)
- [x] 5.4 Building placement along roads with zoning by distance from the centre
- [x] 5.5 People: individual residents per home (home, work place, shop)
- [x] 5.6 Job market: assign work places (same town first, then nearby towns), unemployment
- [x] 5.7 Shopping: assign a retail building in the nearest city
- [x] 5.8 Weekly schedule: 5 work days, 2 shopping days; departure times; dwell times
- [x] 5.9 Trip planner: walk / public transport / private car (abstract) mode choice
- [x] 5.10 Event driven people simulation (priority queue by tick) for scalability
- [x] 5.11 Town growth (roads + buildings) driven by population, jobs and transport service (growth rate scales with transit share and served stations)
- [x] 5.12 New towns spawn over time in explored land (villages, preferably around remote stations)
- [x] 5.13 Town ratings per company & local authority restrictions
- [~] 5.14 Town statistics (population, passengers last month, served %) (monthly trip stats per mode)

## M6 — Infrastructure
- [x] 6.1 Roads: road bits per tile, build / remove along a drag line, slopes, costs, town roads
- [x] 6.2 Road rendering (straight, curves, junctions, slopes, dead ends)
- [x] 6.3 Rails: 6 track bits per tile (incl. diagonals), drag building, slopes
- [x] 6.4 Rail rendering (ballast, sleepers, rails; diagonals)
- [x] 6.5 Level crossings (flat perpendicular road + rail, either build order; traffic lights, train reservations and road clearance)
- [x] 6.6 Signals: block, path and one-way path signals; signal rendering (distinct block occupancy checks across all branches, path reservations, direction cycling and red / green lamps)
- [x] 6.7 Tram tracks (on roads and tram-only)
- [x] 6.8 Bridges and tunnels for road / rail (straight spans between level ends, separate deck height, navigable water below bridges, terrain-covered tunnels, maintenance and whole-span removal)
- [x] 6.9 Canals and water tiles editing (sea-level flooding / reclamation tools, ownership, costs, maintenance and protection for ships and berths; elevated waterways / locks are not implemented)
- [x] 6.10 Stations: bus stop, tram stop, rail station (platform length / count), dock; names; joining adjacent tiles; catchment radius
- [x] 6.11 Depots: road, tram, rail, ship (shipyards on the shore)
- [x] 6.12 Demolish tool with cost; ownership checks
- [x] 6.13 Infrastructure maintenance costs (monthly, per owner and infrastructure piece / facility tile)

## M7 — Vehicles
- [x] 7.1 Vehicle model catalogue (buses, trams, locomotives, multiple units, carriages, ferries) with stats & intro dates
- [x] 7.2 Buy / sell / clone vehicles in depots; start / stop; send to depot
- [~] 7.3 Orders: go to station (stop / non-stop), go via, full load, unload, depot service; shared orders; skip order (station + depot orders, full load, no loading, skip, shared orders; via orders later)
- [x] 7.4 Road pathfinder (A* over the road graph with deterministic tie breaking)
- [x] 7.5 Road vehicle movement: lanes, curves, junctions, queueing, gridlock breaker
- [x] 7.6 Tram movement (tram network, articulated trams)
- [x] 7.7 Rail pathfinder and train movement: consists, acceleration / braking, curve speed limits, slopes (braking for upcoming curves; restriction until the last carriage clears)
- [x] 7.8 Signal logic, path reservation, collision prevention
- [x] 7.9 Ship pathfinder over water tiles and ship movement
- [x] 7.10 Loading / unloading at stations, dwell time
- [x] 7.11 Vehicle ageing, reliability, breakdowns, servicing at depots
- [x] 7.12 Vehicle profit tracking (this year / last year), running costs

## M8 — Passengers & economy
- [x] 8.1 Link graph from vehicle orders (capacity, frequency, travel time)
- [x] 8.2 Station-to-station routing tables (Dijkstra per station, periodic recompute)
- [~] 8.3 Passengers waiting per station per next hop; station capacity; station rating (waiting per next hop; simple rating)
- [x] 8.4 Boarding / alighting and transfers; passengers are the actual Person entities
- [x] 8.5 Fare payment by distance & travel time (per leg, per company)
- [x] 8.6 Passengers giving up after long waits ⇒ rating penalty
- [ ] 8.7 Economy events: inflation (optional), recessions
- [ ] 8.8 Freight milestone: industries, cargo chains, trucks / freight wagons / cargo ships

## M9 — Save / Load
- [x] 9.1 Full world serialisation (versioned)
- [x] 9.2 Save / load UI, autosave every N months, quicksave (save / load menus, monthly autosave, F5 quicksave; host-only in multiplayer)
- [x] 9.3 Round-trip determinism test (save → load → continue ⇒ same hash)

## M10 — Multiplayer
- [x] 10.1 Cross-platform TCP socket layer (Winsock / BSD sockets), non-blocking
- [x] 10.2 Packet framing & protocol definitions, version handshake
- [x] 10.3 Server: client management, company assignment, command validation & scheduling, frame broadcast
- [x] 10.4 Client: map download, catch-up, frame-limited simulation, command submission
- [x] 10.5 Sync checks (state hash) & desync handling
- [~] 10.6 Chat & client list (chat window; client list window later)
- [x] 10.7 Dedicated headless server executable (CLI args, console commands)
- [x] 10.8 Host a game from the client (listen server) and join by address
- [ ] 10.9 Pause / speed control rules in multiplayer
- [x] 10.10 Integration test: server + 2 clients ⇒ identical hashes

## M11 — UI
- [~] 11.1 Dear ImGui integration, theme, font (Roboto font, rounded dark theme)
- [~] 11.2 Main menu: new game (seed, settings), load, host, join, settings, quit (new / load / host / join / quit; settings later)
- [x] 11.3 Main toolbar, time controls (pause, speeds), date / money display
- [~] 11.4 Construction toolbars: road, tram, rail, signals, stations, depots, water, terraform, trees, demolish (grouped toolbar: landscaping, road, tram, rail and water tools incl. signals, platforms, docks and depots with previews and cost)
- [x] 11.5 Depot window: buy vehicles (model list with stats); train consists with carriages
- [x] 11.6 Vehicle window: status, orders editor, details, follow camera
- [~] 11.7 Vehicle list per company, filters (basic list)
- [x] 11.8 Station window (waiting passengers by destination, ratings)
- [x] 11.9 Town window and town directory
- [x] 11.10 Finance window (monthly / yearly ledgers, history, loans, debt warnings) & company details (editable name and livery; bankruptcy status)
- [x] 11.11 News feed and error message popups (news banner + history: first arrivals, new models, town growth, new villages, ageing vehicles)
- [x] 11.12 Minimap
- [~] 11.13 Overlays: catchment areas, passenger flow, town zones (catchment areas of open stations and while placing stops; flow / zones later)
- [ ] 11.14 Settings window: graphics (shadows, view distance), controls, gameplay
- [ ] 11.15 Citizen inspector (follow an individual person's day)

## M12 — Polish & release
- [~] 12.1 Performance pass (profiling, multithreaded meshing, LOD) (terrain LOD + skirts done)
- [ ] 12.2 Sounds (optional)
- [x] 12.3 Packaging: CPack zip (Windows), tar.gz (Linux)
- [ ] 12.4 AI competitors (optional)
