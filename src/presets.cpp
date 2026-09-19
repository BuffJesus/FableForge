#include "presets.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

namespace albion::editor {
namespace fs = std::filesystem;

namespace {
constexpr const char* kExt = ".preset.tng";

bool isPresetFile(const fs::path& p) {
    const std::string n = p.filename().string();
    return n.size() > std::strlen(kExt) && n.compare(n.size() - std::strlen(kExt), std::strlen(kExt), kExt) == 0;
}

std::string readAll(const fs::path& p, std::string& error) {
    std::ifstream in(p, std::ios::binary);
    if (!in) { error = "cannot open " + p.string(); return {}; }
    std::stringstream ss; ss << in.rdbuf();
    return ss.str();
}

// "// FableForge preset: <name>" then "// <description>" (both optional)
void readHeader(const std::string& text, std::string& name, std::string& description) {
    std::istringstream in(text);
    std::string line;
    int n = 0;
    while (std::getline(in, line) && n < 2) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("//", 0) != 0) break;
        std::string body = line.substr(2);
        while (!body.empty() && body.front() == ' ') body.erase(body.begin());
        const std::string tag = "FableForge preset:", oldTag = "Albion Atlas preset:";   // presets saved before the rename
        const std::string* hit = body.rfind(tag, 0) == 0 ? &tag : body.rfind(oldTag, 0) == 0 ? &oldTag : nullptr;
        if (n == 0 && hit) { name = body.substr(hit->size()); while (!name.empty() && name.front() == ' ') name.erase(name.begin()); }
        else if (n == 0) name = body;
        else description = body;
        ++n;
    }
}
}  // namespace

std::string presetSlug(const std::string& name) {
    std::string s;
    for (const char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '-' || c == '_') s += c;
        else if (c == ' ') s += '_';
    }
    if (s.empty()) s = "preset";
    return s;
}

std::vector<PresetInfo> listPresets(const std::vector<fs::path>& folders) {
    std::vector<PresetInfo> out;
    bool user = false;
    for (const fs::path& dir : folders) {
        std::error_code ec;
        if (fs::is_directory(dir, ec)) {
            std::vector<fs::path> files;
            for (const auto& e : fs::directory_iterator(dir, ec)) if (e.is_regular_file(ec) && isPresetFile(e.path())) files.push_back(e.path());
            std::sort(files.begin(), files.end());
            for (const fs::path& f : files) {
                PresetInfo p;
                p.file = f;
                p.user = user;
                std::string err;
                const std::string text = readAll(f, err);
                readHeader(text, p.name, p.description);
                if (p.name.empty()) { std::string stem = f.filename().string(); stem.resize(stem.size() - std::strlen(kExt)); p.name = stem; }
                size_t pos = 0;
                while ((pos = text.find("NewThing ", pos)) != std::string::npos) { ++p.things; pos += 9; }
                out.push_back(std::move(p));
            }
        }
        user = true;   // the first folder is the shipped one, every later one is the user's
    }
    return out;
}

bool loadPreset(const fs::path& file, Document::Fragment& out, std::string& error) {
    const std::string text = readAll(file, error);
    if (text.empty()) { if (error.empty()) error = "empty preset " + file.string(); return false; }
    Document doc;
    if (!doc.openText(file.stem().string(), text, error)) return false;
    std::vector<size_t> all;
    for (size_t i = 0; i < doc.thingCount(); ++i) all.push_back(i);
    out = doc.extract(all);
    if (out.empty()) { error = "no things in " + file.string(); return false; }
    return true;
}

bool savePreset(const fs::path& file, const std::string& name, const std::string& description,
                const Document::Fragment& fragment, std::string& error) {
    if (fragment.empty()) { error = "nothing to save"; return false; }
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) { error = "cannot write " + file.string(); return false; }
    out << "// FableForge preset: " << name << "\r\n";
    out << "// " << (description.empty() ? std::to_string(fragment.items.size()) + " things" : description) << "\r\n";
    out << "Version 2;\r\nXXXSectionStart NULL;\r\n";
    for (const auto& item : fragment.items) {
        out << item.block;
        if (item.block.empty() || item.block.back() != '\n') out << "\r\n";
    }
    out << "XXXSectionEnd;\r\n";
    return bool(out);
}

}  // namespace albion::editor
