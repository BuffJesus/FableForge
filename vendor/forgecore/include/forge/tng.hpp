#pragma once
// Parser for Fable TLC .tng ("things") files — the text format placing every
// scripted entity in a level. Grammar observed from shipping FinalAlbion TNGs:
//   Version 2;
//   XXXSectionStart NULL;
//   NewThing <Type>;
//     Key Value;
//     StartCTC<Name>; ... EndCTC<Name>;
//   EndThing;
//   XXXSectionEnd;
// The parser keeps the exact raw lines so an unmodified file serializes back
// byte-identically (required for a safe editor). Mutations edit only the raw
// lines they touch; every other byte round-trips unchanged.

#include <cstddef>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace forge::tng {

inline constexpr size_t kNoLine = std::numeric_limits<size_t>::max();

struct Property {
    std::string key;
    std::string value; // rest of the line after the key, ';' stripped
    size_t line = kNoLine; // raw-line index, maintained by File
};

// A StartCTC<Name>/EndCTC<Name> component block inside a thing.
struct CtcBlock {
    std::string name; // e.g. CTCPhysicsStandard
    std::vector<Property> properties;
    size_t startLine = kNoLine; // StartCTC line, maintained by File
    size_t endLine = kNoLine;   // EndCTC line, maintained by File
};

struct Thing {
    std::string type; // e.g. Marker, Object, Thing, AICreature
    std::vector<Property> properties; // outside any CTC block
    std::vector<CtcBlock> ctcBlocks;
    size_t startLine = kNoLine; // NewThing line, maintained by File
    size_t endLine = kNoLine;   // EndThing line, maintained by File

    // First top-level property value for key (case-insensitive), if present.
    std::optional<std::string> find(std::string_view key) const;
    std::string scriptName() const;
    std::string definitionType() const; // quotes stripped

    const CtcBlock* findCtc(std::string_view name) const;
};

class File {
public:
    static File parse(const std::filesystem::path& path);
    static File parseText(std::string text, std::string sourceName = {});

    const std::vector<Thing>& things() const { return things_; }
    const std::string& source() const { return source_; }

    // Byte-identical to the parsed input while unmodified.
    std::string serialize() const;

    // --- Mutations (M3). Each edits only the raw lines it touches, then
    // re-derives the document model, so untouched content stays byte-exact.

    // Replace the value of a top-level property (key matched case-insensitively,
    // original key spelling and line terminator preserved), or insert a new
    // "Key Value;" line just before EndThing when the key is absent.
    void setThingProperty(size_t thingIndex, std::string_view key,
                          std::string_view value);
    // Remove a top-level property line. Returns false if the key is absent.
    bool removeThingProperty(size_t thingIndex, std::string_view key);

    // Same, inside the named CTC block. Throws if the block is absent.
    void setCtcProperty(size_t thingIndex, std::string_view ctcName,
                        std::string_view key, std::string_view value);
    bool removeCtcProperty(size_t thingIndex, std::string_view ctcName,
                           std::string_view key);

    // Append a serialized copy of `thing` (its line fields are ignored) before
    // XXXSectionEnd (or at EOF), blank-line separated. Returns the new index.
    size_t addThing(const Thing& thing);
    // Remove the thing's NewThing..EndThing lines plus one adjacent blank line.
    void removeThing(size_t thingIndex);
    // Insert a VERBATIM NewThing..EndThing text block just before the named
    // section's XXXSectionEnd (blank-line separated). Unlike addThing this does
    // not round-trip through the document model, so retail field order --
    // notably the trailing `Health` line that follows the CTC blocks -- is
    // preserved exactly as written. `sectionName` matches the value on
    // `XXXSectionStart <name>;` (case-insensitive); pass "NULL" for the main
    // section. When the file has no sections (the retail minimal empty-TNG
    // form), creates the requested first section. Throws when another section
    // exists but the requested one is absent. Returns the new thing index.
    size_t insertThingBlock(std::string_view sectionName, std::string blockText);

    // The exact NewThing..EndThing text of a thing (its own raw lines, verbatim).
    std::string thingBlockText(size_t thingIndex) const;
    // Name on the XXXSectionStart enclosing the thing ("NULL" when the file has
    // no sections).
    std::string sectionOf(size_t thingIndex) const;
    // Insert a VERBATIM block so that it becomes thing number `thingIndex`
    // (before the thing currently there; `thingIndex == things().size()`
    // appends to the last section). Editors use it to undo a removal at the
    // original position. Returns the new thing's index.
    size_t insertThingBlockBefore(size_t thingIndex, std::string blockText);

    // Replace a thing's lines in place with a block reconstructed from `thing`
    // (its line fields ignored). Keeps the thing's position; every other thing
    // stays byte-exact. Used by cross-mod thing-level merge.
    void replaceThing(size_t thingIndex, const Thing& thing);

private:
    const Thing& thingAt(size_t index) const;
    // Reconstruct a thing's NewThing..EndThing text block from its model.
    std::string serializeThingBlock(const Thing& thing) const;
    void replaceValueOnLine(size_t line, std::string_view value);
    void insertLine(size_t line, std::string text);
    // Re-parse the current raw lines to refresh things_ and all line indices.
    void reindex();

    std::string source_;
    std::string lineTerminator_ = "\r\n"; // dominant terminator, from parse
    std::vector<std::string> rawLines_; // exact bytes incl. terminators
    std::vector<Thing> things_;
};

} // namespace forge::tng
