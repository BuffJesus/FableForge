#pragma once
// albion::editor -- the editable level document behind the FableForge
// editor: one map's .tng (placed things) with undoable commands, plus the
// terrain heights it rests on.
//
// The document wraps forge::tng::File, which keeps every raw line so an
// unmodified file serialises back byte-identically and each edit touches only
// the lines it must. Transform edits rewrite the CTCPhysicsStandard /
// CTCPhysicsNavigator position + right-handed basis lines in retail float
// spelling; new things go through forge::thingplacer (retail field order,
// per-file UID namespace).
//
// Undo is snapshot based: every command stores the full serialised text
// before it ran (a .tng is a few hundred KB at most; the stack is capped).
// Undo/redo re-parse the snapshot, so thing indices are stable for the same
// text and callers should track things by UID across commands.
//
// Frames use the engine's convention (see thingsexport::thingBasis): a thing
// is `pos` + right-handed `forward`/`up`; the mesh instance matrix rows are
// { -(forward x up), -forward, up } * 0.01 * ObjectScale.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "forge/lev.hpp"
#include "forge/terrain.hpp"
#include "forge/terraintex.hpp"
#include "forge/thingplacer.hpp"
#include "forge/tng.hpp"

namespace albion::editor {

struct Frame {
    float pos[3] = {0, 0, 0};         // map-local Fable units
    float forward[3] = {1, 0, 0};
    float up[3] = {0, 0, 1};
    float scale = 1.0f;               // ObjectScale (1 when absent)
};

// 4x4 row-vector matrix (rows = images of local axes, row 3 = position), Fable
// space, cm-scale baked in like the preview instances: v_world = v_local * M.
void frameToMatrix(const Frame& f, float m[16]);
// Inverse of frameToMatrix for a matrix of the same shape (rows orthogonal,
// uniform scale). Returns false for a degenerate matrix.
bool matrixToFrame(const float m[16], Frame& f);
void multiply(const float a[16], const float b[16], float out[16]);   // out = a * b (row-vector order)
bool invert(const float m[16], float out[16]);

// Editable terrain state: vertex heights and per-cell walkability, both on the
// LEV's (width+1) x (height+1) grid (walkability of the extra row/column is
// carried but meaningless, like the file's).
struct TerrainState {
    std::vector<float> heights;
    std::vector<uint8_t> walkable;
    std::vector<std::array<uint8_t, 3>> themeIndex;     // per cell: the 3 palette slots
    std::vector<std::array<uint8_t, 3>> themeStrength;  // ... and their weights (sum 255)
    std::vector<forge::lev::GroundTheme> palette;       // the LEV's 256 ground-theme slots
};

struct TerrainBrush {
    enum class Mode { Raise, Lower, Flatten, Smooth, Walkable, Blocked, Theme };
    Mode mode = Mode::Raise;
    float x = 0, y = 0;        // map-local centre
    float radius = 6.0f;
    float strength = 1.0f;     // units/second (raise/lower), blend/second (flatten/smooth/theme)
    uint8_t themeIndex = 0;    // Theme: LEV palette slot to paint
};

struct ThingSummary {
    size_t index = 0;
    uint64_t uid = 0;
    std::string type;          // NewThing <type>
    std::string definition;    // DefinitionType, unquoted
    std::string scriptName;    // "" for NULL
    bool hasFrame = false;
};

class Document {
public:
    // Loads <map>.tng (loose data/Levels/FinalAlbion/<map>.tng, else the
    // FinalAlbion.wad entry) and the matching .lev for ground heights (from
    // `levPath` when given). Returns false with `error` set.
    bool open(const std::filesystem::path& gameRoot, const std::string& mapName,
              const std::filesystem::path& levPath, std::string& error);
    // In-memory document (tests, scratch levels).
    bool openText(const std::string& mapName, std::string tngText, std::string& error);
    // Attach a .lev (ground heights + terrain editing); open() does this itself.
    bool loadLevel(const std::filesystem::path& levPath, std::string& error);

