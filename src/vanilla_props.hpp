#pragma once
// The vanilla editor's Thing Properties fields (FableWin GetPropertiesStruct per
// class, static RE -> docs/re_reference/vanilla_property_fields.tsv, generated into
// vanilla_props.inc): the label, category and widget each .tng key had there.

#include <string>
#include <span>

namespace albion::editor {

struct VanillaField {
    const char* ctc;        // "" = a field of the thing itself
    const char* key;        // the .tng key
    const char* category;   // the vanilla dialog tab
    const char* label;      // the vanilla caption
    const char* kind;       // bool | int | float | string | def | enum
    const char* defType;    // def picker: the def type (BRAIN, SOUND_THEME ...)
    const char* enumPairs;  // enum: "NAME=value;NAME=value"
    bool hasRange;
    double min, max, step;
    const char* confidence; // H / M
};

// The vanilla field for a CTC block's key (case-sensitive block, case-insensitive key); nullptr if none.
const VanillaField* vanillaField(const std::string& ctc, const std::string& key);
// Read-only generated metadata; entries describe widgets, not default values.
std::span<const VanillaField> vanillaFields();

} // namespace albion::editor
