#include "test.hpp"

#include <chrono>
#include <cstdio>
#include <exception>
#include <string_view>

namespace tp::test {
namespace {
int g_failures = 0;
const char* g_current = "";
} // namespace

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

void reportFailure(const char* expression, const std::string& detail, std::source_location where) {
    ++g_failures;
    std::fprintf(stderr, "  FAILED [%s] %s:%u: %s%s%s\n", g_current, where.file_name(),
                 static_cast<unsigned>(where.line()), expression, detail.empty() ? "" : "  -- ",
                 detail.c_str());
}

} // namespace tp::test

int main(int argc, char** argv) {
    using namespace tp::test;
    const std::string_view filter = argc > 1 ? argv[1] : "";
    int run = 0;
    int failedTests = 0;
    const auto start = std::chrono::steady_clock::now();
    for (const TestCase& tc : registry()) {
        if (!filter.empty() && std::string_view(tc.name).find(filter) == std::string_view::npos) continue;
        ++run;
        g_current = tc.name;
        const int before = g_failures;
        try {
            tc.fn();
        } catch (const RequireFailed&) {
        } catch (const std::exception& e) {
            reportFailure("unexpected exception", e.what(), std::source_location::current());
        }
        if (g_failures != before) {
            ++failedTests;
            std::fprintf(stderr, "FAIL %s\n", tc.name);
        }
    }
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("%d test(s) run, %d failed (%.1f ms)\n", run, failedTests, ms);
    return failedTests == 0 ? 0 : 1;
}
