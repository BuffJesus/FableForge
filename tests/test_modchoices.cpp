#include "forge/modchoices.hpp"
#include "forge/modorder.hpp"
#include "forge/temporarydirectory.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace mc = forge::modchoices;
static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class F> static void refuses(F action) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("expected refusal");
}

int main() {
    try {
        for (const std::string name : {"", "vanilla", "VaNiLlA", " Pack", "Pack ", "Pack\tA", "Pack\nA", "Pack\rA", "Pack\x7f"})
            refuses([&] { forge::modorder::requireLabel(name); });
        for (const std::string name : {"Pack A", "Vanilla Plus", "123", "Pack=One"})
            forge::modorder::requireLabel(name);
        forge::modorder::Order invalidOrder;
        forge::modorder::Entry invalidEntry;
        invalidEntry.name = "vanilla";
        invalidOrder.mods.push_back(invalidEntry);
        refuses([&] { (void)forge::modorder::buildLabels(invalidOrder); });
        invalidOrder.mods.front().enabled = false;
        require(forge::modorder::buildLabels(invalidOrder).empty(), "disabled legacy label blocked recovery");
        refuses([&] { forge::modorder::requireLabels({"Pack", "Pack"}); });
        refuses([&] { forge::modorder::requireLabels({"Pack", "pAcK"}); });
        forge::modorder::requireLabels({"Pack A", "Pack B"});
        const auto legacy = mc::parse("# comment\r\nfile:data/a = Pack A \r\nrecord\tvanilla\nignored\n");
        require(legacy.picks.at("file:data/a") == "Pack A" && legacy.picks.at("record") == "vanilla",
                "legacy choice parsing changed");
        require(legacy.fileReviews.empty(), "legacy picks gained reviews");
        require(mc::canonicalKey("tng:FinalAlbion\\Arena.TNG|uid:AbC") ==
                "tng:finalalbion/arena.tng|uid:AbC", "TNG path or thing identity changed incorrectly");
        require(mc::canonicalKey("qst:FinalAlbion/Test.QST|ExistingQuest") ==
                "qst:finalalbion/test.qst|ExistingQuest", "QST path or quest identity changed incorrectly");
        for (const char* key : {"TEXT_RECORD", "text:English|SHARED", "tng:NoSeparator", "field:[1,2]"})
            require(mc::canonicalKey(key) == key, "non-level or incomplete choice changed");
        const auto aliases = mc::parse("tng:FinalAlbion\\Arena.TNG|uid:AbC\tPack A\n"
            "tng:finalalbion/arena.tng|uid:AbC\tPack B\n"
            "qst:FinalAlbion/TEST.QST|ExistingQuest\tPack MixedCase\n"
            "qst:FinalAlbion/test.qst|existingquest\tvanilla\n");
        require(aliases.picks.size() == 3 &&
                aliases.picks.at("tng:finalalbion/arena.tng|uid:AbC") == "Pack B" &&
                aliases.picks.at("qst:finalalbion/test.qst|ExistingQuest") == "Pack MixedCase" &&
                aliases.picks.at("qst:finalalbion/test.qst|existingquest") == "vanilla",
                "level aliases, last-row precedence or case-sensitive quest identities changed");
        require(mc::parse(mc::serialize(aliases)).picks == aliases.picks,
                "canonical level choices did not survive serialization");
        mc::Choices choices = legacy;
        mc::FileReview review;
        review.winner = "Pack A";
        review.baseline = {true, 3, forge::modorder::sha256OfBytes("old")};
        review.versions.emplace("Pack A", mc::FileContent{true, 3, forge::modorder::sha256OfBytes("new")});
        review.versions.emplace("Pack B", mc::FileContent{true, 0, forge::modorder::sha256OfBytes("")});
        choices.fileReviews.emplace("file:data/a", review);
        const auto serialized = mc::serialize(choices);
        auto decoded = mc::parse(serialized);
        require(decoded.picks == choices.picks && decoded.fileReviews == choices.fileReviews,
                "review round trip changed identities");
        require(mc::serialize(decoded) == serialized, "review serialization is not deterministic");
        auto mixedCase = serialized;
        const auto annotationKey = mixedCase.find("\"key\":\"file:data/a\"");
        require(annotationKey != std::string::npos, "missing serialized review key");
        mixedCase.replace(annotationKey, std::string("\"key\":\"file:data/a\"").size(),
                          "\"key\":\"file:Data/A\"");
        require(mc::parse(mixedCase).fileReviews == decoded.fileReviews,
                "mixed-case review did not protect the canonical file choice");
        refuses([&] { mc::parse(serialized + mixedCase.substr(mixedCase.find("# forge-file-review-v1 "))); });
        auto reordered = review;
        reordered.versions.clear();
        reordered.versions.emplace("Pack B", review.versions.at("Pack B"));
        reordered.versions.emplace("Pack A", review.versions.at("Pack A"));
        require(reordered == review, "contributor ordering changed the review identity");
        reordered.versions.at("Pack A").sha256 = forge::modorder::sha256OfBytes("NEW");
        require(reordered != review, "same-sized contributor changes were ignored");
        reordered = review; reordered.baseline = {};
        require(reordered != review, "baseline presence changes were ignored");
        choices.picks.erase("file:data/a");
        require(mc::parse(mc::serialize(choices)).fileReviews.empty(), "cleared choice retained its review");
        refuses([&] { mc::parse(serialized + serialized.substr(serialized.find("# forge-file-review-v1 "))); });
        for (const char* invalid : {
            "# forge-file-review-v1 {}\n",
            "# forge-file-review-v1 {\"key\":\"record\",\"winner\":\"A\"}\n",
            "# forge-file-review-v1 {\"key\":\"file:x\",\"winner\":\"A\",\"baseline\":{\"present\":true,\"bytes\":0,\"sha256\":\"bad\"},\"versions\":{}}\n",
            "# forge-file-review-v1 {\"key\":\"file:x\",\"winner\":\"A\",\"baseline\":{\"present\":false,\"bytes\":1,\"sha256\":\"\"},\"versions\":{}}\n",
            "# forge-file-review-v1 {\"key\":\"file:x\",\"winner\":\"A\",\"baseline\":{\"present\":false,\"bytes\":-1,\"sha256\":\"\"},\"versions\":{}}\n"})
            refuses([&] { mc::parse(invalid); });
        forge::TemporaryDirectory scratch(std::filesystem::temp_directory_path(), "modchoices-test-");
        const auto path = scratch.path() / "choices.txt";
        require(mc::read(path, true).picks.empty(), "missing optional choices refused");
        refuses([&] { mc::read(path); });
        refuses([&] { mc::read(scratch.path(), true); });
        { std::ofstream out(path, std::ios::binary); out << serialized; }
        require(mc::read(path).fileReviews == decoded.fileReviews, "file read changed reviews");
        require(mc::fileContent(path).sha256 == forge::modorder::sha256OfBytes(serialized), "file hash mismatch");
        require(!mc::fileContent(scratch.path() / "missing").present, "missing file has content");
        std::cout << "mod choice review serialization and identity checks pass\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