    const std::string& mapName() const { return mapName_; }
    const forge::tng::File& file() const { return file_; }
    std::string text() const { return file_.serialize(); }
    bool loadedFromWad() const { return fromWad_; }
    std::filesystem::path loosePath() const { return loosePath_; }

    size_t thingCount() const { return file_.things().size(); }
    ThingSummary summary(size_t index) const;
    std::optional<size_t> indexOfUid(uint64_t uid) const;
    uint64_t uidOf(size_t index) const;
    bool frameOf(size_t index, Frame& out) const;

    // Ground height (bilinear LEV sample) at a map-local XY; nullopt when no
    // level is loaded or the point is outside the grid.
    std::optional<float> groundHeight(float x, float y) const;
    bool hasLevel() const { return level_ != nullptr; }
    const forge::lev::File* level() const { return level_.get(); }

    // ---- commands (each one undo step) ----
    void setFrame(size_t index, const Frame& frame);
    // After the ground changed (a stitch, a scripted sculpt): every thing that
    // stood on the old ground (|z - old height| <= tolerance; retail things sit
    // within 0.4 of it, buried ones further) follows it, keeping its offset.
    // One undo step; returns how many moved.
    size_t reseatThings(const TerrainState& before, float tolerance = 1.0f);
    // The same against the terrain as last saved/deployed (the sculpt session's baseline).
    size_t reseatThingsSinceSave(float tolerance = 1.0f) { return savedTerrain_ ? reseatThings(*savedTerrain_, tolerance) : 0; }
    void setProperty(size_t index, const std::string& key, const std::string& value);
    // Copy of a thing with a fresh UID and ScriptName NULL, inserted right
    // after the original. Returns the new index.
    size_t duplicate(size_t index);
    size_t place(forge::thingplacer::Placement placement);
    // An enemy spawner: a MARKER_CREATURE_GENERATOR thing carrying the retail
    // CTCCreatureGenerator block (self-triggering when the hero comes within
    // `radius`, `activeLimit` creatures at once, -1 = unlimited) drawing from
    // the given CREATURE_GENERATION_FAMILY names. Position is map-local.
    size_t placeCreatureGenerator(const float pos[3], const std::vector<std::string>& families,
                                  float radius, int activeLimit, const std::string& scriptName = "");
    // A fishing spot: the retail MARKER_FISHING_SPOT thing (CTCPhysicsStandard +
    // an empty CTCFishingSpot block; BarrowFields/Bordello/Darkwood_9 shape). With
    // `reward` (an OBJECT_* def, e.g. OBJECT_MOONFISH) it also carries the retail
    // CTCContainerRewardHero so the first catch there is that item. Map-local.
    size_t placeFishingSpot(const float pos[3], const std::string& reward = "");
    // A creature (NPC, animal, guard...) as the retail AICreature thing: a
    // CTCPhysicsNavigator frame, targetable/talk blocks, VillageMember 0 for
    // villager defs, and the world-space InitialPos the engine reads (map
    // origin from FinalAlbion.wld). Position/forward are map-local.
    size_t placeCreature(const float pos[3], const float forward[2], const std::string& definition,
                         const std::string& scriptName = "");
    // A village: the retail `Village` thing (CTCVillage + enemy/opinion blocks,
    // ThingGamePersistent) for a VILLAGE_* def. Buildings, markers and
    // creatures join it through their CTCVillageMember.VillageUID.
    size_t placeVillage(const float pos[3], const std::string& definition, const std::string& scriptName = "");
    // A particle emitter: the retail PARTICLE_EMITTER_PLACEABLE thing (CTCDParticleEmitter,
    // IndependantObject TRUE) playing the named effects.big entry. Position is map-local.
    size_t placeEmitter(const float pos[3], const std::string& effectName, const std::string& scriptName = "");
    // Every Village thing in the map (index, uid, definition, script name).
    std::vector<ThingSummary> villages() const;
    // The village a thing belongs to (its CTCVillageMember.VillageUID; 0 = none / no block).
    uint64_t villageOf(size_t index) const;
    // Make a thing a member of the village with that uid (0 = leave, the block
    // stays with VillageUID 0 like retail). Adds the CTCVillageMember block
    // after CTCEditor when the thing has none. One undo step.
    void setVillageMember(size_t index, uint64_t villageUid);
    // WLD MapX/MapY of this map (0,0 when the map is not placed / no WLD).
    int worldX() const { return worldX_; }
    int worldY() const { return worldY_; }
    int worldSlot() const { return worldSlot_; }   // WLD map slot (GoToMapSlotRetailTransition), 0 = unknown
    void remove(size_t index);

