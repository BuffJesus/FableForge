#include "forge/lev.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace fs = std::filesystem;

namespace forge::lev {
namespace {

constexpr size_t kCellSize = 21;
constexpr uint16_t kLevVersion = 6404;
constexpr size_t kPaletteSize = 33792;

// Ground-theme palette: 256 x { char name[128]; u32 defIndex } immediately after
// the 25-byte LEVHeader and the 22-byte LEVMapHeader.
constexpr size_t kThemeTableOffset = 25 + 22;
constexpr size_t kThemeEntrySize = 128 + 4;
constexpr size_t kThemeEntryCount = 256;

class Reader {
public:
    explicit Reader(const fs::path& path) : file_(path, std::ios::binary) {
        if (!file_.is_open()) {
            throw std::runtime_error("lev: cannot open " + path.string());
        }
    }

    void read(void* out, size_t count, const char* what) {
        file_.read(static_cast<char*>(out), static_cast<std::streamsize>(count));
        if (!file_) {
            throw std::runtime_error(std::string("lev: truncated while reading ") + what);
        }
    }

    template <typename T>
    T readValue(const char* what) {
        T value{};
        read(&value, sizeof(T), what);
        return value;
    }

    std::string readCountedString(const char* what) {
        const uint32_t length = readValue<uint32_t>(what);
        if (length > 4096) {
            throw std::runtime_error(std::string("lev: implausible string length in ") + what);
        }
        std::string value(length, '\0');
        read(value.data(), length, what);
        return value;
    }

