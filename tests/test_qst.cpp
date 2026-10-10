#include "forge/qst.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        using forge::qst::File;
        const std::string prefix = "// AddQuest(\"Drop\", TRUE); stays a comment\r\n";
        const std::string gap = "\t/* retained gap */\r\n";
        const std::string keep = "AddQuest(\"Keep\",\tTRUE);";
        const std::string test = "AddTestQuest(\"Drop\", \"HSP\", 2, \"Display\", \"ini\", \"End\", \"Card\");";
        const std::string tail = "\r\nunknown trailing text\r\n";
        const auto raw = prefix + "AddQuest(\"Drop\", FALSE);" + gap + keep + "\r\n" +
                         test + "\r\nAddQuest(\"Drop\", TRUE);" + tail;
        auto file = File::parseText(raw);
        require(file.serialize() == raw, "initial round trip changed bytes");
        require(file.removeQuest("Absent") == 0 && file.serialize() == raw,
                "absent removal changed bytes");
        require(file.removeQuest("Drop") == 2, "duplicate registrations were not removed");
        const auto expected = prefix + gap + keep + "\r\n" + test + "\r\n" + tail;
        require(file.serialize() == expected, "removal changed gaps, comments or test registration");
        require(file.questCount() == 1 && file.testQuestCount() == 1 && !file.findQuest("Drop"),
                "removed registrations remain in statement accessors");
        require(file.removeQuest("Drop") == 0 && file.serialize() == expected,
                "repeated removal changed bytes");
        require(file.setQuestActive("Keep", false), "surviving statement index is stale");
        auto changed = expected;
        changed.replace(changed.find(keep), keep.size(), "AddQuest(\"Keep\",\tFALSE);");
        require(file.serialize() == changed, "post-removal flag edit changed unrelated bytes");
        file.addQuest("Drop", true);
        require(file.findQuest("Drop") && file.findQuest("Drop")->active(),
                "post-removal append failed");
        require(File::parseText(file.serialize()).questCount() == 2,
                "post-removal document does not parse");

        const auto base = File::parseText("AddQuest(\"Existing\", FALSE);\r\n");
        const auto a = File::parseText("AddQuest(\"Existing\", TRUE);\nAddQuest(\"Added\", TRUE);\n");
        const auto b = File::parseText("AddQuest(\"Existing\", FALSE);\nAddQuest(\"Added\", FALSE);\n");
        const std::vector<forge::qst::MergeInput> one{{"A", &a}};
        const auto vanilla = forge::qst::merge(base, one, {{"Existing", "vanilla"}, {"Added", "vanilla"}});
        require(vanilla.merged.serialize() == base.serialize() && vanilla.conflicts.size() == 2,
                "single-contributor vanilla choice did not restore flag and absence");
        const auto selected = forge::qst::merge(base, one, {{"Existing", "A"}});
        require(selected.merged.findQuest("Existing")->active(), "single source choice was ignored");
        const auto unavailable = forge::qst::merge(base, one, {{"Added", "Removed"}});
        require(unavailable.merged.findQuest("Added")->active() &&
                unavailable.conflicts.front().choiceUnavailable &&
                !unavailable.conflicts.front().overridden,
                "unavailable low-level fallback was presented as an override");
        const auto multiple = forge::qst::merge(base, {{"A", &a}, {"B", &b}}, {{"Added", "B"}});
        require(!multiple.merged.findQuest("Added")->active() &&
                multiple.merged.findQuest("Existing")->active(), "per-quest source choice changed another quest");
        const auto inactive = forge::qst::merge(base, {{"B", &base}}, {{"Existing", "Removed"}, {"Unknown", "Removed"}});
        require(inactive.merged.serialize() == base.serialize() && inactive.conflicts.empty(),
                "inactive choices gained contributions");
        std::cout << "QST removal preservation and contributor choices pass\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
