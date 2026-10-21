# Changelog

All notable changes to transport-poly. Each release's section here becomes its
release notes on GitHub (see [RELEASING.md](RELEASING.md)).

## [Unreleased]

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

[Unreleased]: https://github.com/Alli1223/transport-poly/compare/v0.3.0...HEAD
[0.3.0]: https://github.com/Alli1223/transport-poly/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/Alli1223/transport-poly/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/Alli1223/transport-poly/releases/tag/v0.1.0
