#pragma once
// Curved roads: a smooth curve (a cubic Bezier) from the edge of one road tile to the edge of
// another, laid on the grid as a chain of ordinary road tiles. Route finding, private cars,
// stops, catchments and town growth see the chain like any other road; the curve itself gives
// how it is drawn, how far vehicles drive across each of its tiles, where they are on the way
// and how fast they may take it.
//
// The tiles between the two ends belong to the curve alone: other roads join it at its ends
// only. Tiles the road surface overhangs at a corner (its verge) stay free of buildings.

#include "core/money.hpp"
#include "core/types.hpp"
#include "sim/coords.hpp"
#include "sim/ids.hpp"
#include "sim/track.hpp"

#include <optional>
#include <string>
#include <vector>

namespace tp {

class World;

inline constexpr i32 kRoadCurveMinRadius = 2 * kTrackUnits; // tightest curve a road may take
inline constexpr i32 kRoadCurveMaxTiles = 64;               // most tiles between the two ends
inline constexpr i32 kRoadCurveVerge = 104;                 // the road surface (kerb to kerb) reaches this far from the centre line

struct RoadCurve {
    TilePos a, b;                       // the road tiles the curve joins
    Dir sideA = Dir::N, sideB = Dir::N; // the side of each end tile the curve leaves through
    u8 owner = 0xFF;                    // road owner byte (a company, or kOwnerTown)
    std::vector<TilePos> tiles;         // the tiles between the ends, from a to b

    // Derived from the ends when built or loaded (not saved).
    std::vector<TrackSample> samples; // centre line from a's edge to b's edge (h unused)
    std::vector<i32> tileStart;       // distance along the centre line where it enters tiles[i]; back() is the length
    std::vector<TilePos> verge;       // other tiles the road surface reaches over
    i32 length = 0;
    i32 minRadius = 0; // tightest bend, units (0 when straight)

    // Entry and exit sides of tiles[i], going from a to b.
    Dir entrySide(usize i) const;
    Dir exitSide(usize i) const;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(a, b, sideA, sideB, owner, tiles);
    }
};

struct RoadCurvePlan {
    bool ok = false;
    std::string error;
    Money cost = 0;
    RoadCurve curve; // with its derived geometry
};

namespace roadcurves {

// The midpoint of a tile's edge, in track units.
TPoint edgePoint(TilePos t, Dir side);
// Works out a curve's shape from its ends: the centre line, the tiles it runs over and the
// tiles it overhangs. False (with a reason) when no road curve joins the ends that way.
bool shape(RoadCurve& c, std::string* error = nullptr);
// The side an end tile is joined through for a curve from `a` (leaving through `sideA`) to `b`:
// straight on when `b` lies ahead, round a quarter turn when it lies well off to one side.
Dir naturalEndSide(TilePos a, Dir sideA, TilePos b);
// Plans a curve owned by `owner` without changing anything.
RoadCurvePlan plan(const World& w, TilePos a, Dir sideA, TilePos b, Dir sideB, u8 owner);
// Builds a plan made by plan() for the same world state.
RoadCurveId build(World& w, const RoadCurvePlan& plan);
// Removes a curve: its tiles become clear land and the end tiles lose their pieces towards it.
void remove(World& w, RoadCurveId id);
// Cost of removing a curve.
Money removalCost(const RoadCurve& c);

// The curve whose tiles include `t` (not counting its end tiles).
RoadCurveId at(const World& w, TilePos t);
// Whether a curve's road surface overhangs `t`, a tile that isn't one of its own.
bool onVerge(const World& w, TilePos t);

// Where a road vehicle on `t`, entering through `entry` and leaving through `exit`, is on a
// curve: none when the tile isn't on one or the vehicle doesn't follow it (turning back).
struct Place {
    const RoadCurve* curve = nullptr;
    usize index = 0;
    bool forward = true; // from a towards b
    i32 begin() const { return forward ? curve->tileStart[index] : curve->tileStart[index + 1]; }
    i32 end() const { return forward ? curve->tileStart[index + 1] : curve->tileStart[index]; }
};
Place follow(const World& w, TilePos t, Dir entry, Dir exit);
// The side a road vehicle entering a curve's tile through `entry` leaves it by, following the
// curve (none off a curve, or entering it from the side).
std::optional<Dir> onwardSide(const World& w, TilePos t, Dir entry);
// How far a road vehicle drives across a tile: its share of the curve on a curve, else the
// length of the grid lane (see vehicle.hpp).
i32 pathLength(const World& w, TilePos t, Dir entry, Dir exit);
// The point `s` units along the centre line, and the direction it runs there (from a to b).
TPoint pointAt(const RoadCurve& c, i32 s, TDir* dir = nullptr);
// The highest speed (units per tick) a vehicle as fast as `maxSpeed` may take a curve at.
i32 speedLimit(const RoadCurve& c, i32 maxSpeed);

// Recomputes every curve's geometry and the tile index (after loading).
void rebuildDerived(World& w);

} // namespace roadcurves

} // namespace tp
