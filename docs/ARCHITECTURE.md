# Architecture

## Targets

| Target                  | Kind       | Depends on            | Purpose                                                    |
|-------------------------|------------|-----------------------|------------------------------------------------------------|
| `tp_core`               | static lib | –                     | logging, RNG, hashing, fixed point, serialisation, pools   |
| `tp_sim`                | static lib | tp_core               | the deterministic game simulation (world, towns, people, infrastructure, vehicles, economy, commands) |
| `tp_net`                | static lib | tp_sim                | sockets, protocol, server and client sessions              |
| `tp_render`             | static lib | tp_sim, GLM           | OpenGL renderer, procedural low poly models                |
| `transport-poly`        | executable | tp_render, tp_net, ImGui, GLFW | the game client (input, camera, tools, UI)        |
| `transport-poly-server` | executable | tp_net                | headless dedicated server                                  |
| `tp_tests`              | executable | tp_sim, tp_net        | unit / determinism / integration tests                     |

The simulation never depends on rendering, windowing or UI code, so the
dedicated server builds without OpenGL or X11.

## Determinism

Multiplayer works like OpenTTD: every peer runs the full simulation and only
*commands* travel over the network. That requires bit-identical results on all
platforms and compilers, hence these rules for everything in `tp_sim`:

* No floating point maths. Use integers and `tp::Fixed` (16.16 fixed point).
* No `std::unordered_*` iteration that can influence the game state, no
  `std::hash`, no pointer ordering - iterate pools / vectors / `std::map`.
* Randomness only from the world's `tp::Rng` (or hashes of the seed), never
  `std::random_device` or `<random>` distributions (they differ between
  standard libraries).
* Sorting uses total orders (or `std::stable_sort`).
* The world state hash (`World::stateHash`) is compared between server and
  clients regularly to detect desyncs.

## Infinite world

The world is split into chunks of 32×32 tiles. Terrain is a pure function of
`(seed, tile)`, so any chunk can be generated on any machine at any time.
A chunk becomes *active* (part of the simulated, serialised game state) through
the `ActivateChunks` command, which clients send as their camera explores. Towns
whose centre lies inside a newly activated chunk are founded at that moment, so
the set of simulated towns grows as players explore.

Rendering uses camera-relative coordinates: chunk meshes are built in chunk
local space and translated by `(chunkOrigin - cameraOrigin)` computed in
integers, so precision does not degrade far away from the origin.

## Time

One simulation tick is 15 in-game seconds; a day has 5760 ticks. At normal
speed the game runs 30 ticks per second, so a day lasts 3.2 real minutes and a
bus crossing a town takes about ten real seconds (roughly an in-game hour).
Weekdays drive the citizens' schedules: Monday–Friday work, Saturday–Sunday
shopping.

## Multiplayer flow

1. A client sends a command request to the server.
2. The server stamps it with the client's company, picks an execution tick and
   broadcasts it to all clients (including the sender).
3. The server broadcasts `FRAME(tick)` messages; clients may only simulate up to
   the last frame they received, executing the commands scheduled for each tick.
4. Joining clients receive a snapshot of the world, then replay the command
   stream from that tick onwards to catch up.
5. Every few ticks the server includes its state hash; a mismatch is a desync.

Single player uses exactly the same server code path, just without sockets.
