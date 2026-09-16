// Functional tests for the exporter that need NO retail data: a synthetic
// .lev is written to a temp dir, exported, and the outputs are checked
// structurally (GLB chunk layout + JSON, OBJ face/vertex counts, PNG magic).
// Retail round-trips are exercised separately by tools/retail_smoke.py.

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "forge/lev.hpp"
#include "nlohmann/json.hpp"
#include "terrainexport.hpp"

namespace fs = std::filesystem;
namespace te = albion::terrainexport;
using json = nlohmann::json;

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #cond "\n";  \
            ++g_failures;                                                              \
        }                                                                              \
    } while (0)

template <typename T> void put(std::vector<uint8_t>& v, T x) {
    const auto* p = reinterpret_cast<const uint8_t*>(&x);
    v.insert(v.end(), p, p + sizeof(T));
}

// Write a minimal but valid LEV: w x h cells, heights from `height(x,y)`,
// two ground themes (slot 0 / slot 1) blended left->right, odd rows unwalkable.
fs::path writeSyntheticLev(const fs::path& path, int w, int h,
                           const std::function<float(int, int)>& height) {
    std::vector<uint8_t> b;
    // LEVHeader (25): u32 headerSize, u16 version, u8 pad[3], u32 r1, u32 obs, u32 r2, u32 nav
    put<uint32_t>(b, 25); put<uint16_t>(b, 6404); b.insert(b.end(), 3, 0);
    put<uint32_t>(b, 0); put<uint32_t>(b, 0); put<uint32_t>(b, 0);
    const size_t navOffsetPos = b.size(); put<uint32_t>(b, 0);
    // LEVMapHeader (22): u8 size, u8 mapVersion=8, u8 pad[3] (pad[2]=sub 8), uidLo, uidHi, w, h, flag
    b.push_back(22); b.push_back(8); b.push_back(0); b.push_back(0); b.push_back(8);
    put<uint32_t>(b, 0xC0FFEE); put<uint32_t>(b, 1);
    put<int32_t>(b, w); put<int32_t>(b, h); b.push_back(0);
    // 256 ground themes
    for (int i = 0; i < 256; ++i) {
        char name[128] = {};
        if (i == 0) std::strcpy(name, "GROUND_GRASS_TEST");
        if (i == 1) std::strcpy(name, "GROUND_ROCK_TEST");
        b.insert(b.end(), name, name + 128);
        put<uint32_t>(b, i < 2 ? 1000u + uint32_t(i) : 0u);
    }
    put<uint32_t>(b, 1);          // cell version
    put<uint32_t>(b, 1);          // theme count (=> zero theme strings)
    b.insert(b.end(), 33792, 0);  // palette
    for (int y = 0; y <= h; ++y)
        for (int x = 0; x <= w; ++x) {
            uint8_t cell[21] = {};
            const float raw = height(x, y) / 2048.0f;
            std::memcpy(cell + 5, &raw, 4);
            cell[10] = 0; cell[11] = 1; cell[12] = 0;
            const uint8_t s0 = uint8_t(255 - (255 * x) / std::max(w, 1));
            cell[13] = s0; cell[14] = uint8_t(255 - s0);
            cell[15] = (y % 2 == 0) ? 1 : 0;
            b.insert(b.end(), cell, cell + 21);
        }
    const uint32_t navOffset = uint32_t(b.size());
    std::memcpy(&b[navOffsetPos], &navOffset, 4);
    put<uint32_t>(b, 0); put<uint32_t>(b, 0); // nav header: 0 sections
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    return path;
}

std::vector<uint8_t> readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

uint32_t u32(const std::vector<uint8_t>& b, size_t o) {
    return uint32_t(b[o]) | (uint32_t(b[o + 1]) << 8) | (uint32_t(b[o + 2]) << 16) | (uint32_t(b[o + 3]) << 24);
}

// Parse a GLB into (json, bin) and validate the container layout.
bool parseGlb(const std::vector<uint8_t>& glb, json& doc, std::vector<uint8_t>& bin) {
    if (glb.size() < 20 || u32(glb, 0) != 0x46546C67 || u32(glb, 4) != 2) return false;
    if (u32(glb, 8) != glb.size()) return false;
    const uint32_t jl = u32(glb, 12);
    if (u32(glb, 16) != 0x4E4F534A || jl % 4) return false;
    doc = json::parse(glb.begin() + 20, glb.begin() + 20 + jl);
    const size_t bo = 20 + jl;
    if (bo + 8 > glb.size()) return false;
    const uint32_t bl = u32(glb, bo);
    if (u32(glb, bo + 4) != 0x004E4942 || bo + 8 + bl != glb.size()) return false;
    bin.assign(glb.begin() + bo + 8, glb.end());
    return true;
}

