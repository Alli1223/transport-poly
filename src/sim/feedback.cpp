#include "sim/feedback.hpp"

#include "core/hash.hpp"
#include "sim/world.hpp"

#include <algorithm>

namespace tp::feedback {

namespace {

constexpr u32 kSample = 4;                     // one notable experience in four is told
constexpr i64 kGapTicks = 2 * kTicksPerHour;   // between comments of one kind about a company

void post(World& w, const Person& p, FeedbackKind kind, CompanyId company, StationId station, StationId from,
          VehicleId vehicle, i64 ticks) {
    Company* c = w.companies.tryGet(company);
    if (!c || mix32(p.serial * 0x2545F491u ^ static_cast<u32>(w.clock.tick) * 0x9E3779B9u) % kSample != 0) return;
    i64& last = c->feedbackTicks[static_cast<usize>(kind)]; // tick + 1 of the last one, 0 never
    if (last && w.clock.tick + 1 - last < kGapTicks) return;
    last = w.clock.tick + 1;
    Feedback f;
    f.tick = w.clock.tick;
    f.company = company;
    f.kind = kind;
    f.nameSeed = p.nameSeed;
    f.town = p.town;
    f.station = station;
    f.from = from;
    f.vehicle = vehicle;
    if (const Vehicle* v = w.vehicles.tryGet(vehicle)) {
        f.unit = v->unitNumber;
        f.vehicleType = static_cast<u8>(v->info().type);
    }
    f.minutes = static_cast<u16>(std::clamp<i64>(ticks / kTicksPerMinute, 0, 0xFFFF));
    w.feedback.push_back(f);
    while (w.feedback.size() > kMaxFeedback) w.feedback.pop_front();
}

} // namespace

i32 waitScore(i64 waitedTicks) {
    const i64 minutes = waitedTicks / kTicksPerMinute;
    if (minutes <= 5) return 100;
    if (minutes >= 60) return 0;
    return static_cast<i32>(100 - (minutes - 5) * 100 / 55);
}

i32 rideScore(i64 tiles, i64 rideTicks) {
    // Against the time the fares expect for the distance (see passengers::fare).
    const i64 percent = rideTicks * 100 / (std::max<i64>(tiles, 1) * 14);
    if (percent <= 100) return 100;
    if (percent >= 300) return 0;
    return static_cast<i32>(100 - (percent - 100) / 2);
}

i32 satisfaction(const Vehicle& v) {
    const i64 rated = v.satisfactionCount[0] + v.satisfactionCount[1];
    return rated ? static_cast<i32>((v.satisfactionSum[0] + v.satisfactionSum[1]) / rated) : -1;
}

void boarded(World& w, PersonId person, VehicleId vehicle, StationId station, i64 waitedTicks) {
    Person& p = w.people[person];
    const Vehicle& v = w.vehicles[vehicle];
    const usize capacity = std::max<usize>(1, v.capacity());
    const bool crowded = capacity >= 20 && v.passengers.size() * 10 >= capacity * 9;
    p.boardScore = static_cast<u8>(std::max(0, waitScore(waitedTicks) - (crowded ? 25 : 0)));
    const i64 minutes = waitedTicks / kTicksPerMinute;
    if (minutes >= 45) post(w, p, FeedbackKind::LongWait, v.owner, station, {}, vehicle, waitedTicks);
    else if (crowded) post(w, p, FeedbackKind::Crowded, v.owner, station, {}, vehicle, waitedTicks);
    else if (minutes <= 2) post(w, p, FeedbackKind::ShortWait, v.owner, station, {}, vehicle, waitedTicks);
}

void alighted(World& w, PersonId person, VehicleId vehicle, StationId from, StationId at, i64 tiles, i64 rideTicks) {
    const Person& p = w.people[person];
    Vehicle& v = w.vehicles[vehicle];
    const i32 ride = rideScore(tiles, rideTicks);
    v.satisfactionSum[0] += (p.boardScore + ride) / 2;
    ++v.satisfactionCount[0];
    if (ride <= 20 && tiles >= 4) post(w, p, FeedbackKind::SlowRide, v.owner, at, from, vehicle, rideTicks);
    else if (ride >= 100 && p.boardScore >= 90 && tiles >= 12)
        post(w, p, FeedbackKind::QuickRide, v.owner, at, from, vehicle, rideTicks);
}

void gaveUp(World& w, PersonId person, StationId station, i64 waitedTicks) {
    if (const Station* s = w.stations.tryGet(station))
        post(w, w.people[person], FeedbackKind::GaveUp, s->owner, station, {}, {}, waitedTicks);
}

void stationFull(World& w, PersonId person, StationId station) {
    if (const Station* s = w.stations.tryGet(station))
        post(w, w.people[person], FeedbackKind::StationFull, s->owner, station, {}, {}, 0);
}

void monthly(World& w) {
    for (auto [id, v] : w.vehicles) {
        v.satisfactionSum = {0, v.satisfactionSum[0]};
        v.satisfactionCount = {0, v.satisfactionCount[0]};
    }
}

} // namespace tp::feedback
