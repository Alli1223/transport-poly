// Companies: the league table with the competition, graphs comparing every company month by month,
// and the detailed performance rating behind each company's score.

#include "client/app.hpp"
#include "client/ui/widgets.hpp"

#include <imgui.h>

#include "core/calendar.hpp"
#include "sim/league.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace tp {

namespace {

struct Series {
    std::string name;
    ImU32 colour;
    std::vector<double> values; // one per month, oldest first; NaN before the company existed
};

std::string shortMoney(double v) {
    const double a = std::abs(v);
    const char* sign = v < 0 ? "-" : "";
    if (a >= 1e9) return std::format("{}${:.1f}B", sign, a / 1e9);
    if (a >= 1e6) return std::format("{}${:.1f}M", sign, a / 1e6);
    if (a >= 1e3) return std::format("{}${:.0f}k", sign, a / 1e3);
    return std::format("{}${:.0f}", sign, a);
}

std::string monthLabel(const World& w, int monthsAgo) {
    static constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const CivilDate d = w.clock.date();
    int m = d.month - 1 - monthsAgo, y = d.year;
    while (m < 0) {
        m += 12;
        --y;
    }
    return std::format("{} {:02}", kMonths[m], y % 100);
}

// A line per company over the months, with a grid, axis labels, and the values under the pointer.
void lineChart(const World& w, const std::vector<Series>& series, usize months, bool money, ImVec2 size) {
    const float s = ui::scale();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##chart", size);
    const bool hovered = ImGui::IsItemHovered();
    const float left = 62.0f * s, bottom = 22.0f * s, top = 8.0f * s, right = 10.0f * s;
    const ImVec2 a(p0.x + left, p0.y + top), b(p0.x + size.x - right, p0.y + size.y - bottom);
    dl->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), ui::withAlpha(ui::col::kBar, 0.6f), 8.0f * s);
    double lo = DBL_MAX, hi = -DBL_MAX;
    for (const Series& se : series)
        for (const double v : se.values)
            if (!std::isnan(v)) {
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
    if (lo > hi || months < 2) {
        dl->AddText(ImVec2(a.x + 10.0f * s, a.y + 10.0f * s), ui::col::kTextMuted, "No history yet: the graphs fill in month by month.");
        return;
    }
    if (lo > 0.0) lo = 0.0;
    if (hi <= lo) hi = lo + 1.0;
    // Round the range to a tidy step.
    const double rawStep = (hi - lo) / 4.0;
    const double mag = std::pow(10.0, std::floor(std::log10(rawStep)));
    const double step = rawStep / mag > 5 ? 10 * mag : rawStep / mag > 2 ? 5 * mag : rawStep / mag > 1 ? 2 * mag : mag;
    lo = std::floor(lo / step) * step;
    hi = std::ceil(hi / step) * step;
    const auto X = [&](usize i) { return a.x + (b.x - a.x) * static_cast<float>(i) / static_cast<float>(months - 1); };
    const auto Y = [&](double v) { return b.y - (b.y - a.y) * static_cast<float>((v - lo) / (hi - lo)); };
    ImFont* font = ui::fonts().small ? ui::fonts().small : ImGui::GetFont();
    for (double v = lo; v <= hi + step * 0.5; v += step) {
        const float y = Y(v);
        dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), std::abs(v) < step * 0.01 ? ui::col::kBorderStrong : ui::withAlpha(ui::col::kBorderStrong, 0.4f));
        const std::string label = money ? shortMoney(v) : std::format("{:.0f}", v);
        const ImVec2 ts = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, label.c_str());
        dl->AddText(font, font->FontSize, ImVec2(a.x - ts.x - 6.0f * s, y - ts.y * 0.5f), ui::col::kTextMuted, label.c_str());
    }
    const usize every = months <= 13 ? 2 : months <= 25 ? 4 : 12;
    for (usize i = 0; i < months; i += every) {
        const std::string label = monthLabel(w, static_cast<int>(months - 1 - i));
        const ImVec2 ts = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, label.c_str());
        dl->AddText(font, font->FontSize, ImVec2(X(i) - ts.x * 0.5f, b.y + 4.0f * s), ui::col::kTextMuted, label.c_str());
    }
    for (const Series& se : series) {
        std::vector<ImVec2> line;
        for (usize i = 0; i < se.values.size(); ++i) {
            if (std::isnan(se.values[i])) {
                if (line.size() > 1) dl->AddPolyline(line.data(), static_cast<int>(line.size()), se.colour, 0, 2.5f * s);
                line.clear();
                continue;
            }
            line.push_back(ImVec2(X(i), Y(se.values[i])));
        }
        if (line.size() > 1) dl->AddPolyline(line.data(), static_cast<int>(line.size()), se.colour, 0, 2.5f * s);
        if (line.size() == 1) dl->AddCircleFilled(line[0], 3.0f * s, se.colour);
    }
    if (hovered) {
        const float mx = ImGui::GetIO().MousePos.x;
        const usize i = static_cast<usize>(std::clamp(std::lround((mx - a.x) / (b.x - a.x) * static_cast<float>(months - 1)), 0L,
                                                      static_cast<long>(months - 1)));
        dl->AddLine(ImVec2(X(i), a.y), ImVec2(X(i), b.y), ui::col::kText, 1.0f * s);
        std::vector<std::pair<double, const Series*>> at;
        for (const Series& se : series)
            if (i < se.values.size() && !std::isnan(se.values[i])) at.push_back({se.values[i], &se});
        std::ranges::sort(at, [](const auto& x, const auto& y) { return x.first > y.first; });
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(monthLabel(w, static_cast<int>(months - 1 - i)).c_str());
        for (const auto& [v, se] : at) {
            ImGui::ColorButton("##c", ImGui::ColorConvertU32ToFloat4(se->colour), ImGuiColorEditFlags_NoTooltip, ImVec2(10.0f * s, 10.0f * s));
            ImGui::SameLine();
            ImGui::Text("%s  %s", se->name.c_str(), money ? formatMoney(static_cast<Money>(v)).c_str() : std::format("{:.0f}", v).c_str());
        }
        ImGui::EndTooltip();
    }
}

