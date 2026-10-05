#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace forge::modchoices {

struct FileContent {
    bool present = false;
    uint64_t bytes = 0;
    std::string sha256;
    bool operator==(const FileContent&) const = default;
};

// A review records the complete set of raw-file versions shown by the last
// conflict check. Contributor order does not affect content identity.
struct FileReview {
    std::string winner;
    FileContent baseline;
    std::map<std::string, FileContent> versions;
    bool operator==(const FileReview&) const = default;
};

struct Choices {
    std::map<std::string, std::string> picks;
    std::map<std::string, FileReview> fileReviews;
};

// Legacy key<TAB>winner / key=winner rows remain supported. Optional review
// annotations are comments, so older readers still understand the choices.
// TNG/QST paths use Windows file identity; entry/quest identities keep their case.
std::string canonicalKey(std::string_view key);
// File-path identity: slash/ASCII aliases plus Windows ordinal BMP casing.
// Supplementary characters retain their spelling, matching Windows comparison.
std::string filePathKey(std::string_view path);
Choices parse(std::string_view text);
Choices read(const std::filesystem::path& path, bool allowMissing = false);
std::string serialize(const Choices& choices);
FileContent fileContent(const std::filesystem::path& path);

} // namespace forge::modchoices