    void seek(uint32_t offset) { file_.seekg(offset, std::ios::beg); }
    uint32_t tell() { return static_cast<uint32_t>(file_.tellg()); }
    void skip(size_t count) { file_.seekg(static_cast<std::streamoff>(count), std::ios::cur); }

private:
    std::ifstream file_;
};

} // namespace

File File::open(const fs::path& path) {
    Reader reader(path);
    File result;
    result.source_ = path.filename().string();

    // LEVHeader, 25 bytes
    uint8_t header[25];
    reader.read(header, sizeof(header), "header");
    uint16_t version;
    std::memcpy(&version, header + 4, 2);
    if (version != kLevVersion) {
        throw std::runtime_error("lev: bad version " + std::to_string(version) +
                                 " in " + result.source_);
    }
    uint32_t navOffset;
    std::memcpy(&navOffset, header + 21, 4);
    result.navOffset_ = navOffset;

    // LEVMapHeader, 22 bytes
    uint8_t mapHeader[22];
    reader.read(mapHeader, sizeof(mapHeader), "map header");
    if (mapHeader[1] != 8) {
        throw std::runtime_error("lev: bad map version in " + result.source_);
    }
    const uint8_t subVersion = mapHeader[4];
    if (subVersion != 8 && subVersion != 9) {
        throw std::runtime_error("lev: bad map sub-version in " + result.source_);
    }
    uint32_t uidLo, uidHi;
    std::memcpy(&uidLo, mapHeader + 5, 4);
    std::memcpy(&uidHi, mapHeader + 9, 4);
    result.uid_ = (static_cast<uint64_t>(uidHi) << 32) | uidLo;
    std::memcpy(&result.width_, mapHeader + 13, 4);
    std::memcpy(&result.height_, mapHeader + 17, 4);
    if (result.width_ < 0 || result.height_ < 0 ||
        result.width_ > 4096 || result.height_ > 4096) {
        throw std::runtime_error("lev: implausible map size in " + result.source_);
    }

    // 256 ground themes: 128-byte name + u32 value
    result.groundThemes_.reserve(256);
    for (int i = 0; i < 256; ++i) {
        char name[128];
        reader.read(name, sizeof(name), "ground theme name");
        name[127] = '\0';
        GroundTheme theme;
        theme.name = name;
        theme.value = reader.readValue<uint32_t>("ground theme value");
        result.groundThemes_.push_back(std::move(theme));
    }

    reader.readValue<uint32_t>("cell version");
    const uint32_t themeCount = reader.readValue<uint32_t>("theme count");
    reader.skip(kPaletteSize);
    if (subVersion == 9) {
        reader.readValue<uint32_t>("sub-version 9 extra");
    }
    if (themeCount > 0) {
        result.themes_.reserve(themeCount - 1);
        for (uint32_t i = 0; i + 1 < themeCount; ++i) {
            result.themes_.push_back(reader.readCountedString("theme string"));
        }
    }

    // Cell grid
    const size_t cellCount = static_cast<size_t>(result.width_ + 1) *
                             static_cast<size_t>(result.height_ + 1);
    result.cells_.resize(cellCount * kCellSize);
    result.cellsOffset_ = reader.tell();
    reader.read(result.cells_.data(), result.cells_.size(), "cells");

    // Navigation table of contents (section payloads left unparsed for now)
    reader.seek(navOffset);
    uint8_t navHeader[8];
    reader.read(navHeader, sizeof(navHeader), "nav header");
    uint32_t sectionCount;
    std::memcpy(&sectionCount, navHeader + 4, 4);
    if (sectionCount > 256) {
        throw std::runtime_error("lev: implausible nav section count in " + result.source_);
    }
    for (uint32_t i = 0; i < sectionCount; ++i) {
        NavSectionInfo section;
        section.name = reader.readCountedString("nav section name");
        section.offset = reader.readValue<uint32_t>("nav section offset");
        result.navSections_.push_back(std::move(section));
    }

    std::ifstream original(path, std::ios::binary);
    result.originalBytes_.assign(std::istreambuf_iterator<char>(original),
                                 std::istreambuf_iterator<char>());
    if (result.cellsOffset_ + result.cells_.size() > result.originalBytes_.size()) {
        throw std::runtime_error("lev: cell grid exceeds source file in " +
                                 result.source_);
    }

    return result;
}

void File::checkCell(int x, int y) const {
    if (x < 0 || y < 0 || x >= cellsX() || y >= cellsY()) {
        throw std::out_of_range("lev: cell coordinate out of range (" +
                                std::to_string(x) + "," + std::to_string(y) + ")");
    }
}

const uint8_t* File::cell(int x, int y) const {
    checkCell(x, y);
    const size_t index = static_cast<size_t>(y) * static_cast<size_t>(width_ + 1) +
                         static_cast<size_t>(x);
    return cells_.data() + index * kCellSize;
}

uint8_t* File::cell(int x, int y) {
    checkCell(x, y);
    const size_t index = static_cast<size_t>(y) * static_cast<size_t>(width_ + 1) +
                         static_cast<size_t>(x);
    return cells_.data() + index * kCellSize;
}

float File::heightAt(int x, int y) const {
    float raw;
    std::memcpy(&raw, cell(x, y) + 5, 4);
    return raw * 2048.0f;
}

void File::setHeightAt(int x, int y, float worldHeight) {
    if (!std::isfinite(worldHeight)) {
        throw std::invalid_argument("lev: height must be finite");
    }
    const float raw = worldHeight / 2048.0f;
    std::memcpy(cell(x, y) + 5, &raw, sizeof(raw));
}

void File::save(const fs::path& path) const {
    std::vector<uint8_t> output = originalBytes_;
    std::copy(cells_.begin(), cells_.end(), output.begin() +
              static_cast<std::vector<uint8_t>::difference_type>(cellsOffset_));
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("lev: cannot write " + path.string());
    stream.write(reinterpret_cast<const char*>(output.data()),
                 static_cast<std::streamsize>(output.size()));
    if (!stream) throw std::runtime_error("lev: failed writing " + path.string());
}

void File::setGroundThemeValue(size_t slot, uint32_t defIndex) {
    if (slot >= groundThemes_.size()) {
        throw std::out_of_range("lev: ground theme slot out of range");
    }
    groundThemes_[slot].value = defIndex;
    const size_t at = kThemeTableOffset + slot * kThemeEntrySize + 128;
    if (at + 4 > originalBytes_.size()) {
        throw std::runtime_error("lev: theme palette runs past end of " + source_);
    }
    std::memcpy(originalBytes_.data() + at, &defIndex, sizeof(defIndex));
}

void File::setGroundTheme(size_t slot, const std::string& name, uint32_t defIndex) {
    if (slot >= groundThemes_.size())
        throw std::out_of_range("lev: ground theme slot out of range");
    if (name.size() >= 128 || name.find('\0') != std::string::npos)
        throw std::invalid_argument("lev: ground theme name must fit 127 bytes");
    const size_t at = kThemeTableOffset + slot * kThemeEntrySize;
    if (at + kThemeEntrySize > originalBytes_.size())
        throw std::runtime_error("lev: theme palette runs past end of " + source_);
    std::fill(originalBytes_.begin() + static_cast<std::ptrdiff_t>(at),
              originalBytes_.begin() + static_cast<std::ptrdiff_t>(at + 128), 0);
    std::copy(name.begin(), name.end(),
              originalBytes_.begin() + static_cast<std::ptrdiff_t>(at));
    groundThemes_[slot].name = name;
    setGroundThemeValue(slot, defIndex);
}

std::vector<ThemePaletteIssue> File::auditThemePalette(const DefIndexTable& defs) const {
    std::vector<ThemePaletteIssue> issues;
    if (!defs.indexOf) return issues;
    for (size_t slot = 0; slot < groundThemes_.size() && slot < kThemeEntryCount; ++slot) {
        const GroundTheme& theme = groundThemes_[slot];
        if (theme.name.empty()) continue; // unused palette slot
        ThemePaletteIssue issue;
        issue.slot = static_cast<int>(slot);
        issue.name = theme.name;
        issue.stored = theme.value;
        uint32_t want = 0;
        issue.resolvable = defs.indexOf(theme.name, want);
        issue.expected = issue.resolvable ? want : theme.value;
        if (issue.resolvable && want == theme.value) continue; // correct already
        if (defs.typeOf) issue.storedDefType = defs.typeOf(theme.value);
        issues.push_back(std::move(issue));
    }
    return issues;
}

size_t File::rebaseThemePalette(const DefIndexTable& defs) {
    size_t changed = 0;
    for (const ThemePaletteIssue& issue : auditThemePalette(defs)) {
        if (!issue.resolvable) continue;
        setGroundThemeValue(static_cast<size_t>(issue.slot), issue.expected);
        ++changed;
    }
    return changed;
}

bool File::walkableAt(int x, int y) const {
    return cell(x, y)[15] != 0;
}

void File::setWalkableAt(int x, int y, bool walkable) {
    cell(x, y)[15] = walkable ? 1 : 0;
}

bool File::preferredPathAt(int x, int y) const {
    return cell(x, y)[20] != 0;
}

void File::setPreferredPathAt(int x, int y, bool preferred) {
    cell(x, y)[20] = preferred ? 1 : 0;
}

uint8_t File::themeIndexAt(int x, int y, int index) const {
    if (index < 0 || index >= 3) throw std::out_of_range("lev: theme layer out of range");
    return cell(x, y)[10 + index];
}

uint8_t File::themeStrengthAt(int x, int y, int index) const {
    if (index < 0 || index >= 3) throw std::out_of_range("lev: theme layer out of range");
    const uint8_t* c = cell(x, y);
    if (index < 2) return c[13 + index];
    const unsigned stored = unsigned(c[13]) + unsigned(c[14]);
    return static_cast<uint8_t>(stored >= 255 ? 0 : 255 - stored);
}

void File::setThemeBlendAt(int x, int y,
                           const std::array<uint8_t, 3>& indices,
                           const std::array<uint8_t, 3>& strengths) {
    const unsigned total = unsigned(strengths[0]) + unsigned(strengths[1]) +
                           unsigned(strengths[2]);
    if (total != 255) throw std::invalid_argument("lev: theme strengths must sum to 255");
    uint8_t* c = cell(x, y);
    for (int i = 0; i < 3; ++i) c[10 + i] = indices[i];
    c[13] = strengths[0];
    c[14] = strengths[1];
}

int File::dominantLayerAt(int x, int y) const {
    int best = 0;
    for (int layer = 1; layer < 3; ++layer) {
        if (themeStrengthAt(x, y, layer) > themeStrengthAt(x, y, best)) {
            best = layer;
        }
    }
    return best;
}

const std::string& File::themeNameAt(int x, int y) const {
    return groundThemes_[themeIndexAt(x, y, dominantLayerAt(x, y))].name;
}

} // namespace forge::lev
