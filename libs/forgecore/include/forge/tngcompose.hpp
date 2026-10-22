#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace forge::tng {
struct TextLayer { std::string label, text; };
struct ThingConflict {
    std::string key, winner;
    std::vector<std::string> mods;
    std::vector<std::string> removalMods;
    bool overridden=false, choiceUnavailable=false;
};
struct TextComposition {
    std::string text;
    std::vector<ThingConflict> conflicts;
    // Metadata outside objects composes per existing/new section. Differing
    // changes require a caller's coarser section/map choice for explicit control.
    bool metadataConflict=false;
    size_t deleted=0;
};
struct CompositionOptions {
    // A replacement supplies output content while baseline still determines
    // which later records actually changed. Unchanged full copies cannot
    // resurrect objects omitted by that replacement.
    std::optional<std::string> seed;
    std::map<std::string,std::vector<std::string>> removals;
};
// Baseline-aware union of objects, keyed by UID and retaining their section
// ownership. Omission never deletes. Selected object blocks and untouched bytes
// survive verbatim. Unknown metadata resolves independently per section; absent
// sections are partial inputs. Choices address object keys (uid:<value>).
TextComposition composeText(const std::string& baseline,
                            const std::vector<TextLayer>& layers,
                            const std::map<std::string,std::string>& picks={},
                            bool reviewChoices=false,
                            const CompositionOptions& options={});
} // namespace forge::tng
