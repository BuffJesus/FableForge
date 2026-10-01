#include "worldscenery.hpp"
#include "thingsexport.hpp"
#include "cutoutcache.hpp"
#include "sceneryadmission.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <optional>
#include <set>

namespace albion::gui {
namespace {
using Clock = std::chrono::steady_clock;
constexpr uint64_t MiB = 1024u * 1024u;
constexpr int layer = Renderer::kWorldSceneryLayer;
struct Payload {
    WorldScenery::Demand demand;
    uint64_t generation = 0;
    bool cancelled = false;
    std::string error;
    std::vector<Renderer::PreparedBatch> batches;
    std::vector<terrainexport::Image> images;
    std::map<int, std::shared_ptr<const cutoutmips::Chain>> cutouts;
};
Payload prepare(WorldScenery::Demand demand, uint64_t generation,
                const terrainexport::Context& context, const std::filesystem::path& root,
                bool plants, bool objects, bool creatures, bool cutout,
                cutoutmips::Cache& cutoutCache,
                const std::shared_ptr<std::atomic<bool>>& cancel) {
    FORGE_THREAD("World scenery worker");
    FORGE_ZONE("World coarse scenery prepare");
    Payload result; result.demand = demand; result.generation = generation;
    auto cancelled = [&] { return result.cancelled = cancel->load(); };
    auto append = [&](foliageexport::Scene& scene) {
        if (cancelled()) return;
        foliageexport::loadWorldLods(scene, context);
        if (cancelled()) return;
        auto batches = Renderer::prepareLayer(scene, terrainexport::UpAxis::Y, true, true);
        const int offset = int(result.images.size());
        for (auto& batch : batches) {
            if (batch.image >= 0) batch.image += offset;
            result.batches.push_back(std::move(batch));
        }
        for (auto& image : scene.images) result.images.push_back(std::move(image));
    };
    try {
        if (cancelled()) return result;
        if (plants) {
            foliageexport::Options options;
            options.gameRoot = root; options.textures = true;
            options.up = terrainexport::UpAxis::Y; options.mapLocal = false;
            auto scene = foliageexport::load(demand.name, options, context);
            append(scene);
        }
        if (cancelled()) return result;
        if (objects || creatures) {
            thingsexport::Options options;
            options.gameRoot = root; options.textures = true;
            options.up = terrainexport::UpAxis::Y;
            options.objects = objects; options.creatures = creatures;
            options.originX = float(demand.x); options.originY = float(demand.y);
            auto scene = thingsexport::load(demand.name, options, context);
            append(scene);
        }
        if (cutout) for (const auto& batch : result.batches) {
            if (cancelled()) return result;
            if (batch.alpha && batch.image >= 0 && !result.cutouts.count(batch.image))
                result.cutouts.emplace(batch.image, cutoutCache.acquire(result.images.at(size_t(batch.image))));
        }
    } catch (const std::exception& error) { result.error = error.what(); }
    cancelled();
    return result;
}
} // namespace

struct WorldScenery::Impl {
    struct Resident { Demand demand; float fade = 0; };
    struct Retry { unsigned count = 0; Clock::time_point ready{}; };
    std::map<std::string, Resident> residents;
    std::map<std::string, Retry> retries;
    // Minimum allocation observed when an upload exceeded available room. Do
    // not repeat expensive preparation until that much room becomes available.
    std::map<std::string, uint64_t> memoryBlocks;
    std::string admission, fadingVictim;
    std::set<std::string> awaitingFallback;
    std::future<Payload> future;
    std::shared_ptr<std::atomic<bool>> cancel;
    std::string loading;
    std::optional<Payload> upload;
    DeferredRelease<Payload> release;
    // Only the single preparation worker accesses this cache. Chains retained by
    // upload/cleanup remain immutable when a later entry is evicted.
    std::shared_ptr<cutoutmips::Cache> cutoutCache = std::make_shared<cutoutmips::Cache>(16u * 1024u * 1024u);
    size_t uploadAt = 0;
    uint64_t generation = 0;
    bool retiring = false, enabled = false;
    std::vector<int> tags() const {
        std::vector<int> result;
        for (const auto& [name, resident] : residents) result.push_back(resident.demand.tag);
        if (upload && !retiring) result.push_back(upload->demand.tag);
        return result;
    }
    void retire() { retiring = true; if (release.tryRelease(upload)) retiring = false; }
};

WorldScenery::WorldScenery() : impl_(std::make_unique<Impl>()) {}
WorldScenery::~WorldScenery() {
    if (impl_->cancel) *impl_->cancel = true;
    if (impl_->future.valid()) impl_->future.wait();
}
bool WorldScenery::awaitingFallback(const std::string& name) const {
    return impl_->awaitingFallback.count(name) != 0;
}
float WorldScenery::preparedCoverage(const std::string& name) const {
    const auto found=impl_->residents.find(name);
    return found==impl_->residents.end() ? 0.0f : found->second.fade;
}
void WorldScenery::clear(Renderer& renderer) {
    auto& state = *impl_;
    if (!state.enabled && state.residents.empty() && (!state.upload || state.retiring) &&
        (!state.future.valid() || (state.cancel && state.cancel->load()))) return;
    if (state.cancel) *state.cancel = true;
    ++state.generation;
    renderer.clearLayer(layer);
    state.residents.clear(); state.retries.clear(); state.memoryBlocks.clear(); state.retire(); state.enabled = false;
    state.awaitingFallback.clear();
    state.admission.clear(); state.fadingVictim.clear();
    loaded = pending = bytes = wanted = visible = memoryLimit = 0;
    blocked = 0; settled = !state.future.valid() && !state.upload && state.release.idle();
    names.clear();
}

void WorldScenery::update(Renderer& renderer, const terrainexport::Context& context,
                          const std::filesystem::path& root, const std::vector<Demand>& demands,
                          float dt, bool enabled, bool plants, bool objects, bool creatures,
                          Renderer::VideoMemoryInfo memory, bool allowPrepare) {
    auto& state = *impl_;
    if (!enabled && state.enabled) clear(renderer);
    state.enabled = enabled;
    if (state.retiring) state.retire();
    std::vector<Demand> ordered = enabled ? demands : std::vector<Demand>{};
    std::stable_sort(ordered.begin(), ordered.end(), [](const Demand& a, const Demand& b) { return a.priority < b.priority; });
    std::map<std::string, Demand> desired;
    for (const auto& demand : ordered) {
        if (desired.size() == 64) break;
        if (demand.tag >= 0) desired.emplace(demand.name, demand);
    }
    wanted = desired.size();
    if (state.cancel && !desired.count(state.loading)) *state.cancel = true;
    const auto now = Clock::now();
    auto fail = [&](const std::string& name) {
        auto& retry = state.retries[name];
        retry.ready = now + std::chrono::seconds(std::min(30u, 2u << std::min(retry.count, 4u)));
        ++retry.count; ++failures;
    };
    if (!state.upload && state.release.idle() && state.future.valid() &&
        state.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        state.upload = state.future.get(); state.uploadAt = 0; state.loading.clear();
        auto& payload = *state.upload;
        if (payload.cancelled || payload.generation != state.generation || !desired.count(payload.demand.name)) state.retire();
        else if (!payload.error.empty()) { fail(payload.demand.name); state.retire(); }
    }
    if (state.upload && !state.retiring && !desired.count(state.upload->demand.name)) {
        renderer.removeLayerTag(layer, state.upload->demand.tag); state.retire();
    }
    bytes = renderer.layerSetBytes(layer, state.tags(), true);
    uint64_t limit = 64 * MiB;
    if (memory.valid && memory.budget) {
        limit = std::min(1024 * MiB, memory.budget / 4);
        const uint64_t other = memory.usage - std::min<uint64_t>(memory.usage, bytes);
        const uint64_t room = memory.budget - std::min(memory.budget, other);
        const uint64_t reserve = std::max(256 * MiB,memory.budget/5);
        limit = std::min(limit, room - std::min(room, reserve));
    }
    memoryLimit = size_t(limit);
    if (!limit && state.cancel) *state.cancel = true;
    // Being inside the total budget is not sufficient: a newly important map
    // can need room currently held by lower-priority, still-demanded scenery.
    // Reserve that room by fading one useful victim, then resampling actual
    // shared-texture accounting before choosing another. Never interrupt an
    // upload or a near/coarse handoff to perform this discretionary replacement.
    if (!state.upload && !state.future.valid()) {
        std::vector<worldview::SceneryAdmission> candidates;
        std::vector<std::string> candidateNames, residentNames;
        std::vector<worldview::SceneryOccupant> occupants;
        for (const auto& demand : ordered) {
            if (!desired.count(demand.name) || state.residents.count(demand.name) ||
                (demand.fullCoverage>=1 && !demand.fallbackNeeded)) continue;
            const auto retry=state.retries.find(demand.name);
            if (retry!=state.retries.end() && now<retry->second.ready) continue;
            const auto block=state.memoryBlocks.find(demand.name);
            const uint64_t required=block==state.memoryBlocks.end()?1:block->second;
            const uint64_t available=limit-std::min<uint64_t>(limit,bytes);
            if (required<=available || required>limit) {
                candidates.push_back({demand.priority,required,0});
                candidateNames.push_back(demand.name);
                // The helper stops at the first ready higher-priority request;
                // lower candidates cannot affect this frame's decision.
                if (required<=available) break;
                continue;
            }
            std::vector<int> reclaimableTags;
            for (const auto& [name,resident] : state.residents) {
                const auto requested=desired.find(name);
                const auto& current=requested==desired.end()?resident.demand:requested->second;
                const float priority=requested==desired.end()?1e30f:current.priority;
                if (!current.fallbackNeeded && !(current.fullCoverage>0 && current.fullCoverage<1) &&
                    priority>demand.priority+worldview::kSceneryReplacementMargin)
                    reclaimableTags.push_back(resident.demand.tag);
            }
            candidates.push_back({demand.priority,required,
                renderer.layerSetBytes(layer,reclaimableTags,true)});
            candidateNames.push_back(demand.name);
        }
        int fading=-1;
        for (const auto& [name,resident] : state.residents) {
            const auto demand=desired.find(name);
            const auto& current=demand==desired.end()?resident.demand:demand->second;
            if (name==state.fadingVictim) fading=int(occupants.size());
            occupants.push_back({demand==desired.end()?1e30f:current.priority,
                renderer.layerTagBytes(layer,resident.demand.tag),
                current.fallbackNeeded || (current.fullCoverage>0 && current.fullCoverage<1)});
            residentNames.push_back(name);
        }
        const auto replacement=worldview::sceneryReplacement(candidates,occupants,
            limit-std::min<uint64_t>(limit,bytes),limit,fading);
        state.admission=replacement.admission<0?std::string{}:candidateNames[size_t(replacement.admission)];
        state.fadingVictim=replacement.victim<0?std::string{}:residentNames[size_t(replacement.victim)];
    } else { state.admission.clear(); state.fadingVictim.clear(); }
    const float step = std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.05f) / 0.6f : 0;
    visible = 0;
    for (auto it = state.residents.begin(); it != state.residents.end();) {
        auto demand = desired.find(it->first);
        auto& resident = it->second;
        const bool replacing=it->first==state.fadingVictim;
        resident.fade = std::clamp(resident.fade + (demand != desired.end() && !replacing ? step : -step), 0.0f, 1.0f);
        if (demand != desired.end()) resident.demand = demand->second;
        if (replacing && resident.demand.fullCoverage>=1) resident.fade=0;
        if ((demand == desired.end() || replacing) && resident.fade == 0) {
            const uint64_t before=renderer.layerSetBytes(layer,state.tags(),true);
            const std::string name=it->first;
            renderer.removeLayerTag(layer, resident.demand.tag); it = state.residents.erase(it);
            if (replacing) {
                const uint64_t after=renderer.layerSetBytes(layer,state.tags(),true);
                state.memoryBlocks[name]=std::max<uint64_t>(1,before-std::min(before,after));
                state.fadingVictim.clear(); ++priorityEvictions;
            }
            continue;
        }
        const float coverage = resident.fade * (1 - std::clamp(resident.demand.fullCoverage, 0.0f, 1.0f));
        renderer.setLayerTagVisible(layer, resident.demand.tag, coverage > 0);
        renderer.setLayerTagFade(layer, resident.demand.tag, 1-coverage, true, 1-coverage);
        visible += coverage > 0;
        ++it;
    }
    bytes = renderer.layerSetBytes(layer, state.tags(), true);
    // Memory pressure takes precedence over a fade. Evict least useful maps first.
    while ((bytes > limit || limit == 0) && !state.residents.empty()) {
        auto worst = std::max_element(state.residents.begin(), state.residents.end(),
            [&](const auto& a, const auto& b) {
                const float pa = desired.count(a.first) ? a.second.demand.priority : 1e30f;
                const float pb = desired.count(b.first) ? b.second.demand.priority : 1e30f;
                return pa < pb;
            });
        renderer.removeLayerTag(layer, worst->second.demand.tag);
        state.residents.erase(worst);
        bytes = renderer.layerSetBytes(layer, state.tags(), true);
    }
    if (state.upload && !state.retiring) {
        auto& payload = *state.upload;
        bool ok = limit > 0;
        bool memoryDenied = !ok;
        std::vector<int> residentTags;
        for (const auto& [name, resident] : state.residents) residentTags.push_back(resident.demand.tag);
        const uint64_t residentBytes = renderer.layerSetBytes(layer, residentTags, true);
        const auto started = Clock::now();
        for (unsigned count = 0; ok && state.uploadAt < payload.batches.size() && count < 32; ++count) {
            const auto& batch = payload.batches[state.uploadAt++];
            const auto mip = payload.cutouts.find(batch.image);
            ok = renderer.appendPreparedBatch(layer, batch, payload.images, payload.demand.tag, false,
                mip == payload.cutouts.end() ? nullptr : mip->second.get());
            bytes = renderer.layerSetBytes(layer, state.tags(), true);
            memoryDenied = bytes > limit;
            ok = ok && !memoryDenied;
            if (Clock::now() - started >= std::chrono::milliseconds(2)) break;
        }
        if (!ok) {
            if (memoryDenied)
                state.memoryBlocks[payload.demand.name] = std::max<uint64_t>(1, bytes - std::min<uint64_t>(bytes, residentBytes));
            else fail(payload.demand.name);
            renderer.removeLayerTag(layer, payload.demand.tag); state.retire();
        } else if (state.uploadAt == payload.batches.size()) {
            // An empty map is a valid resident, preventing endless empty reloads.
            state.residents[payload.demand.name] = {desired.at(payload.demand.name), 0};
            state.retries.erase(payload.demand.name); state.memoryBlocks.erase(payload.demand.name); state.retire();
        }
    }
    bytes = renderer.layerSetBytes(layer, state.tags(), true);
    const uint64_t available = limit - std::min<uint64_t>(limit, bytes);
    auto memoryBlocked = [&](const std::string& name) {
        const auto block = state.memoryBlocks.find(name);
        return block != state.memoryBlocks.end() && available < block->second;
    };
    pending = 0; blocked = ordered.size() - std::min(ordered.size(), desired.size());
    size_t constrained = 0;
    for (const auto& [name, demand] : desired) {
        if (state.residents.count(name) || (demand.fullCoverage >= 1 && !demand.fallbackNeeded)) continue;
        ++pending;
        const auto retry = state.retries.find(name);
        constrained += limit <= bytes || memoryBlocked(name) || (retry != state.retries.end() && now < retry->second.ready);
    }
    blocked += constrained;
    if (enabled && allowPrepare && state.fadingVictim.empty() && limit > bytes && !state.future.valid() && !state.upload && state.release.idle()) {
        for (const auto& demand : ordered) {
            if (!desired.count(demand.name) || state.residents.count(demand.name) ||
                (demand.fullCoverage >= 1 && !demand.fallbackNeeded)) continue;
            if (memoryBlocked(demand.name)) continue;
            // Keep newly reclaimed space reserved until the preferred admission
            // can start; otherwise a cheaper low-priority map can refill it.
            if (!state.admission.empty() && demand.name!=state.admission) continue;
            const auto retry = state.retries.find(demand.name);
            if (retry != state.retries.end() && now < retry->second.ready) continue;
            state.cancel = std::make_shared<std::atomic<bool>>(false);
            auto hold = std::make_shared<const terrainexport::Context>(context);
            state.loading = demand.name;
            state.future = std::async(std::launch::async,
                [demand, generation = state.generation, hold, root, plants, objects, creatures,
                 cutout = renderer.worldCutoutMips, cache = state.cutoutCache, cancel = state.cancel] {
                    return prepare(demand, generation, *hold, root, plants, objects, creatures, cutout, *cache, cancel);
                });
            break;
        }
    }
    loaded = state.residents.size(); names.clear(); visible = 0;
    for (const auto& [name, resident] : state.residents) {
        names.push_back(name);
        visible += resident.fade > 0 && resident.demand.fullCoverage < 1;
    }
    bytes = renderer.layerSetBytes(layer, state.tags(), true);
    state.awaitingFallback.clear();
    for (const auto& [name, demand] : desired) {
        // Keep the outgoing detailed map covered between successive victim
        // removals while the next frame resamples how much room remains.
        const bool reclaiming=name==state.admission;
        if (!demand.fallbackNeeded || !limit || (memoryBlocked(name) && !reclaiming)) continue;
        const auto retry = state.retries.find(name);
        if (retry != state.retries.end() && now < retry->second.ready) continue;
        const auto resident = state.residents.find(name);
        // Never pin near geometry for an allocation that has no room to start.
        // Already uploaded fallback geometry can finish its fade without room.
        if (resident == state.residents.end() ? available > 0 || reclaiming : resident->second.fade < 1)
            state.awaitingFallback.insert(name);
    }
    settled = state.admission.empty() && state.fadingVictim.empty() &&
        pending <= constrained && !state.future.valid() && !state.upload && state.release.idle() &&
        std::all_of(state.residents.begin(), state.residents.end(), [&](const auto& item) {
            return desired.count(item.first) ? item.second.fade >= 1 : item.second.fade <= 0;
        });
}
} // namespace albion::gui
