#include "rowexecutor.hpp"
#include "deferredrelease.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <set>
#include <stdexcept>

static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct ReleaseProbe {
    std::shared_ptr<std::promise<void>> entered;
    std::shared_future<void> gate;
    std::thread::id* thread;
    std::atomic<unsigned>* destroyed;
    ReleaseProbe(ReleaseProbe&& other) noexcept
        : entered(std::move(other.entered)), gate(std::move(other.gate)),
          thread(other.thread), destroyed(other.destroyed) {}
    ReleaseProbe(std::shared_ptr<std::promise<void>> e, std::shared_future<void> g,
                 std::thread::id* t, std::atomic<unsigned>* d)
        : entered(std::move(e)), gate(std::move(g)), thread(t), destroyed(d) {}
    ~ReleaseProbe() {
        if (!entered) return;
        *thread = std::this_thread::get_id();
        entered->set_value();
        gate.wait();
        ++*destroyed;
    }
};

static void testRelease() {
    std::promise<void> unblock;
    auto gate = unblock.get_future().share();
    std::atomic<unsigned> destroyed{0};
    std::thread::id firstThread, secondThread;
    auto first = std::make_shared<std::promise<void>>();
    auto second = std::make_shared<std::promise<void>>();
    auto started = first->get_future();
    bool accepted, refused, retained, background, secondAccepted;
    {
        albion::DeferredRelease<ReleaseProbe> release;
        std::optional<ReleaseProbe> a(std::in_place, first, gate, &firstThread, &destroyed);
        std::optional<ReleaseProbe> b(std::in_place, second, gate, &secondThread, &destroyed);
        accepted = release.tryRelease(a) && !a;
        started.wait();
        background = firstThread != std::this_thread::get_id();
        refused = !release.tryRelease(b);
        retained = b.has_value() && destroyed == 0;
        // Unblock before assertions so an assertion failure cannot strand cleanup.
        unblock.set_value();
        while (!release.idle()) std::this_thread::yield();
        secondAccepted = release.tryRelease(b) && !b;
    } // destruction must drain the last accepted payload
    check(accepted && refused && retained && secondAccepted, "release backpressure/lifetime");
    check(background && secondThread != std::this_thread::get_id() && destroyed == 2,
          "payload destruction stayed on caller or was lost at shutdown");
}

int main() {
    try {
        check(albion::RowExecutor::defaultWorkers(0) == 1, "unknown CPU count");
        check(albion::RowExecutor::defaultWorkers(2) == 1, "small CPU headroom");
        check(albion::RowExecutor::defaultWorkers(6) == 4, "CPU headroom");
        check(albion::RowExecutor::defaultWorkers(128) == 8, "worker ceiling");
        albion::RowExecutor pool(3);
        std::atomic<unsigned> empty{0};
        pool.run(0, [&](uint32_t) { ++empty; });
        check(empty == 0, "empty range");
        std::mutex idsMutex;
        std::set<std::thread::id> ids;
        // More simultaneous producers than queue capacity; every row must run
        // exactly once and all producers must share the same three workers.
        std::vector<std::future<void>> callers;
        for (unsigned i = 0; i < 12; ++i) {
            callers.push_back(std::async(std::launch::async, [&] {
                std::array<std::atomic<unsigned>, 257> visits{};
                pool.run(uint32_t(visits.size()), [&](uint32_t row) {
                    ++visits[row];
                    std::lock_guard lock(idsMutex);
                    ids.insert(std::this_thread::get_id());
                });
                for (auto& count : visits) check(count == 1, "row skipped or duplicated");
            }));
        }
        for (auto& caller : callers) caller.get();
        check(!ids.empty() && ids.size() <= 3, "worker budget exceeded");

        std::atomic<unsigned> nested{0};
        pool.run(32, [&](uint32_t) { pool.run(32, [&](uint32_t) { ++nested; }); });
        check(nested == 1024, "nested invocation deadlock or lost rows");

        std::atomic<unsigned> otherBandsFinished{0};
        bool threw = false;
        try {
            pool.run(96, [&](uint32_t row) {
                if (row == 0) throw std::runtime_error("expected worker error");
                if (row == 32 || row == 64) std::this_thread::sleep_for(std::chrono::milliseconds(5));
                if (row == 63 || row == 95) ++otherBandsFinished;
            });
        } catch (const std::runtime_error&) { threw = true; }
        check(threw && otherBandsFinished == 2, "exception escaped before other workers drained");
        std::atomic<unsigned> recovered{0};
        pool.run(99, [&](uint32_t) { ++recovered; });
        check(recovered == 99, "pool did not recover after callback exception");
        albion::RowExecutor single(1);
        single.run(32, [&](uint32_t) { single.run(32, [](uint32_t) {}); });
        testRelease();
        std::cout << "PASS: bounded bakes, exact rows, nesting, exception recovery, worker cleanup/backpressure\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
