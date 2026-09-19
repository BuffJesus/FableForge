#pragma once
// Reader for the named+typed definition schema
// (FableTLC ghidra_out/def_schema.json, mirrored to docs/re_reference).
// For each of 269 def types it gives the game.bin field order (= the
// CPersistContext serialization order recovered from the FableWin donor Transfer
// methods): field name, type, and — where donor/retail field counts agree — the
// retail memory offset. Types use the decoded persist-helper mangling
// (int32/uint32/float/bool/uint8/CCharString/C2DVector/enum Exxx/vector<Cxxx>...).

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace forge::defschema {

struct Field {
    std::string name;   // empty if the donor stored it in an un-inlined global
    std::string type;
    int donorOffset = -1;
    std::optional<int> retailOffset;  // set only when counts aligned
};

struct DefType {
    std::string name;
    std::string transferAddr;   // donor Transfer address (hex)
    bool retailOffsets = false; // true if fields carry aligned retail offsets
    int prefixLen = -1;         // explicit untagged base-prefix byte count
                                // (-1 = locate empirically from first field tag);
                                // needed for types whose Transfer writes zero
                                // fields (THING_GROUP / ENGINE_THEME_GROUP: the
                                // whole 5-byte payload is base prefix)
    std::vector<Field> fields;  // in game.bin serialization order
};

class Schema {
public:
    static Schema load(const std::filesystem::path& path);
    static Schema loadText(const std::string& json,
                           const std::string& sourceName = {});

    const std::vector<DefType>& defs() const { return defs_; }
    const DefType* find(std::string_view name) const;

private:
    std::vector<DefType> defs_;
};

} // namespace forge::defschema
