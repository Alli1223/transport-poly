#pragma once
// Free-form railway track in the style of Transport Fever: a graph of nodes joined by curved
// edges (cubic Bezier curves) that run at any angle, independent of the tile grid.
//
// Positions are fixed point (kTrackUnits per tile) and heights use the terrain's fine units, so
// every peer computes identical geometry. Each node has an axis: side 0 leaves along +dir and
// side 1 along -dir, and trains pass through a node from one side to the other. A side holding
// two edges is a switch. Building track shapes the terrain under it: a level bed, embankments
// where the ground is low and cuttings where it is high.

#include "core/money.hpp"
#include "core/types.hpp"
#include "sim/coords.hpp"
#include "sim/ids.hpp"
#include "sim/terraform.hpp"

#include <array>
#include <string>
#include <vector>

namespace tp {

class World;

struct TrackNodeTag {};
struct TrackEdgeTag {};
using TrackNodeId = StrongId<TrackNodeTag>;
using TrackEdgeId = StrongId<TrackEdgeTag>;

inline constexpr i32 kTrackUnits = 256;          // horizontal units per tile
inline constexpr i32 kDirOne = 4096;             // length of a unit direction
inline constexpr i32 kTrackMinRadius = 3 * 256;  // tightest curve radius
inline constexpr i32 kTrackMaxGrade = 5;         // steepest gradient: fine height units per tile
inline constexpr i32 kTrackMaxLength = 48 * 256; // longest single build
inline constexpr i32 kTrackPiece = 8 * 256;      // builds are split into edges at most this long
inline constexpr i32 kTrackBedHalfWidth = 176;   // ballast bed, units either side of the centre line
inline constexpr i32 kTrackJoinRadius = 16;      // an anchor this close to a node joins it
inline constexpr i32 kPlatformHalfWidth = 244;   // station platforms either side of the track
inline constexpr i32 kTrackSpacing = 300;        // closest two separate tracks may run, centre to centre
inline constexpr i32 kDepotStub = 384;           // length of the track into a depot
inline constexpr i64 kGradeOne = 65536;          // grade scale: fine height per unit of length

struct TPoint {
    i32 x = 0;
    i32 z = 0;
    friend constexpr bool operator==(TPoint, TPoint) = default;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(x, z);
    }
};

struct TDir {
    i32 x = kDirOne;
    i32 z = 0;
    constexpr TDir operator-() const { return {-x, -z}; }
    constexpr bool isZero() const { return x == 0 && z == 0; }
    friend constexpr bool operator==(TDir, TDir) = default;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(x, z);
    }
};

TDir normalised(i64 x, i64 z); // the zero vector stays zero
constexpr i64 dot(TDir a, TDir b) { return static_cast<i64>(a.x) * b.x + static_cast<i64>(a.z) * b.z; }
i64 distance(TPoint a, TPoint b);
constexpr TPoint offset(TPoint p, TDir d, i64 length) {
    return {static_cast<i32>(p.x + d.x * length / kDirOne), static_cast<i32>(p.z + d.z * length / kDirOne)};
}
constexpr TPoint tileCentre(TilePos t) { return {t.x * kTrackUnits + kTrackUnits / 2, t.y * kTrackUnits + kTrackUnits / 2}; }
constexpr TilePos tileAt(TPoint p) {
    return {p.x >= 0 ? p.x / kTrackUnits : (p.x - kTrackUnits + 1) / kTrackUnits,
            p.z >= 0 ? p.z / kTrackUnits : (p.z - kTrackUnits + 1) / kTrackUnits};
}

struct TrackNode {
    TPoint pos;
    i32 h = 0;      // rail height in fine terrain units
    TDir dir;       // axis: side 0 leaves along +dir, side 1 along -dir
    i64 grade = 0;  // height change along +dir, fine units per unit length times kGradeOne
    std::array<TrackEdgeId, 4> edges{}; // index side * 2 + slot
    u8 signals = 0;    // bit s: a path signal for trains leaving through side s (one bit only: one-way)

    TDir side(u8 s) const { return s == 0 ? dir : -dir; }
    int count(u8 s) const { return edges[s * 2u].valid() + edges[s * 2u + 1].valid(); }
    bool hasRoom(u8 s) const { return count(s) < 2; }
    bool empty() const { return count(0) + count(1) == 0; }

    template <class Ar>
    void serialize(Ar& ar) {
        ar(pos, h, dir, grade, edges, signals);
        if (ar.version == 13) {
            u8 unusedSignalKind = 0; // stored by save version 13 only
            ar(unusedSignalKind);
        }
    }
};

enum class TrackKind : u8 { Plain, Platform, Depot };

struct TrackSample {
    TPoint p;
    i32 h = 0; // rail height, fine units
    i32 s = 0; // distance along the edge, units
};

struct TrackEdge {
    TrackNodeId a, b;
    u8 sideA = 0; // the node sides the edge leaves through
    u8 sideB = 0;
    i32 handleA = 0; // Bezier handle lengths along the node sides' directions
    i32 handleB = 0;
    CompanyId owner;
    TrackKind kind = TrackKind::Plain;
    StationId station;
    DepotId depot;
    u32 serial = 0;
    VehicleId reservedBy; // the train holding this edge (trains only drive on track they hold)

