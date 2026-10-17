#include "test.hpp"

TEST_CASE("framework: checks pass") {
    CHECK(1 + 1 == 2);
    CHECK_EQ(2 * 21, 42);
}
