#pragma once

#include "profile.hpp"
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace albion {

// Shared by terrain bakes, rather than spawning a hardware-sized pool per map.
// Callers keep their input/output alive until run() returns, including on error.
class RowExecutor {
public:
    static unsigned defaultWorkers(unsigned hardware) {
        return std::clamp(hardware > 2 ? hardware - 2 : 1u, 1u, 8u);
    }

    explicit RowExecutor(unsigned count) : capacity_(2 * std::max(1u, count)) {
        try {
            for (unsigned i = 0; i < std::max(1u, count); ++i)
                workers_.emplace_back([this] { work(); });
        } catch (...) {
            stop();
            throw;
        }
    }
    ~RowExecutor() { stop(); }
    RowExecutor(const RowExecutor&) = delete;
    RowExecutor& operator=(const RowExecutor&) = delete;

    template<class Fn> void run(uint32_t rows, Fn&& fn) {
        // A nested bake must not wait for the same saturated pool.
        if (active_ == this || rows < 16) {
            for (uint32_t row = 0; row < rows; ++row) fn(row);
            return;
        }
        const unsigned count = std::min<unsigned>(unsigned(workers_.size()), rows / 8);
        std::vector<std::future<void>> done;
        done.reserve(count); // no allocation after a task is submitted
        std::exception_ptr error;
        try {
            for (unsigned t = 0; t < count; ++t) {
                done.push_back(submit([&, t] {
                    FORGE_ZONE("Terrain bake rows");
                    const uint32_t first = uint32_t(uint64_t(rows) * t / count);
                    const uint32_t end = uint32_t(uint64_t(rows) * (t + 1) / count);
                    for (uint32_t row = first; row < end; ++row) fn(row);
                }));
            }
        } catch (...) { error = std::current_exception(); }
        {
            FORGE_ZONE("Terrain bake join");
            // Drain every submitted task before propagating an exception: its
            // callback can refer to the calling bake's stack and image memory.
            for (auto& future : done) {
                try { future.get(); }
                catch (...) { if (!error) error = std::current_exception(); }
            }
        }
        if (error) std::rethrow_exception(error);
    }

private:
    std::future<void> submit(std::function<void()> fn) {
        auto task = std::make_shared<std::packaged_task<void()>>(std::move(fn));
        auto result = task->get_future();
        {
            FORGE_ZONE("Terrain bake queue wait");
            std::unique_lock lock(mutex_);
            space_.wait(lock, [&] { return queue_.size() < capacity_; });
            queue_.emplace_back([task] { (*task)(); });
        }
        ready_.notify_one();
        return result;
    }

    void work() {
        FORGE_THREAD("Terrain bake pool");
        active_ = this;
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
                if (queue_.empty()) break;
                task = std::move(queue_.front());
                queue_.pop_front();
            }
            space_.notify_one();
            task(); // packaged_task captures callback exceptions
        }
        active_ = nullptr;
    }

    void stop() {
        { std::lock_guard lock(mutex_); stopping_ = true; }
        ready_.notify_all();
        for (auto& worker : workers_) if (worker.joinable()) worker.join();
    }

    // Trivial TLS only: no owning allocation or thread-exit destructor.
    inline static thread_local RowExecutor* active_ = nullptr;
    const size_t capacity_;
    std::mutex mutex_;
    std::condition_variable ready_, space_;
    std::deque<std::function<void()>> queue_;
    bool stopping_ = false;
    std::vector<std::thread> workers_;
};

} // namespace albion
