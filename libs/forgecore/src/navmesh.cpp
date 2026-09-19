#include "forge/navmesh.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace forge::navmesh {
namespace {

constexpr uint32_t kVersion = 8;
constexpr uint32_t kEndSymbol = 0xACBDEF12u;
constexpr float kRootSize = 32.0f;

uint32_t readU32(const std::vector<uint8_t>& bytes, size_t& pos, const char* what) {
    if (pos + 4 > bytes.size()) throw std::runtime_error(std::string("navmesh: truncated ") + what);
    uint32_t value = uint32_t(bytes[pos]) | (uint32_t(bytes[pos + 1]) << 8) |
                     (uint32_t(bytes[pos + 2]) << 16) | (uint32_t(bytes[pos + 3]) << 24);
    pos += 4;
    return value;
}

float readF32(const std::vector<uint8_t>& bytes, size_t& pos, const char* what) {
    const uint32_t bits = readU32(bytes, pos, what);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

struct Writer {
    std::vector<uint8_t> bytes;
    void u8(uint8_t value) { bytes.push_back(value); }
    void u32(uint32_t value) {
        bytes.push_back(uint8_t(value)); bytes.push_back(uint8_t(value >> 8));
        bytes.push_back(uint8_t(value >> 16)); bytes.push_back(uint8_t(value >> 24));
    }
    void i32(int32_t value) { u32(static_cast<uint32_t>(value)); }
    void f32(float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        u32(bits);
    }
    void text(const std::string& value) { bytes.insert(bytes.end(), value.begin(), value.end()); }
};

struct Position { float x = 0, y = 0; int32_t data = 0; };
struct Section { std::string name; std::vector<Position> positions; };

std::vector<Section> readSections(const lev::File& file) {
    const auto& bytes = file.originalBytes();
    size_t pos = file.navigationOffset();
    readU32(bytes, pos, "nav directory end");
    const uint32_t count = readU32(bytes, pos, "nav section count");
    if (count > 256) throw std::runtime_error("navmesh: implausible section count");
    struct Toc { std::string name; uint32_t offset; };
    std::vector<Toc> toc;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t length = readU32(bytes, pos, "nav section name length");
        if (length > 4096 || pos + length > bytes.size())
            throw std::runtime_error("navmesh: invalid section name");
        std::string name(reinterpret_cast<const char*>(bytes.data() + pos), length);
        pos += length;
        toc.push_back({std::move(name), readU32(bytes, pos, "nav section offset")});
    }
    std::vector<Section> sections;
    for (const auto& entry : toc) {
        size_t block = entry.offset;
        readU32(bytes, block, "block end");
        if (readU32(bytes, block, "block version") != kVersion)
            throw std::runtime_error("navmesh: unsupported existing block version");
        const float width = readF32(bytes, block, "block width");
        const float height = readF32(bytes, block, "block height");
        if (std::lround(width) != file.width() || std::lround(height) != file.height())
            throw std::runtime_error("navmesh: existing block dimensions differ from LEV");
        readU32(bytes, block, "region count");
        const uint32_t positions = readU32(bytes, block, "position count");
        if (positions > 100000) throw std::runtime_error("navmesh: implausible position count");
        Section section;
        section.name = entry.name;
        section.positions.reserve(positions);
        for (uint32_t i = 0; i < positions; ++i) {
            Position p;
            p.x = readF32(bytes, block, "position x");
            p.y = readF32(bytes, block, "position y");
            p.data = static_cast<int32_t>(readU32(bytes, block, "position data"));
            section.positions.push_back(p);
        }
        sections.push_back(std::move(section));
    }
    if (sections.empty()) sections.push_back(Section{"NULL", {}});
    return sections;
}

enum class CellKind : uint8_t { Blocked, Preferred, Normal };

bool anchorDefinition(const std::string& value) {
    return value == "NAVIGATION_SEED" || value == "REGION_ENTRANCE_POINT" ||
           value == "REGION_EXIT_POINT";
}

std::optional<float> propertyFloat(const tng::Thing& thing, const char* key) {
    for (const auto& block : thing.ctcBlocks) {
        for (const auto& property : block.properties) {
            std::string a = property.key, b = key;
            std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (a == b) {
                try { return std::stof(property.value); }
                catch (const std::exception&) { return std::nullopt; }
            }
        }
    }
    return std::nullopt;
}

struct RasterResult {
    std::vector<CellKind> cells;
    size_t removed = 0;
    size_t anchors = 0;
};

RasterResult buildRaster(const lev::File& file, const tng::File* tngFile) {
    const int w = file.width(), h = file.height();
    RasterResult out;
    out.cells.resize(static_cast<size_t>(w) * h);
    auto idx = [w](int x, int y) { return static_cast<size_t>(y) * w + x; };
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        out.cells[idx(x, y)] = !file.walkableAt(x, y) ? CellKind::Blocked
                              : file.preferredPathAt(x, y) ? CellKind::Preferred
                              : CellKind::Normal;
    }
    std::vector<int> component(out.cells.size(), -1);
    std::vector<size_t> sizes;
    constexpr int dx[4] = {1, -1, 0, 0};
    constexpr int dy[4] = {0, 0, 1, -1};
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const size_t start = idx(x, y);
        if (out.cells[start] == CellKind::Blocked || component[start] >= 0) continue;
        const int id = static_cast<int>(sizes.size());
        size_t count = 0;
        std::queue<std::pair<int, int>> pending;
        pending.push({x, y}); component[start] = id;
        while (!pending.empty()) {
            const auto [cx, cy] = pending.front(); pending.pop(); ++count;
            for (int d = 0; d < 4; ++d) {
                const int nx = cx + dx[d], ny = cy + dy[d];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                const size_t ni = idx(nx, ny);
                if (out.cells[ni] != CellKind::Blocked && component[ni] < 0) {
                    component[ni] = id; pending.push({nx, ny});
                }
            }
        }
        sizes.push_back(count);
    }
    std::set<int> keep;
    if (tngFile != nullptr) {
        for (const auto& thing : tngFile->things()) {
            if (!anchorDefinition(thing.definitionType())) continue;
            const auto px = propertyFloat(thing, "PositionX");
            const auto py = propertyFloat(thing, "PositionY");
            if (!px || !py) continue;
            const int x = static_cast<int>(*px), y = static_cast<int>(*py);
            if (x >= 0 && y >= 0 && x < w && y < h) {
                const int id = component[idx(x, y)];
                if (id >= 0) { keep.insert(id); ++out.anchors; }
            }
        }
    }
    if (keep.empty() && !sizes.empty()) {
        keep.insert(static_cast<int>(std::max_element(sizes.begin(), sizes.end()) - sizes.begin()));
    }
    for (size_t i = 0; i < out.cells.size(); ++i) {
        if (out.cells[i] != CellKind::Blocked && !keep.contains(component[i])) {
            out.cells[i] = CellKind::Blocked; ++out.removed;
        }
    }
    return out;
}

