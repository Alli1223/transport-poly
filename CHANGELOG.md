# Changelog

All notable changes to transport-poly. Each release's section here becomes its
release notes on GitHub (see [RELEASING.md](RELEASING.md)).

## [Unreleased]

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

[Unreleased]: https://github.com/Alli1223/transport-poly/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/Alli1223/transport-poly/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/Alli1223/transport-poly/releases/tag/v0.1.0
