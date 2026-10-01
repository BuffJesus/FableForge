#pragma once
#include "overworld.hpp"
#include <algorithm>

namespace albion::editor {

struct WorldDraft {
    std::vector<MapMove> moves;
    std::vector<OwnerEdit> owners;
    std::vector<SeesEdit> sees;
};

// Preserve the desired state of a draft after its submitted edits were saved.
// A removed submitted edit means the user put that field back to the OLD base;
// it may therefore become a new pending edit against the freshly saved base.
inline WorldDraft rebaseWorldDraft(const WorldLayout& before, const WorldLayout& after,
                                  const WorldDraft& submitted, WorldDraft draft) {
    for (const auto& saved : submitted.moves) {
        const auto found = std::find_if(draft.moves.begin(), draft.moves.end(),
            [&](const auto& edit) { return edit.name == saved.name; });
        if (found == draft.moves.end())
            if (const auto* original = before.find(saved.name))
                draft.moves.push_back({saved.name, original->x, original->y});
    }
    for (const auto& saved : submitted.owners) {
        const auto found = std::find_if(draft.owners.begin(), draft.owners.end(),
            [&](const auto& edit) { return edit.map == saved.map; });
        if (found == draft.owners.end())
            if (const auto* original = before.find(saved.map))
                draft.owners.push_back({saved.map, original->region});
    }
    for (const auto& saved : submitted.sees) {
        const auto found = std::find_if(draft.sees.begin(), draft.sees.end(),
            [&](const auto& edit) { return edit.region == saved.region && edit.map == saved.map; });
        if (found == draft.sees.end())
            if (const auto* original = before.region(saved.region))
                draft.sees.push_back({saved.region, saved.map, original->sees_(saved.map)});
    }
    std::erase_if(draft.moves, [&](const auto& edit) {
        const auto* saved = after.find(edit.name);
        return saved && saved->x == edit.x && saved->y == edit.y;
    });
    std::erase_if(draft.owners, [&](const auto& edit) {
        const auto* saved = after.find(edit.map);
        return saved && saved->region == edit.region;
    });
    std::erase_if(draft.sees, [&](const auto& edit) {
        const auto* saved = after.region(edit.region);
        return saved && saved->sees_(edit.map) == edit.sees;
    });
    return draft;
}

} // namespace albion::editor
