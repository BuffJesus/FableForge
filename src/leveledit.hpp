#pragma once
// albion::editor -- the editable level document behind the Albion Atlas
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
#include <optional>
#include <string>
#include <vector>

#include "forge/lev.hpp"
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
    void setProperty(size_t index, const std::string& key, const std::string& value);
    // Copy of a thing with a fresh UID and ScriptName NULL, inserted right
    // after the original. Returns the new index.
    size_t duplicate(size_t index);
    size_t place(forge::thingplacer::Placement placement);
    void remove(size_t index);

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

private:
    void pushUndo();
    void restore(const std::string& text);
    std::string mapName_;
    forge::tng::File file_;
    std::string original_;
    std::vector<std::string> undo_, redo_;
    uint64_t revision_ = 0;
    mutable uint64_t dirtyRev_ = ~0ull;
    mutable bool dirtyValue_ = false;
    bool fromWad_ = false;
    std::filesystem::path loosePath_;
    std::shared_ptr<forge::lev::File> level_;
};

// Retail float spelling ("0.0", "-0.000102", "96.063965").
std::string formatFloat(float v);

} // namespace albion::editor