struct Node {
    enum class Kind { Internal, Navigable } kind = Kind::Navigable;
    int level = 0;
    float cx = 0, cy = 0;
    uint8_t preference = 0x40;
    std::array<std::unique_ptr<Node>, 4> children;
    int32_t index = 0;
    int32_t region = 0;
    std::vector<Node*> neighbours;
};

float nodeSize(int level) { return kRootSize / float(1 << level); }

std::unique_ptr<Node> subdivide(const RasterResult& raster, int w, int h,
                                int level, float cx, float cy) {
    const float size = nodeSize(level);
    const int x0 = int(std::lround(cx - size * 0.5f));
    const int x1 = int(std::lround(cx + size * 0.5f));
    const int y0 = int(std::lround(cy - size * 0.5f));
    const int y1 = int(std::lround(cy + size * 0.5f));
    if (x0 < 0 || y0 < 0 || x1 > w || y1 > h)
        throw std::runtime_error("navmesh: quadtree root extends outside map");
    auto at = [&](int x, int y) { return raster.cells[static_cast<size_t>(y) * w + x]; };
    const CellKind first = at(x0, y0);
    bool uniform = true;
    for (int y = y0; y < y1 && uniform; ++y)
        for (int x = x0; x < x1; ++x) if (at(x, y) != first) { uniform = false; break; }
    if (uniform) {
        if (first == CellKind::Blocked) return nullptr;
        auto node = std::make_unique<Node>();
        node->level = level; node->cx = cx; node->cy = cy;
        node->preference = first == CellKind::Preferred ? 0x00 : 0x80;
        return node;
    }
    if (level >= 5) throw std::runtime_error("navmesh: non-uniform unit cell");
    auto node = std::make_unique<Node>();
    node->kind = Node::Kind::Internal; node->level = level; node->cx = cx; node->cy = cy;
    const float q = size * 0.25f;
    constexpr int sx[4] = {-1, 1, -1, 1};
    constexpr int sy[4] = {-1, -1, 1, 1};
    bool any = false;
    for (int i = 0; i < 4; ++i) {
        node->children[i] = subdivide(raster, w, h, level + 1,
                                      cx + sx[i] * q, cy + sy[i] * q);
        any |= node->children[i] != nullptr;
    }
    if (!any) return nullptr;
    return node;
}

