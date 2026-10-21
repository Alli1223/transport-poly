#pragma once
// Passenger feedback: every passenger rates each ride on the wait, the crowding and the speed,
// and some say so. Vehicles keep the month's scores (lines and companies add theirs up) and the
// world keeps a feed of the most telling complaints and praise.

#include "core/types.hpp"
#include "sim/ids.hpp"

#include <array>
#include <string_view>

namespace tp {

class World;
struct Vehicle;

enum class FeedbackKind : u8 {
    // Complaints
    LongWait,    // waited a long time before boarding
    GaveUp,      // gave up waiting and found another way
    StationFull, // the station was too crowded to wait at
    Crowded,     // squeezed into a nearly full vehicle
    SlowRide,    // a ride far slower than the distance deserves
    // Praise
    ShortWait, // a vehicle came almost at once
    QuickRide, // a quick ride after a short wait
    Count
};
inline constexpr usize kFeedbackKinds = static_cast<usize>(FeedbackKind::Count);
constexpr bool isPraise(FeedbackKind k) { return k >= FeedbackKind::ShortWait; }

struct Feedback {
    i64 tick = 0;
    CompanyId company;
    FeedbackKind kind = FeedbackKind::LongWait;
    u32 nameSeed = 0; // the passenger's name
    TownId town;      // where they live
    StationId station; // where it happened: the waiting or alighting station
    StationId from;    // rides: where they boarded
    VehicleId vehicle;
    u16 unit = 0; // the vehicle's unit number at the time
    u8 vehicleType = 0;
    u16 minutes = 0;

    template <class Ar>
    void serialize(Ar& ar) {
        ar(tick, company, kind, nameSeed, town, station, from, vehicle, unit, vehicleType, minutes);
    }
};

// Most comments kept in the world's feed.
inline constexpr usize kMaxFeedback = 200;

namespace feedback {

// Scores out of 100.
i32 waitScore(i64 waitedTicks);
i32 rideScore(i64 tiles, i64 rideTicks);
// A vehicle's satisfaction over this month and last, out of 100 (-1: nobody has rated it yet).
i32 satisfaction(const Vehicle& v);

// A waiting passenger boards: rate the wait and the crowding.
void boarded(World& w, PersonId person, VehicleId vehicle, StationId station, i64 waitedTicks);
// A passenger leaves a vehicle: rate the ride and add the leg's score to the vehicle.
void alighted(World& w, PersonId person, VehicleId vehicle, StationId from, StationId at, i64 tiles, i64 rideTicks);
void gaveUp(World& w, PersonId person, StationId station, i64 waitedTicks);
void stationFull(World& w, PersonId person, StationId station);
// Starts a new month of vehicle scores.
void monthly(World& w);

} // namespace feedback

} // namespace tp