    // Several commands as ONE undo step (multi-select move / delete / duplicate / paste):
    // the first pushUndo inside the batch snapshots, the rest are skipped. Nestable.
    void beginBatch();
    void endBatch();
    bool inBatch() const { return batchDepth_ > 0; }

    // A fragment: thing blocks with positions relative to their centroid (copy/paste,
    // presets). `paste` inserts each block at the end of the things with a fresh UID,
    // ScriptName NULL and its frame moved so the centroid lands on `at` (map-local; the
    // z of each thing keeps its offset from the centroid unless `dropToGround`, in which
    // case each is set on the terrain). Returns the new indices; one undo step.
    struct Fragment {
        struct Item { std::string block; Frame frame; bool hasFrame = false; };
        std::vector<Item> items;
        float centre[3] = {0, 0, 0};
        bool empty() const { return items.empty(); }
    };
    Fragment extract(const std::vector<size_t>& indices) const;
    std::vector<size_t> paste(const Fragment& fragment, const float at[3], bool dropToGround);

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    bool undo();
    bool redo();
    // Increments on every change (commands, undo, redo); the preview uses it
    // to know when to rebuild.
    uint64_t revision() const { return revision_; }
    bool dirty() const;   // cached per revision
    // Per-thing description of the differences against the opened text, by
    // UID: "added ...", "removed ...", "moved ...", "changed ...".
    std::vector<std::string> changes() const;

    // Writes the loose data/Levels/FinalAlbion/<map>.tng (creating the folders).
    bool saveLoose(const std::filesystem::path& gameRoot, std::string& error);
    // Replaces the map's entry in FinalAlbion.wad (backup FinalAlbion.wad.atlas-orig
    // is made once, the first time the archive is touched).
    bool deployWad(const std::filesystem::path& gameRoot, std::string& error);
    // Marks the current text as the saved baseline.
    void markSaved() { original_ = file_.serialize(); dirtyRev_ = ~0ull; }