struct Tree {
    std::vector<std::unique_ptr<Node>> roots;
    std::vector<Node*> records;
    std::set<size_t> blockedRecords;
    int32_t total = 0;
    size_t regions = 1;
    size_t internals = 0;
    size_t leaves = 0;
};

Tree buildTree(const RasterResult& raster, int w, int h) {
    if (w <= 0 || h <= 0 || w % 32 != 0 || h % 32 != 0)
        throw std::invalid_argument("navmesh: map dimensions must be positive multiples of 32");
    Tree tree;
    for (int gy = 0; gy < h / 32; ++gy) for (int gx = 0; gx < w / 32; ++gx)
        tree.roots.push_back(subdivide(raster, w, h, 0, 32.0f * (gx + 0.5f),
                                      32.0f * (gy + 0.5f)));
    int32_t counter = 1;
    auto assign = [&](auto&& self, Node* node) -> void {
        if (node->kind == Node::Kind::Internal)
            for (auto& child : node->children) if (child) self(self, child.get());
        node->index = counter++;
    };
    for (auto& root : tree.roots) { if (root) assign(assign, root.get()); else ++counter; }
    tree.total = counter - 1;
    auto emit = [&](auto&& self, Node* node) -> void {
        tree.records.push_back(node);
        if (node->kind == Node::Kind::Internal)
            for (auto& child : node->children) if (child) self(self, child.get());
    };
    for (auto& root : tree.roots) {
        if (root) emit(emit, root.get());
        else { tree.blockedRecords.insert(tree.records.size()); tree.records.push_back(nullptr); }
    }
    if (tree.records.size() != static_cast<size_t>(tree.total))
        throw std::runtime_error("navmesh: record/index count mismatch");

    std::vector<Node*> leafAt(static_cast<size_t>(w) * h, nullptr);
    std::vector<Node*> leaves;
    for (Node* node : tree.records) {
        if (!node) continue;
        if (node->kind == Node::Kind::Internal) { ++tree.internals; continue; }
        ++tree.leaves; leaves.push_back(node);
        const float half = nodeSize(node->level) * 0.5f;
        for (int y = int(std::lround(node->cy - half)); y < int(std::lround(node->cy + half)); ++y)
            for (int x = int(std::lround(node->cx - half)); x < int(std::lround(node->cx + half)); ++x)
                leafAt[static_cast<size_t>(y) * w + x] = node;
    }
    std::map<Node*, std::set<Node*>> adjacent;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        Node* a = leafAt[static_cast<size_t>(y) * w + x];
        if (!a) continue;
        for (const auto [dx, dy] : {std::pair{1, 0}, std::pair{0, 1}}) {
            if (x + dx >= w || y + dy >= h) continue;
            Node* b = leafAt[static_cast<size_t>(y + dy) * w + (x + dx)];
            if (b && b != a) { adjacent[a].insert(b); adjacent[b].insert(a); }
        }
    }
    for (Node* node : leaves) {
        node->neighbours.assign(adjacent[node].begin(), adjacent[node].end());
        std::sort(node->neighbours.begin(), node->neighbours.end(),
                  [](Node* a, Node* b) { return a->index < b->index; });
    }
    int32_t region = 0;
    for (Node* start : leaves) {
        if (start->region != 0) continue;
        ++region; start->region = region;
        std::vector<Node*> pending{start};
        while (!pending.empty()) {
            Node* node = pending.back(); pending.pop_back();
            for (Node* other : node->neighbours) if (other->region == 0) {
                other->region = region; pending.push_back(other);
            }
        }
    }
    tree.regions = static_cast<size_t>(region + 1);
    return tree;
}