ImU32 companyColour(const Company& c) { return ui::rgb(companyColourRgb(c.colour)); }

struct Competition {
    const char* name;
    Money value;
    i32 years;
};
constexpr Competition kCompetitions[] = {
    {"None", 0, 0},
    {"First to a company value of $1,000,000", 1'000'000, 0},
    {"First to a company value of $5,000,000", 5'000'000, 0},
    {"First to a company value of $20,000,000", 20'000'000, 0},
    {"Best rating after 5 years", 0, 5},
    {"Best rating after 10 years", 0, 10},
    {"Best rating after 20 years", 0, 20},
};

} // namespace

void App::applyCompetition(WorldSettings& ws) const {
    const Competition& c = kCompetitions[std::clamp(competition_, 0, static_cast<int>(std::size(kCompetitions)) - 1)];
    ws.goalValue = c.value;
    ws.goalYear = c.years ? ws.startYear + c.years : 0;
}

void App::competitionPicker() {
    ui::fieldLabel("Competition");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##competition", kCompetitions[std::clamp(competition_, 0, static_cast<int>(std::size(kCompetitions)) - 1)].name)) {
        for (int i = 0; i < static_cast<int>(std::size(kCompetitions)); ++i)
            if (ImGui::Selectable(kCompetitions[i].name, i == competition_)) competition_ = i;
        ImGui::EndCombo();
    }
}

