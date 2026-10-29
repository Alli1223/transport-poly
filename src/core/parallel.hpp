#pragma once
// A process-wide pool of worker threads for heavy, independent pieces of pure computation (route
// searches, state hashing). The simulation stays deterministic only if every item writes its result
// to its own slot and the caller merges the slots in index order: nothing may depend on which
// thread ran an item or when.

#include "core/types.hpp"

#include <functional>

namespace tp {

// Runs fn(i) for every i in [0, count), on the calling thread and the workers, and returns once
// all are done. Items are handed out in blocks of at least `grain`. A call made from inside a
// worker runs serially. The first exception thrown by an item is rethrown here.
void parallelFor(usize count, const std::function<void(usize)>& fn, usize grain = 1);

// Threads that run parallelFor items, including the caller (1: everything runs serially).
usize parallelThreads();
// Changes the number of threads (0: one per hardware thread, at most 16). For tests and for
// players who want to leave cores free; results never depend on it.
void setParallelThreads(usize threads);

} // namespace tp
