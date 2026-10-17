#pragma once
// Fundamental integer aliases and assertion macros shared by every module.

#include <cstddef>
#include <cstdint>

namespace tp {

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using usize = std::size_t;

[[noreturn]] void assertFailed(const char* expression, const char* file, int line);

} // namespace tp

// Always-on assertion for invariants whose violation would corrupt the game state.
#define TP_ASSERT(cond)                                                                                      \
    do {                                                                                                     \
        if (!(cond)) [[unlikely]]                                                                            \
            ::tp::assertFailed(#cond, __FILE__, __LINE__);                                                   \
    } while (0)

#ifdef NDEBUG
#define TP_DEBUG_ASSERT(cond) ((void)0)
#else
#define TP_DEBUG_ASSERT(cond) TP_ASSERT(cond)
#endif