    // Derived from the nodes when built or loaded (not saved).
    std::vector<TrackSample> samples;
    i32 length = 0;
    i32 minRadius = 0; // tightest curve, units (0 when straight)

    template <class Ar>
    void serialize(Ar& ar) {
        ar(a, b, sideA, sideB, handleA, handleB, owner, kind, station, depot, serial);
        if (ar.version == 0 || ar.version >= 14) ar(reservedBy);
    }
};

// An edge driven in one direction (forward = from node a to node b).
struct EdgeRun {
    TrackEdgeId edge;
    bool forward = true;
    friend bool operator==(const EdgeRun&, const EdgeRun&) = default;
    template <class Ar>
    void serialize(Ar& ar) {
        ar(edge, forward);
    }
};

struct TrackOptions {
    TrackKind kind = TrackKind::Plain;
    bool straight = false;          // no curve allowed (platforms, depots)
    bool level = false;             // flat from end to end (platforms)
    i32 halfWidth = kTrackBedHalfWidth;
};

// One end of a track to build.
struct TrackAnchor {
    TrackNodeId node; // join this node; if invalid, a node at `pos` is joined when there is one
    TPoint pos;
    TDir into{0, 0};  // direction from this end into the new track; zero lets the planner choose

    template <class Ar>
    void serialize(Ar& ar) {
        ar(node, pos, into);
    }
};

struct TrackPlan {
    bool ok = false;
    std::string error;
    Money cost = 0;       // everything, including earthworks
    Money earthworks = 0; // the terrain part of the cost
    i32 length = 0;       // units
    i32 minRadius = 0;    // units, 0 for straight track
    i32 gradePermille = 0; // steepest gradient along the track

    std::vector<TrackSample> samples; // the whole new track (for previews and the terrain)
    std::vector<FineChange> terrain;  // embankments, cuttings and the bed
    std::vector<TilePos> tiles;       // tiles under the bed
    TrackOptions options;

    // Resolved ends.
    TrackNodeId startNode, endNode; // existing nodes joined, if any
    u8 startSide = 0, endSide = 0;
    struct Piece {
        TPoint p0, p1, p2, p3;
        i32 h0 = 0, h1 = 0;
        i64 g0 = 0, g1 = 0; // grades along the direction of travel
        TDir d0, d3;        // travel directions at both ends
    };
    std::vector<Piece> pieces;
};

namespace track {

// Plans a track between two anchors without changing anything.
TrackPlan plan(const World& w, CompanyId company, const TrackAnchor& from, const TrackAnchor& to,
               const TrackOptions& options = {});
// Builds a plan made by plan() for the same world state; returns the new edges in order.
std::vector<TrackEdgeId> build(World& w, CompanyId company, const TrackPlan& plan);
// Removes an edge, its exclusive tiles and any nodes left without edges (and a platform's
// station tiles).
void remove(World& w, TrackEdgeId id);
i32 halfWidthOf(TrackKind kind);

// A straight, level station platform from `from` towards `to` (snapped onto the start's line).
TrackPlan planPlatform(const World& w, CompanyId company, const TrackAnchor& from, TPoint to);
// Builds a planned platform and adds it to a nearby station of the company (or a new one).
StationId buildPlatform(World& w, CompanyId company, const TrackPlan& plan);
// A depot at the open end (side) of a node.
TrackPlan planDepot(const World& w, CompanyId company, TrackNodeId node);
// Where a depot's building stands and which way its doors face, for a planned stub.
TPoint depotBuildingPos(const TrackPlan& plan);
TDir depotFacing(const TrackPlan& plan);

// Graph walking.
TrackNodeId runEnd(const World& w, EdgeRun r);   // the node a run leads to
TrackNodeId runStart(const World& w, EdgeRun r); // the node a run comes from
u8 runEndSide(const World& w, EdgeRun r);        // the side of runEnd the run arrives through
// Runs that continue from the end of `r` through its end node.
std::vector<EdgeRun> continuations(const World& w, EdgeRun r);
// Rail height and position `distance` units into a run.
TrackSample pointOn(const TrackEdge& e, bool forward, i32 distance);
// Whether a train may take an edge: free (or its own) and no switch sibling held by another.
bool reservable(const World& w, TrackEdgeId e, VehicleId train);
bool hasPlatform(const World& w, StationId s);

void computeSamples(const World& w, TrackEdge& e);
// Recomputes samples and the tile index (after loading).
void rebuildDerived(World& w);

// Tiles within `halfWidth` (+ half a tile) of the sampled centre line.
std::vector<TilePos> bedTiles(const std::vector<TrackSample>& samples, i32 halfWidth = kTrackBedHalfWidth);
// The nearest node within `radius` that has a free side (or any node, with anySide).
TrackNodeId nodeNear(const World& w, TPoint p, i32 radius, bool anySide = false);
// The nearest edge within `radius` of p, and the distance along it.
TrackEdgeId edgeNear(const World& w, TPoint p, i32 radius);
// Terrain height (fine units) at a point, following the tile triangulation.
i32 terrainAt(const World& w, TPoint p);
// The side of `node` an edge from it towards `into` would use (or -1 when none fits).
int sideFor(const TrackNode& node, TDir into);

} // namespace track

} // namespace tp