    // ---- terrain (needs the .lev) ----
    bool hasTerrain() const { return level_ != nullptr && terrain_ != nullptr; }
    int cellsX() const { return level_ ? level_->cellsX() : 0; }
    int cellsY() const { return level_ ? level_->cellsY() : 0; }
    const TerrainState& terrain() const { return *terrain_; }
    // The state being drawn: the stroke's working copy while one is active.
    const TerrainState& liveTerrain() const { return stroke_ && working_ ? *working_ : *terrain_; }
    // Strokes: beginStroke snapshots for undo, applyBrush edits the working
    // copy (call every frame while the mouse is down), endStroke writes the
    // result into the .lev and closes the undo step.
    void beginStroke(const TerrainBrush& brush);
    void applyBrush(const TerrainBrush& brush, float dt);
    bool strokeActive() const { return stroke_; }
    void endStroke();
    // One undo step that sets vertex heights directly (the seam stitcher; no
    // brush). Out-of-range vertices are ignored. False when no terrain is loaded.
    struct VertexHeight { int x = 0, y = 0; float h = 0; };
    // Bilinear height of a terrain state at a map-local point (nullopt outside the grid).
    static std::optional<float> sampleHeight(const TerrainState& t, int cellsX, int cellsY, float x, float y);
    bool setVertexHeights(const std::vector<VertexHeight>& edits);
    uint64_t terrainRevision() const { return terrainRev_; }
    bool terrainDirty() const;
    bool themesDirty() const;          // ground-theme paint pending (needs the layer-mesh rebuild on deploy)
    uint64_t themeRevision() const { return themeRev_; }   // bumps when a theme stroke ends / undoes
    std::optional<float> terrainHeight(float x, float y) const;   // bilinear on the working copy
    // The LEV's ground-theme palette (256 fixed slots, ~30 named on a retail
    // map). Painting is limited to it, so any ENGINE_THEME of the game can be
    // added to a free slot: returns the slot (the existing one when the name is
    // already there), -1 when the palette is full or no terrain is loaded. One
    // undo step (the palette rides in TerrainState); written with the next
    // terrain save.
    int addGroundTheme(const std::string& name, uint32_t defIndex);
    int paletteSlotOf(const std::string& name) const;
    // Deploy: loose .lev, the FinalAlbion.wad entry, and the map's terrain chunk
    // inside FinalAlbion_RT.stb re-baked from the edited heights (same-size,
    // patched in place; one-time .atlas-orig backups). `notes` gets the bake log.
    // `library` (the install's ENGINE_THEME defs) is needed when themes were
    // painted: the layer meshes are regenerated from the LEV themes then.
    bool deployTerrain(const std::filesystem::path& gameRoot, std::vector<std::string>& notes, std::string& error,
                       const forge::terraintex::ThemeLibrary* library = nullptr,
                       const std::function<void(const std::string&)>& progress = {});
    // Writes the loose .lev. Cells whose walkable byte changed since the
    // navigation was last consistent get the retail CNavQuadTree patched in
    // place (only those cells; door nodes, stacked layers and the rest of the
    // tree stay as retail wrote them). `notes` gets a line about the patch.
    bool saveTerrainLoose(const std::filesystem::path& gameRoot, std::string& error,
                          std::vector<std::string>* notes = nullptr);

private:
    struct Snapshot { std::string tng; std::shared_ptr<const TerrainState> terrain; };
    void pushUndo();
    void restore(const Snapshot& s);
    Snapshot snapshot() const;
    void writeTerrainToLevel();
    std::string mapName_;
    int worldX_ = 0, worldY_ = 0, worldSlot_ = 0;
    forge::tng::File file_;
    std::string original_;
    std::vector<Snapshot> undo_, redo_;
    int batchDepth_ = 0;
    bool batchPushed_ = false;
    std::shared_ptr<const TerrainState> terrain_;        // committed state (immutable, shared with snapshots)
    std::shared_ptr<const TerrainState> savedTerrain_;   // baseline for terrainDirty()
    std::unique_ptr<TerrainState> working_;              // during a stroke
    std::unique_ptr<forge::terrain::Heightfield> hf_;    // during a stroke
    bool stroke_ = false;
    float flattenTarget_ = 0;
    uint64_t terrainRev_ = 0;
    uint64_t themeRev_ = 0;
    uint64_t revision_ = 0;
    mutable uint64_t dirtyRev_ = ~0ull;
    mutable bool dirtyValue_ = false;
    bool fromWad_ = false;
    std::filesystem::path loosePath_;
    std::shared_ptr<forge::lev::File> level_;
    std::vector<uint8_t> navWalkable_;   // walkable bytes the level's navigation tree agrees with
};

// Retail float spelling ("0.0", "-0.000102", "96.063965").
std::string formatFloat(float v);

// Every CREATURE_GENERATION_FAMILY name in the install's game.bin (the spawner picker).
std::vector<std::string> creatureFamilies(const std::filesystem::path& gameRoot, std::string& error);

} // namespace albion::editor
