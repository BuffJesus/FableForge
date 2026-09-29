// CPU-only regression for profiler TLS destruction under short-lived workers.
// Start tracy-capture first. Deliberately no window, D3D device or game data.
#include "profile.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

int main() {
    using namespace std::chrono_literals;
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (!TracyIsConnected) {
        if (std::chrono::steady_clock::now() >= deadline) return 2;
        std::this_thread::sleep_for(10ms);
    }
    std::atomic<unsigned> completed{0};
    for (unsigned wave = 0; wave < 128; ++wave) {
        FORGE_ZONE("Worker lifetime wave");
        std::vector<std::thread> workers;
        for (unsigned i = 0; i < 32; ++i) workers.emplace_back([&] {
            FORGE_THREAD("Short-lived profile worker");
            FORGE_ZONE("Worker lifetime check");
            std::vector<unsigned> scratch(4096, 7);
            std::this_thread::sleep_for(500us);
            if (scratch.front() == 7 && scratch.back() == 7) ++completed;
        });
        for (auto& worker : workers) worker.join();
    }
    std::printf("Completed %u profiled workers\n", completed.load());
    return completed == 4096 ? 0 : 1;
}