std::vector<uint8_t> emitBlock(const Tree& tree, const Section& section,
                               int width, int height, uint32_t blockEnd) {
    Writer out;
    out.u32(blockEnd); out.u32(kVersion); out.f32(float(width)); out.f32(float(height));
    out.u32(static_cast<uint32_t>(tree.regions));
    out.u32(static_cast<uint32_t>(section.positions.size()));
    for (const auto& p : section.positions) { out.f32(p.x); out.f32(p.y); out.i32(p.data); }
    out.u32(1); out.i32(tree.total);
    for (Node* node : tree.records) {
        if (!node) { out.u8(0); out.u8(1); out.u8(1); continue; }
        out.u8(0); out.u8(node->level == 0 ? 1 : 0); out.u8(0);
        out.u8(node->kind == Node::Kind::Internal ? 0 : 1);
        out.u8(static_cast<uint8_t>(node->level)); out.u8(0);
        out.f32(node->cx); out.f32(node->cy); out.i32(node->index);
        if (node->kind == Node::Kind::Internal) {
            for (const auto& child : node->children) out.i32(child ? child->index : 0);
        } else {
            out.i32(node->region); out.u8(node->preference);
            out.i32(static_cast<int32_t>(node->neighbours.size()));
            for (Node* other : node->neighbours) out.i32(other->index);
        }
    }
    out.u32(kEndSymbol);
    return out.bytes;
}

} // namespace

GenerateResult generateTerrain(const lev::File& file, const tng::File* tngFile) {
    const auto sections = readSections(file);
    const auto raster = buildRaster(file, tngFile);
    Tree tree = buildTree(raster, file.width(), file.height());

    size_t directorySize = 8;
    for (const auto& section : sections) directorySize += 8 + section.name.size();
    uint32_t offset = file.navigationOffset() + static_cast<uint32_t>(directorySize);
    std::vector<std::vector<uint8_t>> blocks;
    std::vector<uint32_t> offsets;
    for (const auto& section : sections) {
        offsets.push_back(offset);
        auto sizing = emitBlock(tree, section, file.width(), file.height(), 0);
        offset += static_cast<uint32_t>(sizing.size());
        blocks.push_back(emitBlock(tree, section, file.width(), file.height(), offset));
    }
    Writer directory;
    directory.u32(offsets.front());
    directory.u32(static_cast<uint32_t>(sections.size()));
    for (size_t i = 0; i < sections.size(); ++i) {
        directory.u32(static_cast<uint32_t>(sections[i].name.size()));
        directory.text(sections[i].name); directory.u32(offsets[i]);
    }
    GenerateResult result;
    const auto& original = file.originalBytes();
    result.levBytes.assign(original.begin(), original.begin() + file.navigationOffset());
    result.levBytes.insert(result.levBytes.end(), directory.bytes.begin(), directory.bytes.end());
    for (const auto& block : blocks)
        result.levBytes.insert(result.levBytes.end(), block.begin(), block.end());
    if (result.levBytes.size() != offset)
        throw std::runtime_error("navmesh: emitted EOF does not match reservedSize chain");
    result.sections = sections.size(); result.recordsPerSection = tree.records.size();
    result.internalNodes = tree.internals; result.navigableLeaves = tree.leaves;
    result.blockedRootRecords = tree.blockedRecords.size(); result.regions = tree.regions;
    result.islandCellsRemoved = raster.removed; result.anchorCount = raster.anchors;
    result.walkableCells = std::count_if(raster.cells.begin(), raster.cells.end(),
        [](CellKind kind) { return kind != CellKind::Blocked; });
    return result;
}

} // namespace forge::navmesh
