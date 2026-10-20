#include "test.hpp"

#include "core/serialize.hpp"
#include "sim/commands.hpp"
#include "sim/league.hpp"
#include "sim/world.hpp"

#include <algorithm>

using namespace tp;

namespace {

CompanyId found(World& w, const char* name, u8 colour) {
    Command c;
    c.payload = CmdCreateCompany{name, colour};
    return CompanyId(static_cast<u8>(executeCommand(w, c).createdId));
}

// Runs until the next month starts.
void nextMonth(World& w) {
    const auto month = w.clock.date().month;
    while (w.clock.date().month == month) w.tick();
}

bool newsMentions(const World& w, const std::string& text) {
    return std::ranges::any_of(w.news, [&](const NewsItem& n) { return n.text.find(text) != std::string::npos; });
}

} // namespace

TEST_CASE("league: company value and a rating out of 1000") {
    World w;
    w.activateChunk({0, 0});
    const CompanyId a = found(w, "Alpha", 0);
    const Company& co = w.companies[a];
    CHECK_EQ(league::companyValue(w, a), co.money - co.loan);
    const league::Rating r = league::rate(w, a);
    CHECK(r.total >= 0);
    CHECK(r.total <= 1000);
    i32 sum = 0, most = 0;
    for (usize m = 0; m < league::kMeasureCount; ++m) {
        sum += r.points[m];
        most += league::measureInfo(static_cast<league::Measure>(m)).maxPoints;
    }
    CHECK_EQ(sum, r.total);
    CHECK_EQ(most, 1000);
    // More cash and a smaller loan rate higher.
    w.companies[a].money += 5'000'000;
    CHECK(league::rate(w, a).total > r.total);
    CHECK_EQ(league::title(0), std::string_view("Engineer"));
    CHECK_EQ(league::title(1000), std::string_view("Tycoon"));
}

TEST_CASE("league: monthly history, a new leader in the news, and saves") {
    World w;
    w.activateChunk({0, 0});
    const CompanyId a = found(w, "Alpha", 0);
    const CompanyId b = found(w, "Beta", 1);
    nextMonth(w);
    CHECK_EQ(w.companies[a].valueHistory.size(), 1u);
    CHECK_EQ(w.companies[a].ratingHistory.size(), 1u);
    CHECK_EQ(w.companies[a].rating, league::rate(w, a).total);
    // Beta pulls ahead: the news says so.
    w.companies[b].money += 20'000'000;
    nextMonth(w);
    CHECK(w.companies[b].rating > w.companies[a].rating);
    CHECK_EQ(w.companies[b].valueHistory.size(), 2u);
    if (w.companies[b].ratingHistory.front() <= w.companies[a].ratingHistory.front()) CHECK(newsMentions(w, "Beta now leads the league"));

    World copy;
    fromBytes(toBytes(w, kSaveVersion), copy, kSaveVersion);
    CHECK(copy.companies[b].valueHistory == w.companies[b].valueHistory);
    CHECK(copy.companies[b].ratingHistory == w.companies[b].ratingHistory);
    CHECK_EQ(copy.stateHash(), w.stateHash());
}

TEST_CASE("league: the first company to the target value wins the race") {
    WorldSettings s;
    s.goalValue = 3'000'000;
    World w(s);
    w.activateChunk({0, 0});
    const CompanyId a = found(w, "Alpha", 0);
    found(w, "Beta", 1);
    nextMonth(w);
    CHECK(!w.competitionWinner.valid());
    w.companies[a].money += 4'000'000;
    nextMonth(w);
    CHECK_EQ(w.competitionWinner, a);
    CHECK(w.competitionOver);
    CHECK(newsMentions(w, "Alpha wins the race"));
    // Decided once only.
    nextMonth(w);
    CHECK_EQ(std::ranges::count_if(w.news, [](const NewsItem& n) { return n.text.find("wins the race") != std::string::npos; }), 1);

    World copy;
    fromBytes(toBytes(w, kSaveVersion), copy, kSaveVersion);
    CHECK_EQ(copy.competitionWinner, a);
    CHECK(copy.settings.goalValue == 3'000'000);
}

TEST_CASE("league: the best rated company wins when the competition year comes") {
    WorldSettings s;
    s.goalYear = s.startYear + 1;
    World w(s);
    w.activateChunk({0, 0});
    found(w, "Alpha", 0);
    const CompanyId b = found(w, "Beta", 1);
    w.companies[b].money += 3'000'000;
    while (w.clock.date().year < s.goalYear) nextMonth(w);
    nextMonth(w); // the first month of the year has been rated
    CHECK_EQ(w.competitionWinner, b);
    CHECK(newsMentions(w, "Beta wins the competition"));
}
