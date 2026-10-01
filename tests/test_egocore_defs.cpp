#include "forge/egocore.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void check(bool good, const char* message) { if (!good) throw std::runtime_error(message); }
int main() {
    try {
        const std::string oldBlock = "#definition OBJECT FIRST\r\n Value 1;\r\n#end_definition";
        const std::string replacement = "#definition OBJECT FIRST\n Value 2;\n#end_definition";
        const std::string added = "#definition_template OBJECT SECOND\n Value 3;\n#end_definition";
        const std::string prefix = "// untouched prefix\r\n";
        const std::string suffix = "\r\n// untouched suffix\r\n";
        const std::string base = prefix + oldBlock + suffix;
        forge::egocore::Report report;
        std::vector<std::string> addedBlocks;
        const auto result = forge::egocore::mergeDefText(base,
            "/* #definition OBJECT IGNORED */\n" + replacement + "\n" + added, report, &addedBlocks);
        check(result == prefix + replacement + suffix + "\n\n" + added, "valid merge changed unrelated text");
        check(report.blocksReplaced == 1 && report.blocksAdded == 1 && addedBlocks == std::vector<std::string>{added}, "valid merge counts differ");
        const std::vector<std::string> invalid = {
            "#definition OBJECT FIRST\n Value 2;\n",
            "#definition OBJECT FIRST\n Value 2;\n" + added,
            "#definition OBJECT\n#end_definition",
        };
        for (const auto& text : invalid) {
            bool failed = false;
            try { forge::egocore::Report r; forge::egocore::mergeDefText(base, text, r); }
            catch (const std::exception&) { failed = true; }
            check(failed, "malformed definition override was silently accepted");
        }
        bool failed = false;
        try { forge::egocore::Report r; forge::egocore::mergeDefText(invalid[0], replacement, r); }
        catch (const std::exception&) { failed = true; }
        check(failed, "malformed baseline definition was silently retained");
        forge::egocore::Report comments;
        check(forge::egocore::mergeDefText(base, "// #definition OBJECT IGNORED\n/* #definition OBJECT HIDDEN */", comments) == base,
              "comment directives were parsed as overrides");
        const std::string quoted = "#definition OBJECT FIRST\n Text \"#end_definition #definition // /*\";\n#end_definition";
        forge::egocore::Report strings;
        check(forge::egocore::mergeDefText(base, quoted, strings) == prefix + quoted + suffix,
              "quoted directive text changed block boundaries");
        std::cout << "EgoCore definition validation and text preservation: PASS\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