void testMeshGeometry(const fs::path& lev) {
    const auto file = forge::lev::File::open(lev);
    te::Options o; o.textures = false;
    const auto s = te::buildMesh(file, o);
    CHECK(s.mapWidth == 4 && s.mapHeight == 3);
    CHECK(s.vertices.size() == 5 * 4);
    CHECK(s.indices.size() == 4 * 3 * 6);
    // Y-up: Fable (x, y, z) -> (x, z, -y). Vertex (2,1) height = 2*10+1*3 = 23.
    const auto& v = s.vertices[1 * 5 + 2];
    CHECK(std::fabs(v.px - 2) < 1e-5 && std::fabs(v.py - 23) < 1e-3 && std::fabs(v.pz + 1) < 1e-5);
    CHECK(std::fabs(s.minHeight - 0) < 1e-3 && std::fabs(s.maxHeight - (40 + 9)) < 1e-3);
    // Normal of the plane z = 10x + 3y is (-10,-3,1)/|..| in Fable space -> Y-up (nx, nz, -ny).
    const float len = std::sqrt(100.f + 9.f + 1.f);
    CHECK(std::fabs(v.nx - (-10 / len)) < 1e-4 && std::fabs(v.ny - (1 / len)) < 1e-4 &&
          std::fabs(v.nz - (3 / len)) < 1e-4);
    // Winding: every triangle faces up (+Y) in the output.
    for (size_t i = 0; i < s.indices.size(); i += 3) {
        const auto& a = s.vertices[s.indices[i]]; const auto& b = s.vertices[s.indices[i + 1]];
        const auto& c = s.vertices[s.indices[i + 2]];
        const float ux = b.px - a.px, uy = b.py - a.py, uz = b.pz - a.pz;
        const float vx = c.px - a.px, vy = c.py - a.py, vz = c.pz - a.pz;
        const float ny = uz * vx - ux * vz; // y component of cross(u, v)
        CHECK(ny > 0);
    }
    // Walkable + theme data carried through.
    CHECK(s.vertices[0].walkable && !s.vertices[5].walkable);
    CHECK(s.vertices[0].themeWeight[0] == 255 && s.vertices[4].themeWeight[1] == 255);
    CHECK(s.vertices[4].themeIndex[1] == 1);

    te::Options z = o; z.up = te::UpAxis::Z; z.originX = 100; z.originY = 200;
    const auto sz = te::buildMesh(file, z);
    const auto& vz = sz.vertices[1 * 5 + 2];
    CHECK(std::fabs(vz.px - 102) < 1e-5 && std::fabs(vz.py - 201) < 1e-5 && std::fabs(vz.pz - 23) < 1e-3);
}

void testGlbStructure(const fs::path& lev, const fs::path& dir) {
    const auto file = forge::lev::File::open(lev);
    te::Options o; o.textures = false; o.walkableColor = true; o.layers = true;
    const auto s = te::buildScene(file, o);
    CHECK(!s.hasAlbedo);
    const auto written = te::writeGlb(s, dir / "synthetic.glb");
    CHECK(written.size() >= 2); // glb + themes.json (no layer PNGs without textures)
    json doc; std::vector<uint8_t> bin;
    CHECK(parseGlb(readAll(dir / "synthetic.glb"), doc, bin));
    CHECK(doc["asset"]["version"] == "2.0");
    CHECK(doc["buffers"][0]["byteLength"] == bin.size());
    const auto& prim = doc["meshes"][0]["primitives"][0];
    CHECK(prim.contains("indices") && prim["mode"] == 4);
    const auto& attrs = prim["attributes"];
    CHECK(attrs.contains("POSITION") && attrs.contains("NORMAL") && attrs.contains("TEXCOORD_0"));
    CHECK(attrs.contains("COLOR_0") && attrs.contains("_THEME_INDEX") && attrs.contains("_THEME_WEIGHT"));
    const auto& pos = doc["accessors"][attrs["POSITION"].get<int>()];
    CHECK(pos["count"] == 20 && pos["type"] == "VEC3" && pos.contains("min") && pos.contains("max"));
    CHECK(pos["min"][1].get<float>() == 0.0f && std::fabs(pos["max"][1].get<float>() - 49) < 1e-3);
    // Every bufferView is 4-byte aligned and within the BIN chunk.
    for (const auto& bv : doc["bufferViews"]) {
        CHECK(bv["byteOffset"].get<size_t>() % 4 == 0);
        CHECK(bv["byteOffset"].get<size_t>() + bv["byteLength"].get<size_t>() <= bin.size());
    }
    // Index accessor covers every triangle and indices stay in range.
    const auto& ia = doc["accessors"][prim["indices"].get<int>()];
    CHECK(ia["count"] == 72 && ia["componentType"] == 5125);
    const auto& ibv = doc["bufferViews"][ia["bufferView"].get<int>()];
    const size_t ioff = ibv["byteOffset"].get<size_t>();
    for (size_t i = 0; i < 72; ++i) CHECK(u32(bin, ioff + i * 4) < 20);
    CHECK(!doc.contains("images"));
    CHECK(doc["materials"][0]["pbrMetallicRoughness"].contains("baseColorFactor"));
}

