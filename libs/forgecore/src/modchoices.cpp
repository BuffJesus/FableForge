#include "forge/modchoices.hpp"
#include "forge/modorder.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include "nlohmann/json.hpp"
#ifdef _WIN32
#include <windows.h>
#endif

namespace forge::modchoices {
namespace {
using nlohmann::json;
constexpr std::string_view prefix = "# forge-file-review-v1 ";

FileContent content(const json& value) {
    if (!value.is_object() || !value.at("present").is_boolean() ||
        !value.at("bytes").is_number_unsigned() || !value.at("sha256").is_string())
        throw std::runtime_error("invalid file review content");
    FileContent result{value.at("present").get<bool>(), value.at("bytes").get<uint64_t>(),
                       value.at("sha256").get<std::string>()};
    if (result.present) {
        if (result.sha256.size() != 64 || !std::all_of(result.sha256.begin(), result.sha256.end(),
            [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
            throw std::runtime_error("invalid file review SHA256");
    } else if (result.bytes != 0 || !result.sha256.empty())
        throw std::runtime_error("absent file review has content");
    return result;
}

json content(const FileContent& value) {
    return {{"present", value.present}, {"bytes", value.bytes}, {"sha256", value.sha256}};
}
} // namespace

std::string filePathKey(std::string_view path) {
    std::string result(path);
#ifdef _WIN32
    if (std::any_of(result.begin(), result.end(), [](unsigned char c) { return c >= 128; })) {
        auto wide = std::filesystem::path(result).native();
        for (auto& c : wide) {
            if (c < 128 || (c >= 0xd800 && c <= 0xdfff)) continue;
            wchar_t mapped = c;
            const int needed = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE,
                &c, 1, nullptr, 0, nullptr, nullptr, 0);
            if (!needed) throw std::runtime_error("cannot map Windows file-path case");
            if (needed != 1) continue;
            if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE,
                    &c, 1, &mapped, 1, nullptr, nullptr, 0) != 1)
                throw std::runtime_error("cannot map Windows file-path case");
            const int comparison = CompareStringOrdinal(&c, 1, &mapped, 1, TRUE);
            if (!comparison) throw std::runtime_error("cannot compare Windows file-path case");
            if (comparison == CSTR_EQUAL) c = mapped;
        }
        const auto utf8 = std::filesystem::path(wide).generic_u8string();
        result.assign(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    }
#endif
    for (auto& c : result) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    }
    return result;
}

std::string canonicalKey(std::string_view key) {
    if (key.starts_with("file:")) return "file:" + filePathKey(key.substr(5));
    std::string result(key);
    const size_t start = key.starts_with("tng:") || key.starts_with("qst:") ? 4 : 0;
    const size_t end = key.find('|', start);
    if (!start || end == std::string_view::npos) return result;
    result.replace(start, end - start, filePathKey(key.substr(start, end - start)));
    return result;
}

Choices parse(std::string_view text) {
    Choices result;
    std::istringstream input{std::string(text)};
    std::string line;
    while (std::getline(input, line)) {
        if (line.starts_with(prefix)) {
            try {
                const auto value = json::parse(line.substr(prefix.size()));
                auto key = value.at("key").get<std::string>();
                if (!key.starts_with("file:") || key.size() <= 5)
                    throw std::runtime_error("file review key is not a file choice");
                // File choices use case-insensitive paths in the composer. A
                // differently cased annotation must protect that same choice.
                key = canonicalKey(key);
                FileReview review;
                review.winner = value.at("winner").get<std::string>();
                if (review.winner.empty() || review.winner.find_first_of("\r\n\t") != std::string::npos)
                    throw std::runtime_error("invalid file review winner");
                review.baseline = content(value.at("baseline"));
                const auto& versions = value.at("versions");
                if (!versions.is_object()) throw std::runtime_error("invalid file review versions");
                for (const auto& [name, version] : versions.items()) {
                    if (name.empty()) throw std::runtime_error("empty file review contributor");
                    review.versions.emplace(name, content(version));
                }
                if (!result.fileReviews.emplace(key, std::move(review)).second)
                    throw std::runtime_error("duplicate file review: " + key);
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string("cannot read file choice review: ") + e.what());
            }
            continue;
        }
        if (line.empty() || line[0] == '#') continue;
        auto separator = line.find('\t');
        if (separator == std::string::npos) separator = line.find('=');
        if (separator == std::string::npos) continue;
        auto key = line.substr(0, separator), winner = line.substr(separator + 1);
        while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) key.pop_back();
        while (!winner.empty() && std::isspace(static_cast<unsigned char>(winner.front()))) winner.erase(winner.begin());
        while (!winner.empty() && std::isspace(static_cast<unsigned char>(winner.back()))) winner.pop_back();
        if (!key.empty()) result.picks[canonicalKey(key)] = winner;
    }
    return result;
}

Choices read(const std::filesystem::path& path, bool allowMissing) {
    if (allowMissing && !std::filesystem::exists(path)) return {};
    if (!std::filesystem::is_regular_file(path))
        throw std::runtime_error("conflict choices are not a regular file: " + path.string());
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open conflict choices: " + path.string());
    std::ostringstream text;
    text << input.rdbuf();
    if (input.bad() || text.bad()) throw std::runtime_error("cannot read conflict choices: " + path.string());
    return parse(text.str());
}

std::string serialize(const Choices& choices) {
    std::ostringstream output;
    output << "# FableForge: the winners picked on the Mods tab; forge-tools mods deploy reads this file\n";
    for (const auto& [key, winner] : choices.picks) output << key << '\t' << winner << '\n';
    for (const auto& [key, review] : choices.fileReviews) {
        if (!choices.picks.contains(key)) continue;
        json versions = json::object();
        for (const auto& [name, version] : review.versions) versions[name] = content(version);
        output << prefix << json{{"key", key}, {"winner", review.winner},
            {"baseline", content(review.baseline)}, {"versions", versions}}.dump() << '\n';
    }
    const auto result = output.str();
    (void)parse(result); // malformed review metadata must refuse before publication
    return result;
}

FileContent fileContent(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) return {};
    return {true, std::filesystem::file_size(path), modorder::sha256Of(path)};
}
} // namespace forge::modchoices
