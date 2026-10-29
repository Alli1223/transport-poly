#include "core/parallel.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace tp {

namespace {

// True on worker threads, and on a caller while it runs a parallelFor (nested calls run serially).
thread_local bool tInParallel = false;

usize defaultThreads() { return std::clamp<usize>(std::thread::hardware_concurrency(), 1, 16); }

class WorkerPool {
public:
    explicit WorkerPool(usize threads) { start(threads); }

    usize threads() const { return workers_.size() + 1; }

    void resize(usize threads) {
        std::scoped_lock runLock(runMutex_);
        stop();
        start(threads);
    }

    void run(usize count, const std::function<void(usize)>& fn, usize grain) {
        grain = std::max<usize>(1, grain);
        if (count == 0) return;
        if (tInParallel || count <= grain) {
            for (usize i = 0; i < count; ++i) fn(i);
            return;
        }
        std::scoped_lock runLock(runMutex_);
        // Items that call parallelFor themselves run those calls serially.
        tInParallel = true;
        const struct Leave {
            ~Leave() { tInParallel = false; }
        } leave;
        if (workers_.empty()) {
            for (usize i = 0; i < count; ++i) fn(i);
            return;
        }
        Job job;
        job.fn = &fn;
        job.count = count;
        job.grain = grain;
        {
            std::scoped_lock lock(mutex_);
            job.active = workers_.size();
            job_ = &job;
            ++generation_;
        }
        wake_.notify_all();
        work(job);
        {
            std::unique_lock lock(mutex_);
            done_.wait(lock, [&] { return job.active == 0; });
            job_ = nullptr;
        }
        if (job.error) std::rethrow_exception(job.error);
    }

private:
    struct Job {
        const std::function<void(usize)>* fn = nullptr;
        usize count = 0;
        usize grain = 1;
        std::atomic<usize> next{0};
        usize active = 0; // workers yet to finish (guarded by the pool mutex)
        std::mutex errorMutex;
        std::exception_ptr error;
    };

    static void work(Job& job) {
        while (true) {
            const usize begin = job.next.fetch_add(job.grain);
            if (begin >= job.count) return;
            const usize end = std::min(job.count, begin + job.grain);
            for (usize i = begin; i < end; ++i) {
                try {
                    (*job.fn)(i);
                } catch (...) {
                    std::scoped_lock lock(job.errorMutex);
                    if (!job.error) job.error = std::current_exception();
                    job.next = job.count; // stop handing out items
                }
            }
        }
    }

    // Called while no job runs, so a new worker starts out waiting for the next generation.
    void start(usize threads) {
        stopping_ = false;
        const u64 current = generation_;
        for (usize i = 1; i < std::max<usize>(1, threads); ++i)
            workers_.emplace_back([this, current] { loop(current); });
    }

    void stop() {
        {
            std::scoped_lock lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        for (std::thread& t : workers_) t.join();
        workers_.clear();
    }

    void loop(u64 seen) {
        tInParallel = true;
        while (true) {
            Job* job = nullptr;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [&] { return stopping_ || generation_ != seen; });
                if (stopping_) return;
                seen = generation_;
                job = job_;
            }
            work(*job);
            {
                std::scoped_lock lock(mutex_);
                if (--job->active == 0) done_.notify_all();
            }
        }
    }

    std::vector<std::thread> workers_;
    std::mutex runMutex_; // one parallelFor at a time; calls from other threads wait their turn
    std::mutex mutex_;
    std::condition_variable wake_, done_;
    Job* job_ = nullptr;
    u64 generation_ = 0;
    bool stopping_ = false;
};

// Never destroyed: idle workers simply end with the process.
WorkerPool& pool() {
    static WorkerPool* p = new WorkerPool(defaultThreads());
    return *p;
}

} // namespace

void parallelFor(usize count, const std::function<void(usize)>& fn, usize grain) { pool().run(count, fn, grain); }

usize parallelThreads() { return pool().threads(); }

void setParallelThreads(usize threads) { pool().resize(threads == 0 ? defaultThreads() : std::min<usize>(threads, 16)); }

} // namespace tp
