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
        const auto legacy = mc::parse("# comment\r\nfile:data/a = Pack A \r\nrecord\tvanilla\nignored\n");
        require(legacy.picks.at("file:data/a") == "Pack A" && legacy.picks.at("record") == "vanilla",
                "legacy choice parsing changed");
        require(legacy.fileReviews.empty(), "legacy picks gained reviews");
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
