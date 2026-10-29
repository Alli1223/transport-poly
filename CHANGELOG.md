# Changelog

All notable changes to transport-poly. Each release's section here becomes its
release notes on GitHub (see [RELEASING.md](RELEASING.md)).

## [Unreleased]

## [0.6.0] - 2026-10-29

### Track and signals
- New track can join existing track anywhere along it, not only at its ends and
  joints: a switch is put in where it branches off or joins.
- Signals go anywhere along track. Pointing at a signal shows what it does: the block
  it guards, the way trains may pass and, for one-way signals, where they can't.
- Clicking a signal opens it to make it two-way or one-way, turn it round or remove
  it, with the blocks either side and any train holding them.
- The Signal tool places two-way or one-way signals; a one-way signal stands on the
  side of the track pointed at.
- Signal blocks: the Track and Signal tools can colour the track by block (B).
- Signals can be added or changed on track a train is using.

### Trains
- Trains run at half their rated speed and have weight and momentum: long, heavy and
  fully loaded trains take longer to get going, climbs slow them and descents speed
  them up. The train window shows the train's weight.

### Smooth multiplayer
- Trains, cars, trams, ships and people move smoothly for players who join a game.
  A client now plays the server's ticks back at a steady pace a little behind the
  newest one (about a tenth of a second, more if ticks arrive unevenly) instead of
  running each tick the moment it arrives, so motion no longer stops and lurches
  when the host is busy or the network hiccups.
- The regular desync check no longer freezes the game: it took about 80 ms every
  second on a large map (on the host and on every client) and now takes about 4 ms,
  spread over all processor cores.
- Busy stations no longer slow the game down: boarding a train with hundreds of
  people waiting costs a fraction of what it did (a large town's simulation runs
  about thirteen times faster overall).
- After building anything, passenger routes and everyone's plans are worked out on
  all processor cores, so construction in a big game no longer causes a hitch.
- The players under the chat: click a name to look where that player is pointing.
  Names in the players window do the same.
- The dedicated server takes `--threads N` to leave cores free, and
  `--bench ticks` measures how fast a saved game runs.

### Station entrances
- A rail station passengers can't reach shows **No road access** in its window, which
  highlights **Add road entrance** with a short explanation.
- An entrance can go on a house beside a road: the house is demolished in the same command
  at the usual demolition cost, which the entrance's price includes and its tooltip names.
  Industries, and buildings the town won't let you demolish, are refused with the reason.

