#pragma once
// Minimal self-registering unit test framework.

#include <concepts>
#include <format>
#include <source_location>
#include <string>
#include <vector>

namespace tp::test {

struct TestCase {
    const char* name;
    void (*fn)();
};

std::vector<TestCase>& registry();

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

struct RequireFailed {};

void reportFailure(const char* expression, const std::string& detail, std::source_location where);

template <class A, class B>
std::string describe(const A& a, const B& b) {
    if constexpr (std::formattable<A, char> && std::formattable<B, char>) {
        return std::format("{} vs {}", a, b);
    } else {
        return {};
    }
}

} // namespace tp::test

#define TP_TEST_CONCAT2(a, b) a##b
#define TP_TEST_CONCAT(a, b) TP_TEST_CONCAT2(a, b)

#define TEST_CASE(name)                                                                                      \
    static void TP_TEST_CONCAT(tp_test_fn_, __LINE__)();                                                     \
    static const ::tp::test::Registrar TP_TEST_CONCAT(tp_test_reg_, __LINE__){                               \
        name, &TP_TEST_CONCAT(tp_test_fn_, __LINE__)};                                                       \
    static void TP_TEST_CONCAT(tp_test_fn_, __LINE__)()

#define CHECK(expr)                                                                                          \
    do {                                                                                                     \
        if (!(expr)) ::tp::test::reportFailure(#expr, {}, std::source_location::current());                 \
    } while (0)

#define CHECK_EQ(a, b)                                                                                       \
    do {                                                                                                     \
        const auto& tp_a_ = (a);                                                                             \
        const auto& tp_b_ = (b);                                                                             \
        if (!(tp_a_ == tp_b_))                                                                               \
            ::tp::test::reportFailure(#a " == " #b, ::tp::test::describe(tp_a_, tp_b_),                      \
                                      std::source_location::current());                                     \
    } while (0)

#define REQUIRE(expr)                                                                                        \
    do {                                                                                                     \
        if (!(expr)) {                                                                                       \
            ::tp::test::reportFailure(#expr, {}, std::source_location::current());                          \
            throw ::tp::test::RequireFailed{};                                                               \
        }                                                                                                    \
    } while (0)
