#pragma once
#include "profile.hpp"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace albion {

// One CPU payload at a time. No GPU objects: those stay on the render thread.
// The caller retains its payload if the worker is busy, providing backpressure.
template<class T> class DeferredRelease {
public:
    DeferredRelease() : worker_([this] { work(); }) {}
    ~DeferredRelease() {
        { std::lock_guard lock(mutex_); stopping_ = true; }
        ready_.notify_one();
        worker_.join();
    }
    DeferredRelease(const DeferredRelease&) = delete;
    DeferredRelease& operator=(const DeferredRelease&) = delete;

    bool idle() const {
        std::lock_guard lock(mutex_);
        return !busy_;
    }
    bool tryRelease(std::optional<T>& value) {
        if (!value) return true;
        {
            std::lock_guard lock(mutex_);
            if (busy_) return false;
            payload_ = std::make_unique<T>(std::move(*value));
            value.reset(); // moved-from value: no bulk buffers remain
            busy_ = true;
        }
        ready_.notify_one();
        return true;
    }

private:
    void work() {
        FORGE_THREAD("World CPU cleanup");
        for (;;) {
            std::unique_ptr<T> payload;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [&] { return stopping_ || payload_; });
                if (!payload_) return;
                payload = std::move(payload_);
            }
            { FORGE_ZONE("World prepared payload release (worker)"); payload.reset(); }
            { std::lock_guard lock(mutex_); busy_ = false; }
        }
    }
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::unique_ptr<T> payload_;
    bool busy_ = false, stopping_ = false;
    std::thread worker_;
};
} // namespace albion