### Autosaves
- The game autosaves every five minutes of play (time paused doesn't count) and keeps the
  last five, `autosave-1` to `autosave-5`, overwriting the oldest. This replaces the monthly
  `autosave.tpsave`, which is no longer written. Settings turns autosaves on or off; an older
  settings file's monthly interval counts as on.
- Single player games and multiplayer hosts autosave; joined players don't. The dedicated
  server's monthly `--autosave months` is unchanged.
- The load menu says how long ago each save was written and marks autosaves with a clock.

### Fixes
- Passengers waiting at some stations walked off in a straight line forever, and were
  still counted as waiting. Waiting spots are now always on the platform, and anyone
  who had wandered off is called back.
- The Linux game no longer hangs on a black window at startup.
- Save format 32 loads older saves; multiplayer protocol 43 requires matching game
  versions.

## [0.5.0] - 2026-10-28

### Passenger and freight stations
- Rail stations are built for passengers or for freight. Passenger stations keep their
  canopies, benches and footbridges and handle passengers only; freight stations have
  concrete loading docks, a goods shed, a gantry crane and floodlights, handle freight
  only, and reach the industries around their platforms without a road entrance.
- A station's window changes what it is for. Stations from earlier saves take both.

### Placing stations
- The Station tool's panel chooses what a new station is for, one to four platforms
  built side by side, and a fixed length of 4 to 16 tiles (or as far as you drag).
- While placing, a mark at every tile and a label show the platform length, and the
  tooltip gives the cost of every platform and the longest train the station takes.
- A train's depot window shows how many tiles long it is, and warns when it is longer
  than the platform at one of its stops.

### Lines
- **New line** in the Lines window makes a named line before any vehicle runs it:
  choose the kind of vehicle, click its stops on the map, and create it.
- **Edit line** changes a line's stops for every vehicle on it, and adds vehicles or
  takes them off; a vehicle's window can also put it on a line.
- Each stop sets which goods it picks up and drops off. Freight is only sent with a
  service that will drop it off where it is going.

### Ground cover
- Zoomed in, the ground grows what suits it: meadows of grass with drifts of
  buttercups, daisies, poppies and violets, ferns under the trees, reeds by the water,
  heather on the heath and tundra, dry tufts and stones on the steppe and in the
  desert, and bare snow on the peaks. It sways in the wind and fades as the camera
  pulls back.
- **Ground detail** in the graphics settings (Off, Some, Lush), set by the presets.

### Fixes
- Command line choices such as `--climate`, `--mountains` and `--ai` apply over the
  saved settings instead of being overwritten by them.
- Save format 31 loads older saves; multiplayer protocol 40 requires matching game
  versions.

## [0.4.0] - 2026-10-27

### Laying track over the land
- While laying track, platforms or depots the ground shows its elevation: colours
  by height, a contour line at every height level and a bold one every five. Track
  that follows a contour stays level.
- A gold line marks the height of the track being laid, or before the first click
  the height of the ground under the pointer.
- The track plan's tooltip draws its height profile, the track over the ground, so
  cuttings, embankments and bridges show at a glance.
- **H** shows or hides the elevation view, with the track tools or at any other
  time; the choice is remembered (also under Settings, Controls).

### Industries and fish
- Industries suit the land they stand on: quarries and mines on and around the
  mountains, cut into the hillside, and logging camps deep in thick woods.
- Fish farms: floating net pens in lakes and the sea near the shore, within reach
  of a dock or a quay. Fish is a new refrigerated cargo, sold at the shops.

### Terrain and vegetation
- Grass, rock, sand and snow shade gradually into one another instead of changing
  tile by tile, in every world.
- New worlds have gentler small-scale relief, a snow line that wanders up and down
  the mountains with scree gathering below it, and woods in stands of oak, birch
  and pine that mix where they meet, fringed with bushes, with willows by the water
  and copses across the open country. Older worlds keep their terrain.

### Timetables and vehicle replacement
- A timetabled wait at any stop, and timing points that hold vehicles so a service
  leaves evenly spaced: a fixed interval, or the measured round trip shared out
  between its vehicles. The lines window can space a whole line.
- Autorenew old vehicles, and replace models with compatible newer ones at their
  next depot visit, trading the old one in.

### Passengers, roads and subsidies
- Passenger feedback: rides are scored on the wait, crowding and speed, giving
  vehicle, line and company satisfaction and a feed of complaints and praise.
- Subsidies: double pay for a year to the first company to carry passengers between
  unlinked towns, or an industry's produce to a new place.
- Private cars drive on any connected road, between towns too, and two intercity
  coaches join the road vehicles.
- Save format 30 loads older saves; multiplayer protocol 39 requires matching game
  versions.

## [0.3.0] - 2026-10-21

### Private cars
- Citizens without a usable public transport route drive longer local journeys
  on connected roads within their home town. Short journeys remain walks.
- Car ownership increases with the year, including among existing residents.
- Visible coloured saloons, hatchbacks and estates with drivers, night lights and
  positional engine sounds. Cars queue with road traffic and re-plan after road removal.
- Save format 28 retains active car routes and loads older saves; multiplayer
  protocol 37 requires matching game versions.

### Fixes
- Returning to the main menu closes the session between frames, preventing the
  news, minimap and renderer from accessing a world that has just been destroyed.

### Audio
- Three original synthesized funk/jazz arrangements with bass, drums, electric
  piano, clavinet, brass and vibraphone parts.
- Named soundtrack playlist and next-track control, with fuller instrument tones
  and melodies arriving earlier in each tune.
- Positional steam, diesel and electric trains, road vehicles, trams and ships;
  nearby vehicles get louder as the camera zooms in, with departure whistles/horns.
- Rhythmic steam exhaust, brake-air releases and speed-dependent wheel/joint
  sounds from every locomotive and carriage, with up to 24 nearby sources mixed.
- Layered car engines and tyre noise, boat engine/propeller wash, and local water,
  countryside and town soundscapes.
- Birds, crickets, water, farms and industry ambience, construction/demolition,
  purchases, interface feedback and news sounds.
- Separate master, music, vehicle, ambience and interface volume controls in the
  Audio settings tab, plus background muting. Existing mute choices are preserved.

### Graphics settings
- Windows release builds open without a console window and default to fullscreen;
  an existing saved windowed preference is respected. Debug builds keep their console.
- Borderless fullscreen, monitor selection and an Alt+Enter shortcut.
- Low, Balanced and High presets, plus separate controls for anti-aliasing, frame
  rate limits, brightness, shadow resolution, terrain detail and tree draw distance.
- Individual cloud, cloud shadow, mist, night mist, smoke/steam, light halo, tree
  sway and water animation controls. Existing settings files remain compatible.

### Visibility
- Night mist is thinner and uses cool sky colours instead of the orange sunset
  lighting, keeping terrain and transport routes easier to see after dark.

## [0.2.0] - 2026-10-20

### Installation and updates
- Windows installer with a selectable folder, per-user or all-user installation,
  Start menu and optional desktop shortcuts, and an uninstaller that preserves saves.
- Background startup checks for new stable GitHub releases, with release notes,
  download progress, checksum verification, skip/later choices and install/restart.
- Installed upgrades keep the installation folder and scope; portable Windows
  upgrades back up replaced files and restore them if copying fails.
- Release builds publish the installer alongside portable Windows and Linux packages.

### Towns and industries
- Completed passenger arrivals and goods deliveries increase town growth.
- Every industry can expand to production level 8 through sustained completed exports;
  processing plants gain capacity too, provided their inputs are supplied.
- Larger countryside sites with varied starting sizes, extra fields, paddocks,
  storage and outbuildings, plus expansion meters and industry growth news.
- Save format 27 loads older saves; multiplayer protocol 36 requires matching peers.

### Multiplayer
- A company lobby when joining: found your own company with a name and a
  colour, join a friend's company to play it together (co-op), or watch.
- Co-op companies can be locked with a password; servers can require one.
- Every player's pointer is shown in the world in their company's colour.
- Chat to everyone, to your company, or privately to one player; the host can
  remove players.
- A companies window with a league table ranked by a performance rating out of
  1000, graphs comparing every company month by month, and each company's
  rating broken down; news of a new league leader.
- Competitions: the first company to a target value, or the best rated after
  a number of years, wins.
- Find another player's pointer from the players window; dedicated server
  console commands to list companies, kick players, pause, and set or clear
  the server and company passwords.

## [0.1.0] - 2026-10-20

The first release: a low poly transport game in the spirit of OpenTTD and
Transport Fever, with deterministic lockstep multiplayer.

### World
- Endless chunked terrain with smooth hills and mountains; new worlds choose
  their land (continents, mainland, islands, archipelago or a single island),
  the amount of mountains, lakes and rivers, and the climate (temperate, arid,
  arctic, tropical or varied biomes, each with its own ground and trees).
- Weather that changes day to day: drifting clouds with moving shadows,
  overcast light and valley mist; a day and night cycle with lit windows,
  street lamps, stars and the moon; trees sway in the wind.
- Towns of individually simulated citizens who live, work, shop and visit
  other towns, walking along pavements and riding your services. Towns grow
  with good service and ask for goods as they grow.
- Countryside industries with production chains: farms, logging camps,
  quarries and mines feed sawmills, dairies, textile mills, brickworks, steel
  mills and factories.

### Transport
- Buses, articulated trams, trains and ships, with stops, stations, depots,
  docks and shipyards.
- Free-form curved railways in the style of Transport Fever that shape the
  ground into embankments and cuttings, with automatic bridges, tunnels and
  level crossings, path signals, and stations of straight platforms at any
  angle that can be upgraded and joined to roads by entrances.
- Overhead electrification for electric trains.
- Train consists of engines, coaches and freight wagons, built in the depot;
  freight (bulk, refrigerated, piece goods and timber classes) carried
  between industries and towns.
- Orders with full load, unload, no loading and pass-through stops,
  waypoints, depot visits and shared order lists; vehicle reliability,
  breakdowns and servicing.

### Seeing what happens
- Click a person to see where they are going and why, their journey step by
  step and their route on the map; hover or click any vehicle or carriage.
- A lines window and lines map layer for every route you run, plus station
  catchment, passenger flow and town zone layers, a minimap and news.
- Detailed vehicles: a steam tank engine with turning wheels and rods that
  chuffs steam, diesels, electric engines with pantographs, coaches with
  bogies and gangways, and passengers visible through the windows.

### Economy and play
- Companies with loans, ledgers, inflation, recessions and bankruptcy,
  optional computer competitors, town ratings and station ratings.
- Multiplayer through a dedicated server or a hosted game, with chat, a
  player list and host-controlled pause and speed; save, load and autosave.

[Unreleased]: https://github.com/Alli1223/transport-poly/compare/v0.6.0...HEAD
[0.6.0]: https://github.com/Alli1223/transport-poly/compare/v0.5.0...v0.6.0
[0.5.0]: https://github.com/Alli1223/transport-poly/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/Alli1223/transport-poly/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/Alli1223/transport-poly/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/Alli1223/transport-poly/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/Alli1223/transport-poly/releases/tag/v0.1.0