void App::drawCompaniesWindow() {
    if (!showCompanies_) return;
    World& w = world();
    const float s = uiScale_;
    ui::PanelSpec spec;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    spec.pos = ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f - 330.0f * s, vp->WorkPos.y + 76.0f * s);
    spec.width = 660.0f;
    spec.accent = ui::col::kAccent;
    spec.subtitle = "How every company is doing";
    if (ui::beginPanel("###companies", "Companies", ui::Icon::Star, &showCompanies_, spec)) {
        static constexpr ui::Segment kTabs[] = {{"League", ui::Icon::Star}, {"Graphs", ui::Icon::Chart}, {"Rating", ui::Icon::Gauge}};
        if (const int tab = ui::segmented("##companytabs", kTabs, companiesTab_); tab >= 0) companiesTab_ = tab;
        ImGui::Dummy(ImVec2(0, 4.0f * s));
        // Live ratings, best first.
        struct Row {
            CompanyId id;
            const Company* c;
            league::Rating rating;
        };
        std::vector<Row> rows;
        for (const auto [id, c] : w.companies)
            if (!c.bankrupt) rows.push_back({id, &c, league::rate(w, id)});
        std::ranges::sort(rows, [](const Row& x, const Row& y) { return x.rating.total != y.rating.total ? x.rating.total > y.rating.total : x.id < y.id; });
        const std::vector<net::ClientInfo> people = players();

        if (companiesTab_ == 0) {
            // The competition, if this game is played for one.
            const Company* winner = w.companies.tryGet(w.competitionWinner);
            if (winner) {
                ui::iconText(ui::Icon::Star, ui::col::kAccent, std::format("{} won the competition!", winner->name).c_str(), ui::col::kAccent);
            } else if (w.settings.goalValue > 0 && !rows.empty()) {
                const Row* lead = &rows.front();
                Money best = rows.front().rating.values[static_cast<usize>(league::Measure::CompanyValue)];
                for (const Row& r : rows)
                    if (r.rating.values[static_cast<usize>(league::Measure::CompanyValue)] > best) {
                        best = r.rating.values[static_cast<usize>(league::Measure::CompanyValue)];
                        lead = &r;
                    }
                ui::iconText(ui::Icon::Star, ui::col::kAccent,
                             std::format("Competition: the first company worth {} wins", formatMoney(w.settings.goalValue)).c_str());
                ui::meterRow(std::format("Leader: {}", lead->c->name).c_str(), formatMoney(best).c_str(),
                             std::clamp(static_cast<float>(best) / static_cast<float>(w.settings.goalValue), 0.0f, 1.0f), companyColour(*lead->c));
            } else if (w.settings.goalYear > 0 && !rows.empty()) {
                const i32 left = w.settings.goalYear - w.clock.date().year;
                ui::iconText(ui::Icon::Star, ui::col::kAccent,
                             std::format("Competition: the best rating in {} wins ({})", w.settings.goalYear,
                                         w.competitionOver ? std::string("decided")
                                                           : left > 0 ? std::format("{} year{} to go", left, left == 1 ? "" : "s") : std::string("this year"))
                                 .c_str());
            } else {
                ui::FontScope f(ui::fonts().small);
                ui::mutedText("No competition in this game: choose one when starting or hosting a game.");
            }
            ImGui::Dummy(ImVec2(0, 4.0f * s));
            if (ImGui::BeginTable("##league", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
                ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 26.0f * s);
                ImGui::TableSetupColumn("Company", ImGuiTableColumnFlags_WidthStretch, 3.0f);
                ImGui::TableSetupColumn("Rating", ImGuiTableColumnFlags_WidthStretch, 2.2f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.3f);
                ImGui::TableSetupColumn("Profit / year", ImGuiTableColumnFlags_WidthStretch, 1.5f);
                ImGui::TableSetupColumn("Vehicles", ImGuiTableColumnFlags_WidthFixed, 60.0f * s);
                ImGui::TableHeadersRow();
                for (usize i = 0; i < rows.size(); ++i) {
                    const Row& r = rows[i];
                    ImGui::TableNextRow(0, 40.0f * s);
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", i + 1);
                    ImGui::TableNextColumn();
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + 3.0f * s), ImVec2(p.x + 6.0f * s, p.y + 33.0f * s), companyColour(*r.c), 3.0f * s);
                    ImGui::SetCursorScreenPos(ImVec2(p.x + 12.0f * s, p.y));
                    ImGui::BeginGroup();
                    ImGui::Text("%s%s", r.c->name.c_str(), r.id == session_->localCompany() ? "  (you)" : "");
                    std::string who;
                    for (const auto& pl : people)
                        if (pl.company == r.id) who += (who.empty() ? "" : ", ") + pl.name;
                    if (r.c->aiControlled) who = "Computer";
                    {
                        ui::FontScope f(ui::fonts().small);
                        ui::mutedText(std::format("{}{}{}", league::title(r.rating.total), who.empty() ? "" : "  |  ", who).c_str());
                    }
                    ImGui::EndGroup();
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", r.rating.total);
                    ImGui::SameLine();
                    ui::meter(static_cast<float>(r.rating.total) / 1000.0f, companyColour(*r.c));
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(shortMoney(static_cast<double>(r.rating.values[static_cast<usize>(league::Measure::CompanyValue)])).c_str());
                    ImGui::TableNextColumn();
                    const i64 profit = r.rating.values[static_cast<usize>(league::Measure::OperatingProfit)];
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(profit < 0 ? ui::col::kRed : ui::col::kText), "%s", shortMoney(static_cast<double>(profit)).c_str());
                    ImGui::TableNextColumn();
                    usize fleet = 0;
                    for (const auto [vid, v] : w.vehicles)
                        if (v.owner == r.id) ++fleet;
                    ImGui::Text("%zu", fleet);
                }
                ImGui::EndTable();
            }
        } else if (companiesTab_ == 1) {
            static constexpr const char* kMetrics[] = {"Company value", "Operating profit per month", "Income per month", "Performance rating"};
            ImGui::SetNextItemWidth(260.0f * s);
            ImGui::Combo("##metric", &graphMetric_, kMetrics, 4);
            ImGui::SameLine();
            static constexpr const char* kSpans[] = {"1 year", "2 years", "5 years", "10 years"};
            static constexpr usize kSpanMonths[] = {12, 24, 60, 120};
            ImGui::SetNextItemWidth(120.0f * s);
            ImGui::Combo("##span", &graphSpan_, kSpans, 4);
            const usize span = kSpanMonths[std::clamp(graphSpan_, 0, 3)];
            std::vector<Series> series;
            usize longest = 0;
            for (const Row& r : rows) {
                Series se{r.c->name, companyColour(*r.c), {}};
                std::vector<double> all;
                if (graphMetric_ == 0)
                    for (const Money v : r.c->valueHistory) all.push_back(static_cast<double>(v));
                else if (graphMetric_ == 3)
                    for (const i16 v : r.c->ratingHistory) all.push_back(v);
                else
                    for (const MonthLedger& m : r.c->monthlyHistory) {
                        Money total = 0;
                        for (usize e = 0; e < kExpenseCount; ++e) {
                            const auto kind = static_cast<Expense>(e);
                            const bool counts = graphMetric_ == 2 ? isIncome(kind)
                                                                  : kind != Expense::Construction && kind != Expense::NewVehicles && kind != Expense::LoanInterest;
                            if (counts) total += m.amounts[e];
                        }
                        all.push_back(static_cast<double>(total));
                    }
                longest = std::max(longest, std::min(all.size(), span));
                se.values = std::move(all);
                series.push_back(std::move(se));
            }
            // Align every company's months at the latest one.
            for (Series& se : series) {
                std::vector<double> v(longest, std::nan(""));
                const usize n = std::min(se.values.size(), longest);
                for (usize i = 0; i < n; ++i) v[longest - n + i] = se.values[se.values.size() - n + i];
                se.values = std::move(v);
            }
            lineChart(w, series, longest, graphMetric_ != 3, ImVec2(ImGui::GetContentRegionAvail().x, 300.0f * s));
            for (usize i = 0; i < series.size(); ++i) {
                if (i) ImGui::SameLine();
                ui::legendItem(series[i].colour, series[i].name.c_str());
            }
        } else {
            // The detailed rating of one company.
            if (!w.companies.contains(ratingCompany_)) ratingCompany_ = w.companies.contains(session_->localCompany()) ? session_->localCompany() : (rows.empty() ? CompanyId{} : rows.front().id);
            const Company* c = w.companies.tryGet(ratingCompany_);
            if (ImGui::BeginCombo("Company", c ? c->name.c_str() : "")) {
                for (const Row& r : rows)
                    if (ImGui::Selectable(r.c->name.c_str(), r.id == ratingCompany_)) ratingCompany_ = r.id;
                ImGui::EndCombo();
            }
            if (c) {
                const league::Rating rating = league::rate(w, ratingCompany_);
                ui::sectionHeader(std::format("{} - {} of 1000, {}", c->name, rating.total, league::title(rating.total)).c_str());
                for (usize m = 0; m < league::kMeasureCount; ++m) {
                    const auto measure = static_cast<league::Measure>(m);
                    const league::MeasureInfo& info = league::measureInfo(measure);
                    const bool money = measure == league::Measure::LowestProfit || measure == league::Measure::Income ||
                                       measure == league::Measure::OperatingProfit || measure == league::Measure::Cash ||
                                       measure == league::Measure::Loan || measure == league::Measure::CompanyValue;
                    const auto show = [&](i64 v) { return money ? formatMoney(v) : std::format("{}", v); };
                    const std::string value = measure == league::Measure::Loan ? std::format("{} (none is best)", show(rating.values[m]))
                                                                                : std::format("{} of {}", show(rating.values[m]), show(info.target));
                    ui::meterRow(std::format("{}  ({} / {})", info.name, rating.points[m], info.maxPoints).c_str(), value.c_str(),
                                 static_cast<float>(rating.points[m]) / static_cast<float>(info.maxPoints), companyColour(*c));
                }
                ui::FontScope f(ui::fonts().small);
                ui::mutedText("Ratings are recorded at the start of each month; the league leader is announced in the news.");
            }
        }
    }
    ui::endPanel();
}

} // namespace tp