void testObjStructure(const fs::path& lev, const fs::path& dir) {
    const auto file = forge::lev::File::open(lev);
    te::Options o; o.textures = false;
    const auto s = te::buildScene(file, o);
    te::writeObj(s, dir / "synthetic.obj");
    std::ifstream f(dir / "synthetic.obj");
    std::string line; int v = 0, vt = 0, vn = 0, faces = 0; bool mtllib = false;
    while (std::getline(f, line)) {
        if (line.rfind("v ", 0) == 0) ++v;
        else if (line.rfind("vt ", 0) == 0) ++vt;
        else if (line.rfind("vn ", 0) == 0) ++vn;
        else if (line.rfind("f ", 0) == 0) ++faces;
        else if (line.rfind("mtllib ", 0) == 0) mtllib = true;
    }
    CHECK(v == 20 && vt == 20 && vn == 20 && faces == 24 && mtllib);
    CHECK(fs::exists(dir / "synthetic.mtl"));
}

void testBcDecoders() {
    // BC1 block: c0 = pure red (0xF800), c1 = pure blue (0x001F), all indices 0 -> red.
    uint8_t blk[8] = {0x00, 0xF8, 0x1F, 0x00, 0, 0, 0, 0};
    auto px = te::decodeBc1ToRgba(blk, 4, 4);
    CHECK(px.size() == 64 && px[0] == 255 && px[1] == 0 && px[2] == 0 && px[3] == 255);
    // Indices all 1 -> blue.
    blk[4] = blk[5] = blk[6] = blk[7] = 0x55;
    px = te::decodeBc1ToRgba(blk, 4, 4);
    CHECK(px[0] == 0 && px[2] == 255);
    // c0 < c1 with index 3 -> transparent black (1-bit alpha mode).
    uint8_t blk2[8] = {0x1F, 0x00, 0x00, 0xF8, 0xFF, 0xFF, 0xFF, 0xFF};
    px = te::decodeBc1ToRgba(blk2, 4, 4);
    CHECK(px[3] == 0);
    // BC2: explicit alpha nibbles 0xA (=170) + red colour block; c0<c1 must NOT trigger alpha mode.
    uint8_t bc2[16] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0x1F, 0x00, 0x00, 0xF8, 0xFF, 0xFF, 0xFF, 0xFF};
    px = te::decodeBc2ToRgba(bc2, 4, 4);
    CHECK(px[3] == 170 && (px[0] != 0 || px[2] != 0));
    // BGRA -> RGBA swap.
    const uint8_t bgra[4] = {1, 2, 3, 4};
    const auto rgba = te::bgra8ToRgba(bgra, 1, 1);
    CHECK(rgba[0] == 3 && rgba[1] == 2 && rgba[2] == 1 && rgba[3] == 4);
}

void testPng() {
    te::Image img; img.width = 2; img.height = 2; img.rgba.assign(16, 200); img.name = "t";
    const auto png = te::encodePng(img);
    CHECK(png.size() > 8 && png[0] == 0x89 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "AlbionTerrainTests";
    fs::create_directories(dir);
    const fs::path lev = writeSyntheticLev(dir / "synthetic.lev", 4, 3,
                                           [](int x, int y) { return 10.0f * x + 3.0f * y; });
    testMeshGeometry(lev);
    testGlbStructure(lev, dir);
    testObjStructure(lev, dir);
    testBcDecoders();
    testPng();
    if (g_failures) { std::cerr << g_failures << " failure(s)\n"; return 1; }
    std::cout << "albionterrain_tests: all passed\n";
    return 0;
}
