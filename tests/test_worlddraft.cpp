#include "worlddraft.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace albion::editor;
static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static bool empty(const WorldDraft& draft) { return draft.moves.empty() && draft.owners.empty() && draft.sees.empty(); }
int main() {
    try {
        WorldLayout before;
        WorldMapBox map; map.name="Map"; map.x=32; map.y=64; map.region="Old";
        before.maps.push_back(map);
        WorldMapBox other; other.name="Other"; other.x=320; other.y=640; other.region="Old";
        before.maps.push_back(other);
        WorldRegion region; region.name="Region"; before.regionInfo.push_back(region);
        WorldDraft submitted{{{"Map", 96, 128}}, {{"Map", "New"}}, {{"Region", "Map", true}}};
        WorldLayout after=before;
        after.maps[0].x=96; after.maps[0].y=128; after.maps[0].region="New";
        after.regionInfo[0].sees.push_back("Map");
        check(empty(rebaseWorldDraft(before,after,submitted,submitted)), "unchanged submitted edits stayed dirty");
        const auto reverted=rebaseWorldDraft(before,after,submitted,{});
        check(reverted.moves.size()==1 && reverted.moves[0].x==32 && reverted.moves[0].y==64, "put-back move did not retain old origin");
        check(reverted.owners.size()==1 && reverted.owners[0].region=="Old", "put-back owner did not retain old owner");
        check(reverted.sees.size()==1 && !reverted.sees[0].sees, "put-back visibility did not retain old state");
        auto later=submitted;
        later.moves[0].x=160;
        later.owners[0].region="Third";
        later.sees.clear();
        later.moves.push_back({"Other",352,640});
        auto rebased=rebaseWorldDraft(before,after,submitted,later);
        check(rebased.moves.size()==2 && rebased.moves[0].x==160 && rebased.moves[1].name=="Other", "later or unrelated move was lost");
        check(rebased.owners.size()==1 && rebased.owners[0].region=="Third", "later owner was lost");
        check(rebased.sees.size()==1 && !rebased.sees[0].sees, "later visibility cancellation was lost");
        auto undoSnapshot=submitted;
        undoSnapshot.moves[0].x=160;
        const auto undoStep=rebaseWorldDraft(before,after,submitted,undoSnapshot);
        check(undoStep.moves.size()==1 && undoStep.moves[0].x==160 && undoStep.owners.empty() && undoStep.sees.empty(), "undo snapshot retained saved region edits");
        WorldLayout missing=after; missing.maps.clear(); missing.regionInfo.clear();
        const auto orphaned=rebaseWorldDraft(before,missing,submitted,later);
        check(orphaned.moves.size()==2 && orphaned.owners.size()==1 && orphaned.sees.size()==1, "missing targets discarded pending edits");
        check(before.maps[0].x==32 && after.maps[0].x==96 && later.moves[0].x==160, "rebase modified its input state");
        WorldLayout bounds;
        WorldMapBox square; square.slot=1; square.name="Square"; square.w=64; square.h=64;
        bounds.maps.push_back(square);
        std::string why;
        check(checkMove(bounds, {}, {"Square",8128,8128}, why), "valid move at world edge refused");
        check(!checkMove(bounds, {}, {"Square",std::numeric_limits<int>::max()-31,0}, why), "overflowing X extent accepted");
        check(!checkMove(bounds, {}, {"Square",0,std::numeric_limits<int>::max()-31}, why), "overflowing Y extent accepted");
        check(!checkMove(bounds, {}, {"Square",8160,8128}, why), "map extending beyond world accepted");
        check(!checkMove(bounds, {}, {"Square",std::numeric_limits<int>::min(),0}, why), "negative extreme origin accepted");
        bounds.maps[0].w=0;
        check(!checkMove(bounds, {}, {"Square",0,0}, why), "zero-width map accepted");
        bounds.maps[0].w=-64;
        check(!checkMove(bounds, {}, {"Square",0,0}, why), "negative-width map accepted");
        bounds.maps[0].w=64; bounds.maps[0].h=0;
        check(!checkMove(bounds, {}, {"Square",0,0}, why), "zero-height map accepted");
        std::cout << "World draft rebase checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
