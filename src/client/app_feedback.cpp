// Passenger feedback (a feed of complaints and praise, and satisfaction scores) and subsidies
// (double pay for a year to the first company to start a new service).

#include "client/app.hpp"
#include "client/ui/widgets.hpp"

#include <imgui.h>

#include "core/calendar.hpp"
#include "core/hash.hpp"
#include "core/names.hpp"

#include <cctype>
#include <format>
#include <string>

namespace tp {

namespace {

std::string dateOf(const World& w, i64 tick) { return formatDate(w.clock.startDay + static_cast<i32>(tick / kTicksPerDay)); }

ImU32 scoreColour(float t) {
    return t < 0.5f ? ui::mix(ui::col::kRed, ui::col::kAccent, t * 2.0f) : ui::mix(ui::col::kAccent, ui::col::kGreen, (t - 0.5f) * 2.0f);
}

std::string capitalised(std::string s) {
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

// What the passenger said, in one of a few ways of putting it.
std::string feedbackText(const World& w, const Feedback& f) {
    const auto station = [&](StationId id) {
        const Station* s = w.stations.tryGet(id);
        return s ? s->name : std::string("the stop");
    };
    const std::string at = station(f.station), from = station(f.from);
    const std::string vehicle = f.unit ? std::format("{} {}", vehicleTypeName(static_cast<VehicleType>(f.vehicleType)), f.unit)
                                       : std::string("the service");
    const u32 m = f.minutes;
    switch (mix32(f.nameSeed ^ static_cast<u32>(f.tick)) % 3) {
    case 0:
        switch (f.kind) {
        case FeedbackKind::LongWait: return std::format("Waited {} minutes at {} for {}. Not good enough.", m, at, vehicle);
        case FeedbackKind::GaveUp: return std::format("Gave up after {} minutes at {} and got a lift instead.", m, at);
        case FeedbackKind::StationFull: return std::format("{} was so packed I couldn't even get in.", at);
        case FeedbackKind::Crowded: return std::format("Squeezed onto {} at {} like a sardine.", vehicle, at);
        case FeedbackKind::SlowRide: return std::format("{} minutes from {} to {} on {}? I could have walked.", m, from, at, vehicle);
        case FeedbackKind::ShortWait: return std::format("Barely waited at {}: {} came right away.", at, vehicle);
        case FeedbackKind::QuickRide: return std::format("Quick trip from {} to {} on {}. Lovely!", from, at, vehicle);
        default: break;
        }
        break;
    case 1:
        switch (f.kind) {
        case FeedbackKind::LongWait: return std::format("{} minutes at {} before {} finally turned up!", m, at, vehicle);
        case FeedbackKind::GaveUp: return std::format("Nothing came to {} for {} minutes, so I went another way.", at, m);
        case FeedbackKind::StationFull: return std::format("No room to stand at {}. More services, please!", at);
        case FeedbackKind::Crowded: return std::format("Standing room only on {} again.", vehicle);
        case FeedbackKind::SlowRide: return std::format("{} crawled all the way from {} to {}.", vehicle, from, at);
        case FeedbackKind::ShortWait: return std::format("Turned up at {} and {} was already there. Perfect.", at, vehicle);
        case FeedbackKind::QuickRide: return std::format("Only {} minutes from {} to {}. Brilliant service.", m, from, at);
        default: break;
        }
        break;
    default:
        switch (f.kind) {
        case FeedbackKind::LongWait: return std::format("Does anything run on time from {}? I waited {} minutes.", at, m);
        case FeedbackKind::GaveUp: return std::format("{} minutes at {} and still no sign of anything. I'm done.", m, at);
        case FeedbackKind::StationFull: return std::format("Turned away from {}: completely full.", at);
        case FeedbackKind::Crowded: return std::format("{} from {} was packed. Run more of them!", vehicle, at);
        case FeedbackKind::SlowRide: return std::format("Far too slow: {} minutes from {} to {}.", m, from, at);
        case FeedbackKind::ShortWait: return std::format("No waiting around at {} today. Thanks!", at);
        case FeedbackKind::QuickRide: return std::format("{} got me from {} to {} in no time.", vehicle, from, at);
        default: break;
        }
        break;
    }
    return {};
}

std::string feedbackAuthor(const World& w, const Feedback& f) {
    const Town* town = w.towns.tryGet(f.town);
    const std::string name = generatePersonFirstName(f.nameSeed) + " " + generatePersonSurname(f.nameSeed ^ 0x9E3779B9u);
    return town ? std::format("{} of {}, {}", name, town->name, dateOf(w, f.tick)) : std::format("{}, {}", name, dateOf(w, f.tick));
}

} // namespace

void App::satisfactionMeter(const char* label, i64 sum, i64 count) {
    if (count <= 0) {
        ui::statRow(label, "Not rated yet", ui::col::kTextMuted);
        return;
    }
    const i64 score = sum / count;
    const float t = static_cast<float>(score) / 100.0f;
    ui::meterRow(label, std::format("{}% ({} ride{})", score, count, count == 1 ? "" : "s").c_str(), t, scoreColour(t));
    ui::itemTooltip({"Passenger satisfaction", "How passengers rated their rides this month and last: a short wait, "
                                               "room to sit and a quick ride score well."});
}

void App::drawFeedbackEntries(const std::function<bool(const Feedback&)>& match, usize max) {
    const World& w = world();
    usize shown = 0;
    for (auto it = w.feedback.rbegin(); it != w.feedback.rend() && shown < max; ++it) {
        const Feedback& f = *it;
        if (!match(f)) continue;
        ++shown;
        ImGui::PushID(static_cast<int>(shown));
        ImGui::BeginGroup();
        const bool praise = isPraise(f.kind);
        ui::iconInline(praise ? ui::Icon::Star : ui::Icon::Warning, praise ? ui::col::kGreen : ui::col::kOrange);
        ImGui::SameLine();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(std::format("\"{}\"", feedbackText(w, f)).c_str());
        ImGui::PopTextWrapPos();
        {
            ui::FontScope small(ui::fonts().small);
            ui::mutedText(feedbackAuthor(w, f).c_str());
        }
        ImGui::EndGroup();
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked()) {
            if (w.vehicles.contains(f.vehicle) && w.vehicles[f.vehicle].unitNumber == f.unit && f.unit) selVehicle_ = f.vehicle;
            if (const Station* s = w.stations.tryGet(f.station)) {
                selStation_ = f.station;
                focusOn(s->sign);
            }
        }
        ImGui::Dummy(ImVec2(0, 2.0f * uiScale_));
        ImGui::PopID();
    }
    if (!shown) ui::mutedText("No comments yet.");
}

void App::drawFeedbackWindow() {
    if (!showFeedback_) return;
    const World& w = world();
    const float s = uiScale_;
    if (fleetCompany_ == -2) fleetCompany_ = session_->localCompany().valid() ? session_->localCompany().value : -1;
    ui::PanelSpec spec;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    spec.pos = ImVec2(vp->WorkPos.x + 20.0f * s, vp->WorkPos.y + 80.0f * s);
    spec.width = 480.0f;
    spec.height = 520.0f;
    spec.accent = ui::col::kTeal;
    spec.subtitle = "What passengers say";
    if (ui::beginPanel("###feedbackPanel", "Passenger feedback", ui::Icon::Chat, &showFeedback_, spec)) {
        const Company* selected = fleetCompany_ >= 0 ? w.companies.tryGet(CompanyId{static_cast<u8>(fleetCompany_)}) : nullptr;
        if (ImGui::BeginCombo("Company", selected ? selected->name.c_str() : "All companies")) {
            if (ImGui::Selectable("All companies", fleetCompany_ < 0)) fleetCompany_ = -1;
            for (const auto [id, co] : w.companies)
                if (ImGui::Selectable(co.name.c_str(), fleetCompany_ == id.value)) fleetCompany_ = id.value;
            ImGui::EndCombo();
        }
        const auto ofCompany = [&](CompanyId c) { return fleetCompany_ < 0 || c.value == fleetCompany_; };
        i64 sum = 0, count = 0;
        for (const auto [id, v] : w.vehicles)
            if (ofCompany(v.owner)) {
                sum += v.satisfactionSum[0] + v.satisfactionSum[1];
                count += v.satisfactionCount[0] + v.satisfactionCount[1];
            }
        satisfactionMeter("Satisfaction", sum, count);
        int complaints = 0, praise = 0;
        for (const Feedback& f : w.feedback)
            if (ofCompany(f.company)) ++(isPraise(f.kind) ? praise : complaints);
        ui::statRow("Recent comments", std::format("{} complaints, {} praise", complaints, praise).c_str());
        static constexpr ui::Segment kFilters[] = {{"All", ui::Icon::None, nullptr},
                                                   {"Complaints", ui::Icon::Warning, nullptr},
                                                   {"Praise", ui::Icon::Star, nullptr}};
        if (const int clicked = ui::segmented("##feedbackFilter", kFilters, feedbackFilter_); clicked >= 0) feedbackFilter_ = clicked;
        ImGui::Dummy(ImVec2(0, 2.0f * s));
        ImGui::BeginChild("##feedbackList", ImVec2(0, 0), ImGuiChildFlags_Borders);
        drawFeedbackEntries([&](const Feedback& f) {
            return ofCompany(f.company) && (feedbackFilter_ == 0 || (feedbackFilter_ == 2) == isPraise(f.kind));
        }, kMaxFeedback);
        ImGui::EndChild();
    }
    ui::endPanel();
}

void App::drawSubsidiesWindow() {
    if (!showSubsidies_) return;
    const World& w = world();
    const float s = uiScale_;
    ui::PanelSpec spec;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    spec.pos = ImVec2(vp->WorkPos.x + vp->WorkSize.x - 520.0f * s, vp->WorkPos.y + 80.0f * s);
    spec.width = 500.0f;
    spec.accent = ui::col::kAccent;
    spec.subtitle = "Double pay for a year";
    if (ui::beginPanel("###subsidiesPanel", "Subsidies", ui::Icon::Spark, &showSubsidies_, spec)) {
        {
            ui::FontScope small(ui::fonts().small);
            ui::mutedText("The first company to carry these from one end to the other is paid double");
            ui::mutedText("for them on that route for a year. Passengers must travel between the two towns.");
        }
        const auto row = [&](const Subsidy& sub, usize index, const std::string& status, ImU32 colour) {
            ImGui::PushID(static_cast<int>(index));
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(capitalised(subsidies::describe(w, sub)).c_str());
            ImGui::PopTextWrapPos();
            ImGui::PushStyleColor(ImGuiCol_Text, ui::vec(colour));
            {
                ui::FontScope small(ui::fonts().small);
                ImGui::TextUnformatted(status.c_str());
            }
            ImGui::PopStyleColor();
            if (ui::button("From", ui::Icon::Target, ui::Btn::Ghost)) focusOn(subsidies::sourceTile(w, sub));
            ImGui::SameLine();
            if (ui::button("To", ui::Icon::Target, ui::Btn::Ghost)) focusOn(subsidies::destinationTile(w, sub));
            ImGui::Dummy(ImVec2(0, 2.0f * s));
            ImGui::PopID();
        };
        ui::sectionHeader("On offer");
        bool any = false;
        for (usize i = 0; i < w.subsidies.size(); ++i)
            if (const Subsidy& sub = w.subsidies[i]; !sub.awarded()) {
                any = true;
                row(sub, i, std::format("Offer open until {}", dateOf(w, sub.untilTick)), ui::col::kTextMuted);
            }
        if (!any) ui::emptyState(ui::Icon::Spark, "No subsidies on offer right now");
        ui::sectionHeader("Awarded");
        any = false;
        for (usize i = 0; i < w.subsidies.size(); ++i)
            if (const Subsidy& sub = w.subsidies[i]; sub.awarded()) {
                any = true;
                const Company* c = w.companies.tryGet(sub.awardedTo);
                const bool mine = sub.awardedTo == session_->localCompany();
                row(sub, i, std::format("{}: double pay until {}", c ? c->name : std::string("?"), dateOf(w, sub.untilTick)),
                    mine ? ui::col::kGreen : ui::col::kTextMuted);
            }
        if (!any) ui::mutedText("None yet.");
    }
    ui::endPanel();
}

} // namespace tp
