#pragma once
#include "renderer.hpp"
#include <filesystem>
#include <memory>

namespace albion::gui {

// Coarse object fallback outside the detailed terrain/map budget. All GPU work
// stays on the caller's render thread; preparation and bulk cleanup are workers.
class WorldScenery {
public:
    struct Demand {
        std::string name;
        int tag = -1, x = 0, y = 0;
        float priority = 0, fullCoverage = 0;
        bool fallbackNeeded = false; // outgoing near detail needs a ready replacement
    };
    WorldScenery();
    ~WorldScenery();
    WorldScenery(const WorldScenery&) = delete;
    WorldScenery& operator=(const WorldScenery&) = delete;
    void update(Renderer&, const terrainexport::Context&, const std::filesystem::path& root,
                const std::vector<Demand>&, float dt, bool enabled,
                bool plants, bool objects, bool creatures,
                Renderer::VideoMemoryInfo, bool allowPrepare);
    void clear(Renderer&);
    bool awaitingFallback(const std::string& name) const;
    float preparedCoverage(const std::string& name) const;

    size_t loaded = 0, pending = 0, bytes = 0, failures = 0;
    size_t wanted = 0, visible = 0, memoryLimit = 0;
    size_t blocked = 0; // retry, memory, or map-cap constrained demand
    size_t priorityEvictions = 0; // completed lower-priority residency replacements
    bool settled = true; // includes worker/upload/cleanup and resident fades
    std::vector<std::string> names;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace albion::gui
