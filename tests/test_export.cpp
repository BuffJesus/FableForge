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
#include "forge/terrain.hpp"
#include "forge/fractal.hpp"
#include "forge/stb.hpp"
#include "forge/navmesh.hpp"
#include "forge/navpatch.hpp"
#include "nlohmann/json.hpp"
#include "foliageexport.hpp"
#include "terrainexport.hpp"
#include "thingsexport.hpp"
#include "navlines.hpp"
#include "leveledit.hpp"
#include "presets.hpp"
#include "gtg.hpp"
#include "forge/meshcompose.hpp"
#include "forge/meshpreview.hpp"

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

// A fileVersion-3 .lev with the game-map grid (environment + sound themes): an
// 8x8 map -> 2x2 game-map cells, sounds {SOUND_A, SOUND_B}, atmos slot 1 named.
fs::path writeGameMapLev(const fs::path& path) {
    const int w = 8, h = 8;
    std::vector<uint8_t> b;
    put<uint32_t>(b, 25); put<uint16_t>(b, 6404); b.insert(b.end(), 3, 0);
    put<uint32_t>(b, 0);
    const size_t obsPos = b.size(); put<uint32_t>(b, 0);
    put<uint32_t>(b, 0);
    const size_t navOffsetPos = b.size(); put<uint32_t>(b, 0);
    b.push_back(22); b.push_back(8); b.push_back(0); b.push_back(0); b.push_back(8);
    put<uint32_t>(b, 7); put<uint32_t>(b, 0);
    put<int32_t>(b, w); put<int32_t>(b, h); b.push_back(0);
    for (int i = 0; i < 256; ++i) { char name[128] = {}; if (i == 0) std::strcpy(name, "GROUND_GRASS_TEST"); b.insert(b.end(), name, name + 128); put<uint32_t>(b, i == 0 ? 1000u : 0u); }
    put<uint32_t>(b, 3);          // file version 3
    put<uint32_t>(b, 3);          // sound count: NULL + 2 names
    for (int i = 0; i < 256; ++i) { char name[128] = {}; if (i == 1) std::strcpy(name, "ENVIRONMENT_UNDERTREES"); b.insert(b.end(), name, name + 128); put<uint32_t>(b, i == 1 ? 77u : 0u); }
    for (const char* n : {"SOUND_A", "SOUND_B"}) { put<uint32_t>(b, uint32_t(std::strlen(n))); b.insert(b.end(), n, n + std::strlen(n)); }
    for (int y = 0; y <= h; ++y)
        for (int x = 0; x <= w; ++x) {
            uint8_t cell[21] = {21, 0, 0, 0, 7};
            cell[13] = 255; cell[15] = 1;
            b.insert(b.end(), cell, cell + 21);
        }
    for (int i = 0; i < (w / 4) * (h / 4); ++i) {
        uint8_t rec[11] = {11, 0, 0, 0, 1, 0, 0, 0, 255, 0, uint8_t(i == 3 ? 2 : 1)};
        b.insert(b.end(), rec, rec + 11);
    }
    const uint32_t obs = uint32_t(b.size());
    std::memcpy(&b[obsPos], &obs, 4);
    b.insert(b.end(), 16, 0xAB);  // opaque obstacle data
    const uint32_t navOffset = uint32_t(b.size());
    std::memcpy(&b[navOffsetPos], &navOffset, 4);
    put<uint32_t>(b, 0); put<uint32_t>(b, 0);
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

void testFoliageGlb(const fs::path& lev, const fs::path& dir) {
    const auto file = forge::lev::File::open(lev);
    te::Options o; o.textures = false;
    const auto terrain = te::buildScene(file, o);
    namespace fe = albion::foliageexport;
    fe::Scene fol;
    fol.found = true;
    fe::Mesh m; m.meshId = 7; m.name = "MESH_TEST_BLADE";
    // A vertical quad in Fable space (x across, z up), two triangles.
    for (int i = 0; i < 4; ++i) {
        forge::meshpreview::Vertex v; v.x = (i & 1) ? 50.f : -50.f; v.y = 0; v.z = (i & 2) ? 100.f : 0.f;
        v.nx = 0; v.ny = -1; v.nz = 0; v.u = (i & 1) ? 1.f : 0.f; v.v = (i & 2) ? 0.f : 1.f;
        m.geometry.vertices.push_back(v);
    }
    m.geometry.triangles.push_back({0, 1, 3, 0}); m.geometry.triangles.push_back({0, 3, 2, 0});
    te::Image img; img.width = img.height = 2; img.rgba = {0,255,0,255, 0,255,0,0, 0,255,0,255, 0,255,0,0}; img.name = "blade";
    fol.images.push_back(img); m.image = 0; m.hasAlpha = true; m.instanceCount = 2;
    fe::SubMesh part; part.material = 0; part.image = 0; part.hasAlpha = true; part.indices = {0, 1, 3, 0, 3, 2};
    m.parts.push_back(part);
    fol.meshes.push_back(m);
    fe::Instance i0; i0.mesh = 0; i0.type = 0; i0.x = 1; i0.y = 2; i0.z = 5; i0.yaw = 0; i0.scale = 0.01f;
    fe::Instance i1; i1.mesh = 0; i1.type = 0; i1.x = 3; i1.y = 1; i1.z = 6; i1.yaw = 3.14159265f / 2; i1.scale = 0.02f;
    fol.instances.push_back(i0);
    fol.instances.push_back(i1);
    const auto glb = fe::buildGlbWithFoliage(terrain, fol);
    json doc; std::vector<uint8_t> bin;
    CHECK(parseGlb(glb, doc, bin));
    CHECK(doc["meshes"].size() == 2 && doc["meshes"][1]["name"] == "MESH_TEST_BLADE");
    CHECK(doc["materials"][1]["alphaMode"] == "MASK" && doc["materials"][1]["doubleSided"] == true);
    CHECK(doc["scenes"][0]["nodes"].size() == 2);   // terrain + Foliage root
    const auto& root = doc["nodes"][doc["scenes"][0]["nodes"][1].get<int>()];
    CHECK(root["name"] == "Foliage" && root["children"].size() == 2);
    const auto& n0 = doc["nodes"][root["children"][0].get<int>()];
    // Fable (1, 2, 5) -> glTF (1, 5, -2); identity rotation; uniform scale.
    CHECK(std::fabs(n0["translation"][0].get<float>() - 1) < 1e-5 && std::fabs(n0["translation"][1].get<float>() - 5) < 1e-5 &&
          std::fabs(n0["translation"][2].get<float>() + 2) < 1e-5);
    CHECK(std::fabs(n0["rotation"][3].get<float>() - 1) < 1e-5 && std::fabs(n0["scale"][0].get<float>() - 0.01f) < 1e-6);
    const auto& n1 = doc["nodes"][root["children"][1].get<int>()];
    // 90 degrees about Fable Z -> quaternion about glTF Y: (0, sin45, 0, cos45).
    CHECK(std::fabs(n1["rotation"][1].get<float>() - 0.70710678f) < 1e-4 && std::fabs(n1["rotation"][3].get<float>() - 0.70710678f) < 1e-4);
    // Mesh positions were axis-converted: the quad's top (z=100) is now y=100.
    const auto& posAcc = doc["accessors"][doc["meshes"][1]["primitives"][0]["attributes"]["POSITION"].get<int>()];
    CHECK(std::fabs(posAcc["max"][1].get<float>() - 100) < 1e-4 && std::fabs(posAcc["min"][0].get<float>() + 50) < 1e-4);
    CHECK(doc["images"].size() == 1);   // untextured terrain + one blade texture
    // OBJ variant appends a foliage object with 8 more vertices.
    fe::writeObjWithFoliage(terrain, fol, dir / "fol.obj");
    std::ifstream f(dir / "fol.obj"); std::string line; int v = 0, objs = 0, faces = 0;
    while (std::getline(f, line)) { if (line.rfind("v ", 0) == 0) ++v; if (line.rfind("o ", 0) == 0) ++objs; if (line.rfind("f ", 0) == 0) ++faces; }
    CHECK(v == 20 + 8 && objs == 2 && faces == 24 + 4);
}

void testPng() {
    te::Image img; img.width = 2; img.height = 2; img.rgba.assign(16, 200); img.name = "t";
    const auto png = te::encodePng(img);
    CHECK(png.size() > 8 && png[0] == 0x89 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
}

} // namespace

// Thing composition = the engine's CalcObjectMatrix rows {-right, -forward, up}:
// world = pos + lx*(-right) + ly*(-forward) + lz*up. Pinned by the Arena (oval pit
// axis, N/S corridors, audience billboards facing the pit) and Oakvale's fences.
void testThingBasis() {
    auto near = [](float a, float b) { return std::fabs(a - b) < 1e-5f; };
    float m[9];
    // North-facing thing (the Arena building): right = +X, so local x -> world -x,
    // local y -> world -y, local z -> world +z.
    const float north[3] = {0, 1, 0}, up[3] = {0, 0, 1};
    albion::thingsexport::thingBasis(north, up, 1.0f, m);
    CHECK(near(m[0], -1) && near(m[1], 0) && near(m[2], 0));
    CHECK(near(m[3], 0) && near(m[4], -1) && near(m[5], 0));
    CHECK(near(m[6], 0) && near(m[7], 0) && near(m[8], 1));
    // Default east-facing thing: forward +X, right = X x Z = -Y. Local x -> +y, local y -> -x.
    const float east[3] = {1, 0, 0};
    albion::thingsexport::thingBasis(east, up, 0.01f, m);
    CHECK(near(m[0], 0) && near(m[1], 0.01f) && near(m[2], 0));
    CHECK(near(m[3], -0.01f) && near(m[4], 0) && near(m[5], 0));
    CHECK(near(m[8], 0.01f));
    // Unnormalised input is normalised; a zero forward falls back to +X.
    const float longNorth[3] = {0, 5, 0}, zero[3] = {0, 0, 0};
    albion::thingsexport::thingBasis(longNorth, up, 1.0f, m);
    CHECK(near(m[4], -1));
    albion::thingsexport::thingBasis(zero, up, 1.0f, m);
    CHECK(near(m[3], -1) && near(m[1], 1));

    // Parent placed at scale 2 with a 3x graphic; child has a 5x graphic.
    // Dummy translation follows the full parent scale, but dummy axis lengths
    // and the parent's graphic scale must not leak into the child's size.
    albion::foliageexport::Instance parent, child, grandchild;
    parent.x = 10; parent.y = 20; parent.z = 30;
    albion::thingsexport::thingBasis(north, up, 0.06f, parent.m);
    const float dummy[12] = {0, 4, 0, -7, 0, 0, 0, 0, 9, 100, 200, 300};
    albion::thingsexport::childTransform(dummy, parent, 2, 5, child);
    CHECK(near(child.x, 4) && near(child.y, 8) && near(child.z, 48));
    CHECK(child.hasMatrix && near(child.scale, 0.1f));
    CHECK(near(child.m[0], 0) && near(child.m[1], -0.1f));
    CHECK(near(child.m[3], 0.1f) && near(child.m[4], 0) && near(child.m[8], 0.1f));
    const float next[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 100, 0, 0};
    albion::thingsexport::childTransform(next, child, 2, 1, grandchild);
    CHECK(near(grandchild.x, 4) && near(grandchild.y, -2) && near(grandchild.z, 48));
    CHECK(near(grandchild.scale, 0.02f) && near(grandchild.m[1], -0.02f));
}

// Water: ENGINE_THEME WaterHeight is a depth above the ground. On flat ground at
// 5, slot 1 (weight grows with x) declared a lake 2 deep must put a sheet 0.1
// below ground + smoothed depth over every vertex that carries the theme plus a
// 2-cell bank, with the depth fade ending at 0 on the dry side.
void testWater(const fs::path& dir) {
    const fs::path lev = writeSyntheticLev(dir / "flat.lev", 4, 3, [](int, int) { return 5.0f; });
    const auto level = forge::lev::File::open(lev);
    te::Options o; o.textures = false;
    te::ThemeLayer grass; grass.slot = 0; grass.resolved = true;
    te::ThemeLayer lake; lake.slot = 1; lake.resolved = true; lake.waterType = 1; lake.waterHeight = 2.0f;
    te::Scene s = te::buildMesh(level, o);
    s.themes = {grass, lake};
    te::buildWater(level, s, {{0, 0}, {1, 1}}, o);
    CHECK(s.water.wetVertices == 4 * 4);   // x = 1..4 carry slot-1 weight; x = 0 has none
    CHECK(!s.water.indices.empty() && s.water.iceIndices.empty());
    CHECK(s.water.ice.size() == s.water.positions.size() / 3 && s.water.fade.size() == s.water.ice.size());
    bool sane = true, sawRight = false;
    for (size_t i = 0; i + 2 < s.water.positions.size(); i += 3) {
        const int x = int(std::lround(s.water.positions[i]));
        const float h = s.water.positions[i + 1], f = s.water.fade[i / 3];
        // Column 4: window x 2..4, depths 1, 1.5, 2 -> level 6.5 -> vertex 6.4, fade (6.5-5)/2 = 0.75
        if (x == 4) { sawRight = true; if (std::fabs(h - 6.4f) > 0.05f || std::fabs(f - 0.75f) > 0.05f) sane = false; }
        if (h < 5.0f - 0.2f || h > 7.0f || f < 0.0f || f > 1.0f) sane = false;
    }
    CHECK(sawRight && sane);
    // A lake whose smoothed sheet is underground is not emitted.
    te::Scene s2 = te::buildMesh(level, o);
    te::ThemeLayer puddle = lake; puddle.waterHeight = 0.01f;
    s2.themes = {grass, puddle};
    te::buildWater(level, s2, {{0, 0}, {1, 1}}, o);
    CHECK(s2.water.wetVertices > 0 && s2.water.empty());
    // Dry map: no water at all.
    te::Scene dry = te::buildMesh(level, o);
    dry.themes = {grass, grass};
    te::buildWater(level, dry, {{0, 0}, {1, 1}}, o);
    CHECK(dry.water.empty() && dry.water.wetVertices == 0);
}

// The level document: byte-exact round trip, frame edits in retail spelling,
// duplicate/remove/place with undo, and the change summary.
void testLevelDocument() {
    auto near = [](float a, float b) { return std::fabs(a - b) < 1e-5f; };
    const std::string tng =
        "Version 2;\r\n"
        "XXXSectionStart NULL;\r\n"
        "\r\n"
        "NewThing Object;\r\n"
        "Player 4;\r\n"
        "UID 18446744073709551615;\r\n"
        "DefinitionType \"OBJECT_BARREL_01\";\r\n"
        "ScriptName NULL;\r\n"
        "StartCTCPhysicsStandard;\r\n"
        "PositionX 10.5;\r\n"
        "PositionY 20.25;\r\n"
        "PositionZ 3.0;\r\n"
        "RHSetForwardX 0.0;\r\n"
        "RHSetForwardY 1.0;\r\n"
        "RHSetForwardZ 0.0;\r\n"
        "RHSetUpX 0.0;\r\n"
        "RHSetUpY 0.0;\r\n"
        "RHSetUpZ 1.0;\r\n"
        "EndCTCPhysicsStandard;\r\n"
        "Health 6000.0;\r\n"
        "EndThing;\r\n"
        "\r\n"
        "NewThing Marker;\r\n"
        "UID 18446744073709551614;\r\n"
        "DefinitionType \"MARKER_BASIC\";\r\n"
        "ScriptName \"Start\";\r\n"
        "EndThing;\r\n"
        "\r\n"
        "XXXSectionEnd;\r\n";
    albion::editor::Document doc;
    std::string err;
    CHECK(doc.openText("Synthetic", tng, err));
    CHECK(doc.text() == tng);             // byte-exact while untouched
    CHECK(!doc.dirty());
    CHECK(doc.thingCount() == 2);
    const auto s0 = doc.summary(0);
    CHECK(s0.definition == "OBJECT_BARREL_01" && s0.hasFrame && s0.scriptName.empty());
    CHECK(doc.summary(1).scriptName == "\"Start\"" || doc.summary(1).scriptName == "Start");
    albion::editor::Frame f;
    CHECK(doc.frameOf(0, f));
    CHECK(near(f.pos[0], 10.5f) && near(f.forward[1], 1.0f) && near(f.up[2], 1.0f) && near(f.scale, 1.0f));

    // frame <-> matrix agree with thingBasis' convention and invert each other
    float m[16]; albion::editor::frameToMatrix(f, m);
    float basis[9]; const float fwd[3] = {0, 1, 0}, up[3] = {0, 0, 1};
    albion::thingsexport::thingBasis(fwd, up, 0.01f, basis);
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) CHECK(near(m[r * 4 + c], basis[r * 3 + c]));
    albion::editor::Frame back; CHECK(albion::editor::matrixToFrame(m, back));
    CHECK(near(back.pos[1], 20.25f) && near(back.forward[1], 1.0f) && near(back.scale, 1.0f));
    float inv[16], id[16]; CHECK(albion::editor::invert(m, inv)); albion::editor::multiply(m, inv, id);
    CHECK(near(id[0], 1) && near(id[5], 1) && near(id[10], 1) && near(id[15], 1) && near(id[12], 0));

    // move + rotate + scale: only the touched lines change, retail spelling
    f.pos[0] = 11.0f; f.forward[0] = 1; f.forward[1] = 0; f.scale = 2.5f;
    doc.setFrame(0, f);
    CHECK(doc.dirty());
    const std::string t1 = doc.text();
    CHECK(t1.find("PositionX 11.0;\r\n") != std::string::npos);
    CHECK(t1.find("RHSetForwardX 1.0;\r\n") != std::string::npos);
    CHECK(t1.find("ObjectScale 2.5;\r\n") != std::string::npos);
    CHECK(t1.find("EndCTCPhysicsStandard;\r\nHealth 6000.0;") != std::string::npos);   // trailing field order kept
    CHECK(t1.find("NewThing Marker;\r\nUID 18446744073709551614;") != std::string::npos);
    auto ch = doc.changes();
    CHECK(ch.size() == 1 && ch[0].rfind("moved OBJECT_BARREL_01", 0) == 0);

    // undo restores the exact bytes; redo re-applies
    CHECK(doc.undo()); CHECK(doc.text() == tng); CHECK(!doc.dirty());
    CHECK(doc.redo()); CHECK(doc.text() == t1);

    // duplicate: same block, fresh UID, right after the original
    const size_t d = doc.duplicate(0);
    CHECK(d == 1 && doc.thingCount() == 3);
    CHECK(doc.uidOf(1) != doc.uidOf(0) && doc.uidOf(1) != 0);
    CHECK(doc.summary(1).definition == "OBJECT_BARREL_01" && doc.summary(2).type == "Marker");
    CHECK((doc.uidOf(1) >> 32) == 0xFFFFFE00ull);   // retail high dword
    // remove the copy, undo brings it back at the same index
    const uint64_t dupUid = doc.uidOf(1);
    doc.remove(1);
    CHECK(doc.thingCount() == 2 && !doc.indexOfUid(dupUid));
    CHECK(doc.undo() && doc.thingCount() == 3 && doc.indexOfUid(dupUid) == 1);
    doc.remove(1);

    // place a new thing through thingplacer
    forge::thingplacer::Placement pl;
    pl.definitionType = "OBJECT_CRATE_01";
    pl.position = {1.0f, 2.0f, 3.0f};
    const size_t n = doc.place(pl);
    CHECK(n == 2 && doc.summary(n).definition == "OBJECT_CRATE_01");
    ch = doc.changes();
    CHECK(ch.size() == 2);   // moved barrel + added crate
    bool added = false; for (const auto& c : ch) added = added || c.rfind("added OBJECT_CRATE_01", 0) == 0;
    CHECK(added);
    // remove the original barrel: reported as removed
    doc.remove(0);
    ch = doc.changes();
    bool removed = false; for (const auto& c : ch) removed = removed || c.rfind("removed OBJECT_BARREL_01", 0) == 0;
    CHECK(removed);
    // setProperty + markSaved
    doc.setProperty(0, "ScriptName", "\"Exit\"");
    CHECK(doc.text().find("ScriptName \"Exit\";") != std::string::npos);
    doc.markSaved();
    CHECK(!doc.dirty() && doc.changes().empty());
    // a batch is one undo step: two moves + a duplicate, one undo restores all
    {
        while (doc.redo()) {}
        const std::string before = doc.text();
        const size_t count = doc.thingCount();
        doc.beginBatch();
        albion::editor::Frame g; CHECK(doc.frameOf(1, g)); g.pos[0] += 5; doc.setFrame(1, g);
        g.pos[1] += 5; doc.setFrame(1, g);
        doc.duplicate(1);
        doc.endBatch();
        CHECK(doc.thingCount() == count + 1);
        CHECK(doc.undo() && doc.text() == before && doc.thingCount() == count);
        CHECK(doc.redo() && doc.thingCount() == count + 1);
        doc.undo();
    }
    // a fragment keeps relative positions; paste lands its centroid on `at`, fresh UIDs,
    // ScriptName NULL; one undo step
    {
        const std::string before = doc.text();
        const size_t count = doc.thingCount();
        albion::editor::Frame a; CHECK(doc.frameOf(1, a));
        const size_t crate = doc.place([] { forge::thingplacer::Placement p; p.definitionType = "OBJECT_CRATE_02"; p.position = {10.0f, 0.0f, 0.0f}; return p; }());
        albion::editor::Frame c; CHECK(doc.frameOf(crate, c));
        const auto frag = doc.extract({1, crate});
        CHECK(frag.items.size() == 2 && frag.items[0].hasFrame && frag.items[1].hasFrame);
        CHECK(near(frag.centre[0], (a.pos[0] + c.pos[0]) * 0.5f));
        const float at[3] = {100.0f, 200.0f, 7.0f};
        const auto pasted = doc.paste(frag, at, false);
        CHECK(pasted.size() == 2 && doc.thingCount() == count + 3);
        albion::editor::Frame p0, p1; CHECK(doc.frameOf(pasted[0], p0) && doc.frameOf(pasted[1], p1));
        CHECK(near(p0.pos[0] - p1.pos[0], a.pos[0] - c.pos[0]));            // relative layout kept
        CHECK(near((p0.pos[0] + p1.pos[0]) * 0.5f, 100.0f) && near(p0.pos[1] + p1.pos[1], 400.0f));
        CHECK(doc.uidOf(pasted[0]) != doc.uidOf(1) && doc.uidOf(pasted[1]) != doc.uidOf(crate));
        CHECK(doc.summary(pasted[1]).definition == "OBJECT_CRATE_02");
        CHECK(doc.undo() && doc.thingCount() == count + 1);   // the paste was one step
        CHECK(doc.undo() && doc.text() == before);            // then the crate
    }
    // presets: save the selection as a file, list it, load it, paste it (round trip)
    {
        const fs::path dir = fs::temp_directory_path() / "atlas_preset_test";
        std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);
        const auto frag = doc.extract({0, 1});
        std::string err;
        CHECK(albion::editor::savePreset(dir / (albion::editor::presetSlug("My camp!") + ".preset.tng"), "My camp!", "two things", frag, err));
        const auto list = albion::editor::listPresets({dir / "nope", dir});
        CHECK(list.size() == 1 && list[0].name == "My camp!" && list[0].description == "two things" && list[0].things == 2 && list[0].user);
        albion::editor::Document::Fragment back;
        CHECK(albion::editor::loadPreset(list[0].file, back, err));
        CHECK(back.items.size() == 2 && near(back.centre[0], frag.centre[0]) && near(back.centre[1], frag.centre[1]));
        const size_t count = doc.thingCount();
        const float at[3] = {50.0f, 60.0f, 1.0f};
        const auto pasted = doc.paste(back, at, false);
        CHECK(pasted.size() == 2 && doc.thingCount() == count + 2);
        bool framed = false;
        for (size_t k = 0; k < pasted.size(); ++k) {
            albion::editor::Frame q;
            if (!frag.items[k].hasFrame) continue;
            CHECK(doc.frameOf(pasted[k], q));
            CHECK(near(q.pos[0] - 50.0f, frag.items[k].frame.pos[0] - frag.centre[0]));
            framed = true;
        }
        CHECK(framed);
        doc.undo();
        fs::remove_all(dir, ec);
    }
    // undo depth survives many edits
    for (int i = 0; i < 200; ++i) { f.pos[0] = float(i); doc.setFrame(1, f); }
    int undone = 0; while (doc.undo()) ++undone;
    CHECK(undone == 128);
}

// Terrain strokes on the synthetic LEV: raise, undo, walkable paint, loose save.
void testTerrainEditing(const fs::path& lev, const fs::path& dir) {
    auto near = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
    albion::editor::Document doc;
    std::string err;
    CHECK(doc.openText("Synthetic", "Version 2;\r\nXXXSectionStart NULL;\r\nXXXSectionEnd;\r\n", err));
    CHECK(doc.loadLevel(lev, err));
    CHECK(doc.hasTerrain() && doc.cellsX() == 5 && doc.cellsY() == 4);
    const float h0 = doc.terrain().heights[size_t(2) * 5 + 2];
    CHECK(near(*doc.terrainHeight(2.0f, 2.0f), h0));
    albion::editor::TerrainBrush b;
    b.mode = albion::editor::TerrainBrush::Mode::Raise; b.x = 2; b.y = 2; b.radius = 1.5f; b.strength = 4.0f;
    doc.beginStroke(b);
    CHECK(doc.strokeActive());
    doc.applyBrush(b, 0.5f);   // +2 units at the centre
    CHECK(near(*doc.terrainHeight(2.0f, 2.0f), h0 + 2.0f));
    doc.endStroke();
    CHECK(!doc.strokeActive() && doc.terrainDirty());
    CHECK(near(doc.terrain().heights[size_t(2) * 5 + 2], h0 + 2.0f));
    CHECK(near(doc.terrain().heights[0], 0.0f));   // far corner untouched
    // the .lev sees it too
    CHECK(near(doc.level()->heightAt(2, 2), h0 + 2.0f));
    // undo restores heights and the level; redo re-applies
    CHECK(doc.undo() && near(doc.terrain().heights[size_t(2) * 5 + 2], h0) && near(doc.level()->heightAt(2, 2), h0) && !doc.terrainDirty());
    CHECK(doc.redo() && near(doc.terrain().heights[size_t(2) * 5 + 2], h0 + 2.0f));
    // walkable paint
    b.mode = albion::editor::TerrainBrush::Mode::Blocked; b.radius = 0.6f; b.x = 1.5f; b.y = 1.5f;
    doc.beginStroke(b); doc.applyBrush(b, 0.1f); doc.endStroke();
    CHECK(doc.terrain().walkable[size_t(1) * 5 + 1] == 0 && !doc.level()->walkableAt(1, 1));
    CHECK(doc.level()->walkableAt(2, 2));   // even rows are walkable in the synthetic map
    // ground-theme paint: slot 1 painted over the slot-0 left edge keeps the 3-slot invariant
    CHECK(!doc.themesDirty());
    const uint64_t themeRev0 = doc.themeRevision();
    b.mode = albion::editor::TerrainBrush::Mode::Theme; b.themeIndex = 1; b.radius = 0.6f; b.x = 0; b.y = 0; b.strength = 10.0f;
    doc.beginStroke(b); doc.applyBrush(b, 1.0f); doc.endStroke();
    CHECK(doc.themesDirty() && doc.terrainDirty() && doc.themeRevision() == themeRev0 + 1);
    {
        const auto& t = doc.terrain();
        int sum = 0; bool hasSlot1 = false;
        for (int k = 0; k < 3; ++k) { sum += t.themeStrength[0][k]; hasSlot1 = hasSlot1 || (t.themeIndex[0][k] == 1 && t.themeStrength[0][k] > 0); }
        CHECK(sum == 255 && hasSlot1);
        CHECK(doc.level()->themeStrengthAt(0, 0, 0) == t.themeStrength[0][0]);   // mirrored into the .lev
        CHECK(t.themeIndex[size_t(3) * 5 + 4] == doc.terrain().themeIndex[size_t(3) * 5 + 4]);
    }
    CHECK(doc.undo() && !doc.themesDirty() && doc.themeRevision() == themeRev0 + 2);
    CHECK(doc.redo() && doc.themesDirty());
    // loose save under a scratch root, then reopen and compare
    const fs::path root = dir / "terrain_root";
    CHECK(doc.saveTerrainLoose(root, err));
    CHECK(!doc.terrainDirty());
    const auto saved = forge::lev::File::open(root / "data" / "Levels" / "FinalAlbion" / "Synthetic.lev");
    CHECK(near(saved.heightAt(2, 2), h0 + 2.0f) && !saved.walkableAt(1, 1) && saved.walkableAt(2, 2));
    bool savedSlot1 = false;
    for (int k = 0; k < 3; ++k) savedSlot1 = savedSlot1 || (saved.themeIndexAt(0, 0, k) == 1 && saved.themeStrengthAt(0, 0, k) > 0);
    CHECK(savedSlot1);
}

// The vanilla Themes-dialog tools (eyedropper, Replace pen, Flood Replace, Replace
// All) and the Height Toolbox's Draw Paths, on a 9x9-cell synthetic map: theme 1 in
// two blocks split by a theme-2 band, heights = x + 10 y.
void testThemeToolsAndPaths(const fs::path& dir) {
    namespace ed = albion::editor;
    // the pure blend swap: a duplicate after the swap merges, the sum stays 255
    {
        const auto b = forge::terrain::replaceThemeInBlend({{1, 3, 0}, {200, 55, 0}}, 1, 3);
        CHECK(b.indices[0] == 3 && b.strengths[0] == 255 && b.strengths[1] == 0 && b.strengths[2] == 0);
        const auto same = forge::terrain::replaceThemeInBlend({{4, 5, 6}, {100, 100, 55}}, 1, 3);
        CHECK(same.indices[0] == 4 && same.strengths[0] == 100 && same.strengths[2] == 55);
    }
    const fs::path path = writeSyntheticLev(dir / "themes.lev", 8, 8, [](int x, int y) { return float(x + 10 * y); });
    {
        auto lev = forge::lev::File::open(path);
        for (int y = 0; y < lev.cellsY(); ++y)
            for (int x = 0; x < lev.cellsX(); ++x) {
                const uint8_t slot = (x >= 3 && x <= 5) ? 2 : 1;
                lev.setThemeBlendAt(x, y, {slot, 0, 0}, {255, 0, 0});
            }
        lev.save(path);
    }
    ed::Document doc;
    std::string err;
    CHECK(doc.openText("Themes", "Version 2;\r\nXXXSectionStart NULL;\r\nXXXSectionEnd;\r\n", err));
    CHECK(doc.loadLevel(path, err));
    const int cx = doc.cellsX();
    auto themeAt = [&](int x, int y) { return doc.terrain().themeIndex[size_t(y) * cx + x][0]; };
    // eyedropper
    CHECK(doc.dominantThemeAt(1.5f, 1.5f) == std::optional<uint8_t>(1));
    CHECK(doc.dominantThemeAt(4.2f, 7.9f) == std::optional<uint8_t>(2));
    CHECK(!doc.dominantThemeAt(-1.0f, 2.0f));
    // flood replace stays on the clicked patch; the band blocks it
    const uint64_t rev0 = doc.themeRevision();
    const size_t flooded = doc.replaceTheme(1, 7, ed::Document::ReplaceScope::Connected, 1.5f, 1.5f);
    CHECK(flooded == size_t(3 * doc.cellsY()));
    CHECK(themeAt(0, 0) == 7 && themeAt(2, 8) == 7 && themeAt(4, 4) == 2 && themeAt(7, 4) == 1);
    CHECK(doc.themeRevision() == rev0 + 1 && doc.themesDirty());
    CHECK(doc.level()->themeIndexAt(0, 0, 0) == 7);                  // mirrored into the .lev
    CHECK(doc.replaceTheme(1, 7, ed::Document::ReplaceScope::Connected, 4.5f, 4.5f) == 0);   // clicked cell lacks theme 1
    // replace all reaches the far block; one undo step each
    CHECK(doc.replaceTheme(1, 7, ed::Document::ReplaceScope::All) == size_t(3 * doc.cellsY()));
    CHECK(themeAt(8, 8) == 7);
    CHECK(doc.undo() && themeAt(8, 8) == 1 && themeAt(0, 0) == 7);
    CHECK(doc.undo() && themeAt(0, 0) == 1);
    // the Replace pen: only cells under the brush
    ed::TerrainBrush b;
    b.mode = ed::TerrainBrush::Mode::ReplaceTheme; b.replaceFrom = 2; b.themeIndex = 9; b.x = 4.5f; b.y = 4.5f; b.radius = 1.0f;
    doc.beginStroke(b); doc.applyBrush(b, 0.1f); doc.endStroke();
    CHECK(themeAt(4, 4) == 9 && themeAt(4, 8) == 2 && themeAt(0, 0) == 1);
    CHECK(doc.undo() && themeAt(4, 4) == 2);
    // Draw Paths: vertices within the radius of (1,1)->(6,1) take the interpolated end heights (11 -> 16)
    auto h = [&](int x, int y) { return doc.terrain().heights[size_t(y) * cx + x]; };
    const size_t pathed = doc.drawPath(1.0f, 1.0f, 6.0f, 1.0f, 1.0f);
    CHECK(pathed > 0);
    CHECK(std::fabs(h(3, 2) - 13.0f) < 1e-4f && std::fabs(h(3, 0) - 13.0f) < 1e-4f && std::fabs(h(6, 1) - 16.0f) < 1e-4f);
    CHECK(std::fabs(h(3, 4) - 43.0f) < 1e-4f);                          // outside the radius
    CHECK(std::fabs(doc.level()->heightAt(3, 2) - 13.0f) < 1e-4f);
    CHECK(doc.undo() && std::fabs(h(3, 2) - 23.0f) < 1e-4f);
}

// The .lev game-map grid (FableWin CGameMapCell): environment blend + sound per
// 4x4 cells, the atmos palette and the sound list; edits patch in place.
void testGameMapGrid(const fs::path& dir) {
    const fs::path path = writeGameMapLev(dir / "gamemap.lev");
    auto lev = forge::lev::File::open(path);
    CHECK(lev.hasGameMap() && lev.gameMapWidth() == 2 && lev.gameMapHeight() == 2);
    CHECK((lev.soundThemes() == std::vector<std::string>{"SOUND_A", "SOUND_B"}));
    CHECK(lev.atmosThemes().size() == 256 && lev.atmosThemes()[1].name == "ENVIRONMENT_UNDERTREES" && lev.atmosThemes()[1].value == 77);
    CHECK(lev.soundAt(0, 0) == 1 && lev.soundAt(1, 1) == 2);
    CHECK(lev.atmosAt(0, 0).indices[0] == 0 && lev.atmosAt(0, 0).strengths[0] == 255 && lev.atmosAt(0, 0).strengths[2] == 0);
    lev.setSoundAt(1, 0, 2);
    lev.setAtmosAt(0, 1, {{1, 0, 0}, {200, 55, 0}});
    lev.setAtmosTheme(2, "ENVIRONMENT_HAUNTED", 88);
    bool threw = false;
    try { lev.setSoundAt(0, 0, 3); } catch (const std::exception&) { threw = true; }   // past the sound list
    CHECK(threw);
    const fs::path out = dir / "gamemap_out.lev";
    lev.save(out);
    const auto before = readAll(path), after = readAll(out);
    CHECK(before.size() == after.size());           // in place: nothing moves
    const auto re = forge::lev::File::open(out);
    CHECK(re.hasGameMap() && re.soundAt(1, 0) == 2 && re.soundAt(0, 0) == 1);
    CHECK(re.atmosAt(0, 1).indices[0] == 1 && re.atmosAt(0, 1).strengths[0] == 200 && re.atmosAt(0, 1).strengths[1] == 55);
    CHECK(re.atmosThemes()[2].name == "ENVIRONMENT_HAUNTED" && re.atmosThemes()[2].value == 88);
    size_t diff = 0;
    for (size_t i = 0; i < before.size(); ++i) diff += before[i] != after[i];
    CHECK(diff > 0 && diff < 40);                    // only the touched cells + palette slot
    // the older synthetic writer (fileVersion 1) has no grid
    CHECK(!forge::lev::File::open(writeSyntheticLev(dir / "nogrid.lev", 4, 4, [](int, int) { return 0.0f; })).hasGameMap());
}

// Environment + sound painting through the document (vanilla ENVIRONMENT tab and
// Survey > Sounds): brushes on the 4x4 game-map cells, a new environment theme in
// a free atmos slot, dirty tracking, the loose save, undo.
void testEnvironmentAndSoundPaint(const fs::path& dir) {
    namespace ed = albion::editor;
    const fs::path lev = writeGameMapLev(dir / "envsound.lev");
    ed::Document doc;
    std::string err;
    CHECK(doc.openText("EnvSound", "Version 2;\r\nXXXSectionStart NULL;\r\nXXXSectionEnd;\r\n", err));
    CHECK(doc.loadLevel(lev, err));
    CHECK(doc.hasGameMap() && doc.soundThemes().size() == 2);
    CHECK((doc.environmentAndSoundAt(1.0f, 1.0f) == std::optional<std::pair<uint8_t, uint8_t>>(std::make_pair(uint8_t(0), uint8_t(1)))));
    ed::TerrainBrush b;
    b.mode = ed::TerrainBrush::Mode::Sound; b.themeIndex = 2; b.x = 2.0f; b.y = 2.0f; b.radius = 1.0f;   // only game cell (0,0)
    doc.beginStroke(b); doc.applyBrush(b, 0.1f); doc.endStroke();
    CHECK(doc.terrainDirty());
    CHECK(doc.environmentAndSoundAt(1.0f, 1.0f)->second == 2 && doc.environmentAndSoundAt(5.0f, 1.0f)->second == 1);
    b.mode = ed::TerrainBrush::Mode::Environment; b.themeIndex = 1; b.x = 6.0f; b.y = 6.0f; b.strength = 10.0f;   // game cell (1,1)
    doc.beginStroke(b); doc.applyBrush(b, 1.0f); doc.endStroke();
    CHECK(doc.environmentAndSoundAt(6.0f, 6.0f)->first == 1 && doc.environmentAndSoundAt(1.0f, 1.0f)->first == 0);
    CHECK(doc.addEnvironmentTheme("ENVIRONMENT_HAUNTED", 88) == 2);
    CHECK(doc.addEnvironmentTheme("ENVIRONMENT_UNDERTREES", 77) == 1);    // already there
    CHECK(doc.addEnvironmentTheme("ENVIRONMENT_X", 0) == -1);             // a 0 value means "no atmos"
    const fs::path root = dir / "envsound_root";
    CHECK(doc.saveTerrainLoose(root, err));
    const auto saved = forge::lev::File::open(root / "data" / "Levels" / "FinalAlbion" / "EnvSound.lev");
    CHECK(saved.hasGameMap() && saved.soundAt(0, 0) == 2 && saved.soundAt(1, 0) == 1);
    {
        const auto a = saved.atmosAt(1, 1);   // the painted theme holds the whole blend, in whichever slot it took
        int best = 0; for (int k = 1; k < 3; ++k) if (a.strengths[k] > a.strengths[best]) best = k;
        CHECK(a.indices[best] == 1 && a.strengths[best] == 255);
    }
    CHECK(saved.atmosThemes()[2].name == "ENVIRONMENT_HAUNTED");
    CHECK(!doc.terrainDirty());
    CHECK(doc.undo() && doc.undo() && doc.environmentAndSoundAt(6.0f, 6.0f)->first == 0);
    // camera passability (+16): blocked cells take the brush; walkable cells stay passable (the saver's OR)
    CHECK(doc.terrain().cameraPassable.size() == size_t(doc.cellsX()) * doc.cellsY());
    b.mode = ed::TerrainBrush::Mode::CameraPass; b.x = 4.5f; b.y = 4.5f; b.radius = 0.6f;
    doc.beginStroke(b); doc.applyBrush(b, 0.1f); doc.endStroke();
    b.mode = ed::TerrainBrush::Mode::CameraBlock; b.x = 1.5f; b.y = 1.5f;
    doc.beginStroke(b); doc.applyBrush(b, 0.1f); doc.endStroke();
    CHECK(doc.level()->cameraPassableAt(4, 4) && doc.level()->cameraPassableAt(1, 1));   // (1,1) is walkable: stays passable
    CHECK(doc.terrain().cameraPassable[size_t(1) * doc.cellsX() + 1] == 0 && doc.terrainDirty());
}

// The vanilla fractal generator port against the static-RE reference
// (seed-0 tables and heights computed by the recovered algorithm).
void testFractal() {
    namespace fr = forge::fractal;
    const fr::Generator g;   // dialog defaults
    const int p16[16] = {0, 86, 114, 50, 240, 208, 77, 82, 180, 118, 6, 211, 205, 182, 55, 144};
    bool permOk = true;
    for (int i = 0; i < 16; ++i) permOk = permOk && g.permutation()[size_t(i)] == p16[i];
    CHECK(permOk);
    CHECK(std::fabs(g.gradients()[0][0] - (-0.6872546076774597f)) < 1e-7f && std::fabs(g.gradients()[0][1] - 0.04230388626456261f) < 1e-7f);
    CHECK(std::fabs(g.gradients()[1][0] - 0.7492795586585999f) < 1e-7f && std::fabs(g.gradients()[1][1] - 0.662253201007843f) < 1e-7f);
    const uint8_t t8[8] = {106, 103, 100, 98, 95, 92, 90, 87};
    bool tabOk = true;
    for (int i = 0; i < 8; ++i) tabOk = tabOk && fr::Generator::rsqrtTable()[size_t(i)] == t8[i];
    CHECK(tabOk);
    CHECK(std::fabs(g.perlin(0.3f, 0.7f) - (-0.33992505073547363f)) < 1e-6f);
    CHECK(std::fabs(g.perlin(10.25f, 3.5f) - 0.038245201110839844f) < 1e-6f);
    struct Case { double x, y; float h, hFall; };
    const Case cases[] = {{0, 0, 0.5117537975311279f, 0.0f}, {100, 200, 0.4384312629699707f, 0.0f},
                          {2048, 2048, 0.5116974711418152f, 0.5116974711418152f}, {3000, 1500, 0.8535566926002502f, 0.5033801198005676f},
                          {517, 3999, 1.0f, 0.0f}};
    fr::Params fall; fall.useFalloff = true; fall.startFalloff = 500; fall.endFalloff = 1500;
    const fr::Generator gf(fall);
    for (const auto& c : cases) {
        CHECK(std::fabs(g.heightAt(c.x, c.y) - c.h) < 1e-5f);
        CHECK(std::fabs(gf.heightAt(c.x, c.y) - c.hFall) < 1e-5f);
    }
    fr::Params ed; ed.lacunarity = 2.7; ed.dimension = 0.2; ed.octaves = 18.5; ed.worldScaler = 0.12; ed.mapX = 679000; ed.mapY = 664100;
    CHECK(std::fabs(fr::Generator(ed).heightAt(123, 456) - 0.5859578847885132f) < 1e-5f);   // fractional octaves
    // applied to a document: absolute heights = fractal * scale at world = origin + local
    const fs::path lev = writeSyntheticLev(fs::temp_directory_path() / "fableforge_fractal.lev", 4, 4, [](int, int) { return 3.0f; });
    albion::editor::Document doc;
    std::string err;
    CHECK(doc.openText("Fractal", "Version 2;\r\nXXXSectionStart NULL;\r\nXXXSectionEnd;\r\n", err) && doc.loadLevel(lev, err));
    fr::Params ap; ap.scale = 70.0;
    CHECK(doc.applyFractal(ap) > 0);
    CHECK(std::fabs(doc.terrain().heights[0] - float(double(g.heightAt(0, 0)) * 70.0)) < 1e-3f);
    CHECK(std::fabs(doc.terrain().heights[size_t(2) * 5 + 3] - float(double(g.heightAt(3, 2)) * 70.0)) < 1e-3f);
    CHECK(doc.undo() && doc.terrain().heights[0] == 3.0f);
}

// Adding a sound name re-lays the .lev after the list: a 32x32 map with a generated
// navigation tree keeps its cells and nodes, and every absolute offset follows.
void testAddSoundTheme(const fs::path& dir) {
    const fs::path base = writeSyntheticLev(dir / "sound32.lev", 32, 32, [](int x, int) { return float(x); });
    const auto generated = forge::navmesh::generateTerrain(forge::lev::File::open(base));
    { std::ofstream(base, std::ios::binary | std::ios::trunc).write(reinterpret_cast<const char*>(generated.levBytes.data()), std::streamsize(generated.levBytes.size())); }
    const auto before = forge::lev::File::open(base);
    auto after = before;
    CHECK(after.addSoundTheme("SOUND_THEME_WOODLAND") == 1);
    CHECK(after.addSoundTheme("SOUND_THEME_WOODLAND") == 1);          // found, not added twice
    CHECK(after.addSoundTheme("SOUND_THEME_OCEAN") == 2);
    const fs::path out = dir / "sound32_out.lev";
    after.save(out);
    const auto re = forge::lev::File::open(out);
    CHECK((re.soundThemes() == std::vector<std::string>{"SOUND_THEME_WOODLAND", "SOUND_THEME_OCEAN"}));
    CHECK(readAll(out).size() == readAll(base).size() + (4 + 20) + (4 + 17));
    CHECK(re.heightAt(7, 3) == before.heightAt(7, 3) && re.walkableAt(5, 5) == before.walkableAt(5, 5));
    const auto na = forge::navmesh::parseNavigation(before), nb = forge::navmesh::parseNavigation(re);
    CHECK(na.sections.size() == nb.sections.size() && !nb.sections.empty());
    for (size_t s = 0; s < na.sections.size() && s < nb.sections.size(); ++s)
        CHECK(na.sections[s].nodes.size() == nb.sections[s].nodes.size() && na.sections[s].regionCount == nb.sections[s].regionCount);
    const auto bytes = readAll(out);
    CHECK(u32(bytes, re.navigationOffset()) == re.navSections()[0].offset);                 // the table's end
    CHECK(u32(bytes, re.navSections().back().offset) == uint32_t(bytes.size()));            // the last section's end
}

// Quest sections (vanilla Quests dialog): list, add (valid names, no duplicates),
// place into the current section, move a thing between sections, undo.
void testQuestSections() {
    namespace ed = albion::editor;
    const std::string tng =
        "Version 2;\r\nXXXSectionStart NULL;\r\n"
        "NewThing Object;\r\nUID 100;\r\nDefinitionType \"OBJECT_BARREL_01\";\r\nScriptName NULL;\r\nEndThing;\r\n\r\n"
        "XXXSectionEnd;\r\n\r\nXXXSectionStart Q_TEST;\r\n"
        "NewThing Object;\r\nUID 200;\r\nDefinitionType \"OBJECT_CRATE_01\";\r\nScriptName NULL;\r\nEndThing;\r\n\r\n"
        "XXXSectionEnd;\r\n";
    ed::Document doc;
    std::string err;
    CHECK(doc.openText("Sections", tng, err));
    CHECK((doc.sections() == std::vector<std::string>{"NULL", "Q_TEST"}));
    CHECK(doc.sectionOf(0) == "NULL" && doc.sectionOf(1) == "Q_TEST");
    CHECK((doc.thingSections() == std::vector<std::string>{"NULL", "Q_TEST"}));
    CHECK(!doc.addSection("bad name") && !doc.addSection("q_test") && !doc.addSection(""));
    CHECK(doc.addSection("MY_QUEST") && doc.sections().size() == 3);
    CHECK(doc.text().find("XXXSectionStart MY_QUEST;\r\nXXXSectionEnd;\r\n") != std::string::npos);
    doc.setPlacementSection("MY_QUEST");
    forge::thingplacer::Placement pl;
    pl.definitionType = "OBJECT_CRATE_02";
    pl.position = {1.0f, 2.0f, 3.0f};
    const size_t n = doc.place(pl);
    CHECK(doc.sectionOf(n) == "MY_QUEST" && doc.summary(n).definition == "OBJECT_CRATE_02");
    const auto moved = doc.moveToSection(n, "null");
    CHECK(moved && doc.sectionOf(*moved) == "NULL" && doc.summary(*moved).definition == "OBJECT_CRATE_02");
    CHECK(!doc.moveToSection(0, "NOPE"));
    CHECK(doc.undo() && doc.sectionOf(2) == "MY_QUEST");        // the move is one step
    doc.setPlacementSection("GONE");                              // unknown: falls back to NULL
    const size_t m = doc.place(pl);
    CHECK(doc.sectionOf(m) == "NULL");
    // a file without sections gets NULL first, then the new one
    ed::Document bare;
    CHECK(bare.openText("Bare", "Version 2;\r\n", err));
    CHECK(bare.addSection("Q_NEW") && (bare.sections() == std::vector<std::string>{"NULL", "Q_NEW"}));
}

// The per-component property grid (vanilla Thing Properties tabs): kinds, the
// hidden fields, typed edits refused when they do not fit, one undo step each.
void testThingProperties() {
    namespace ed = albion::editor;
    using K = ed::Document::PropertyRow::Kind;
    const std::string tng =
        "Version 2;\r\nXXXSectionStart NULL;\r\n"
        "NewThing Object;\r\nUID 100;\r\nDefinitionType \"OBJECT_DOOR\";\r\nScriptName NULL;\r\nScriptData \"hi\";\r\n"
        "StartCTCPhysicsStandard;\r\nPositionX 1.0;\r\nEndCTCPhysicsStandard;\r\n"
        "StartCTCDoor;\r\nStartOpen FALSE;\r\nLockLevel 2;\r\nEndCTCDoor;\r\n"
        "StartCTCOwnedEntity;\r\nOwnerUID 5;\r\nEndCTCOwnedEntity;\r\n"
        "Health 1.0;\r\nEndThing;\r\n\r\nXXXSectionEnd;\r\n";
    ed::Document doc;
    std::string err;
    CHECK(doc.openText("Props", tng, err));
    const auto rows = doc.propertiesOf(0);
    auto find = [&](const std::string& ctc, const std::string& key) -> const ed::Document::PropertyRow* {
        for (const auto& r : rows) if (r.ctc == ctc && r.key == key) return &r;
        return nullptr;
    };
    CHECK(find("", "ScriptName") && find("", "ScriptName")->kind == K::Raw);
    CHECK(find("", "ScriptData") && find("", "ScriptData")->kind == K::String);
    CHECK(find("", "Health") && find("", "Health")->kind == K::Float);
    CHECK(find("CTCDoor", "StartOpen") && find("CTCDoor", "StartOpen")->kind == K::Bool);
    CHECK(find("CTCDoor", "LockLevel") && find("CTCDoor", "LockLevel")->kind == K::Int);
    CHECK(!find("", "UID") && !find("", "DefinitionType") && !find("CTCPhysicsStandard", "PositionX") && !find("CTCOwnedEntity", "OwnerUID"));
    CHECK(doc.setPropertyValue(0, "CTCDoor", "StartOpen", "TRUE"));
    CHECK(doc.text().find("StartOpen TRUE;") != std::string::npos);
    CHECK(!doc.setPropertyValue(0, "CTCDoor", "StartOpen", "3"));      // a bool stays a bool
    CHECK(!doc.setPropertyValue(0, "CTCDoor", "LockLevel", "2.5"));    // an int stays an int
    CHECK(doc.setPropertyValue(0, "", "Health", "3"));                 // a float takes an int
    CHECK(!doc.setPropertyValue(0, "", "ScriptData", "no quotes"));
    CHECK(doc.setPropertyValue(0, "", "ScriptName", "MyDoor"));
    CHECK(!doc.setPropertyValue(0, "", "ScriptName", "a;b"));          // would break the line
    CHECK(!doc.setPropertyValue(0, "CTCOwnedEntity", "OwnerUID", "7"));  // links are not here
    CHECK(doc.undo() && doc.text().find("ScriptName NULL;") != std::string::npos);
    CHECK(doc.undo() && doc.text().find("Health 1.0;") != std::string::npos);
}

// Links between things (vanilla attach modes): a villager's village / owner / home,
// listed, validated against the target's kind, set, cleared and undone.
void testThingLinks() {
    namespace ed = albion::editor;
    const std::string tng =
        "Version 2;\r\nXXXSectionStart NULL;\r\n"
        "NewThing AICreature;\r\nUID 100;\r\nDefinitionType \"CREATURE_VILLAGER\";\r\nScriptName NULL;\r\n"
        "StartCTCVillageMember;\r\nVillageUID 0;\r\nEndCTCVillageMember;\r\n"
        "StartCTCOwnedEntity;\r\nVersionNumber 1;\r\nEndCTCOwnedEntity;\r\n"
        "HomeBuildingUID 0;\r\nEndThing;\r\n\r\n"
        "NewThing Village;\r\nUID 200;\r\nDefinitionType \"VILLAGE_OAKVALE\";\r\nScriptName NULL;\r\n"
        "StartCTCVillage;\r\nEndCTCVillage;\r\nEndThing;\r\n\r\n"
        "NewThing Building;\r\nUID 300;\r\nDefinitionType \"BUILDING_HOUSE\";\r\nScriptName NULL;\r\nEndThing;\r\n\r\n"
        "XXXSectionEnd;\r\n";
    ed::Document doc;
    std::string err;
    CHECK(doc.openText("Links", tng, err));
    auto links = doc.linksOf(0);
    CHECK(links.size() == 3);
    CHECK(links[0].field == "VillageUID" && links[0].target == 0 && !links[0].targetIndex);
    CHECK(links[1].field == "OwnerUID" && links[1].target == 0);   // component present, field absent = 0
    CHECK(links[2].field == "HomeBuildingUID" && links[2].ctc.empty());
    CHECK(doc.linksOf(2).empty());
    // only a village fits VillageUID, only a building fits HomeBuildingUID
    CHECK(doc.linkTargetFits(links[0], 1) && !doc.linkTargetFits(links[0], 2));
    CHECK(doc.linkTargetFits(links[2], 2) && !doc.linkTargetFits(links[2], 1));
    CHECK(doc.setLink(0, "CTCVillageMember", "VillageUID", 200));
    CHECK(doc.setLink(0, "CTCOwnedEntity", "OwnerUID", 300));        // inserted inside the block
    CHECK(doc.setLink(0, "", "HomeBuildingUID", 300));
    links = doc.linksOf(0);
    CHECK(links[0].target == 200 && links[0].targetIndex == std::optional<size_t>(1));
    CHECK(links[1].target == 300 && links[2].targetIndex == std::optional<size_t>(2));
    CHECK(doc.text().find("OwnerUID 300;\r\nEndCTCOwnedEntity;") != std::string::npos);
    CHECK(!doc.setLink(2, "CTCVillageMember", "VillageUID", 200));   // the building has no such link
    CHECK(doc.undo() && doc.linksOf(0)[2].target == 0);
    CHECK(doc.setLink(0, "CTCVillageMember", "VillageUID", 0) && doc.linksOf(0)[0].target == 0);
}

// A map of another world (vanilla File > Load World): the document reads the .tng
// beside the .lev, takes its origin/slot from that .wld, and every save -- objects,
// "deploy", terrain -- goes back to those two files, never into FinalAlbion\.
void testExternalWorld(const fs::path& dir) {
    namespace ed = albion::editor;
    const fs::path root = dir / "ext_world_root";
    const fs::path levels = root / "data" / "Levels";
    fs::create_directories(levels / "MyWorld");
    const fs::path lev = writeSyntheticLev(levels / "MyWorld" / "Foo.lev", 4, 4, [](int, int) { return 2.0f; });
    const fs::path tng = levels / "MyWorld" / "Foo.tng";
    { std::ofstream(tng, std::ios::binary) << "Version 2;\r\nXXXSectionStart NULL;\r\nXXXSectionEnd;\r\n"; }
    const fs::path wld = levels / "MyWorld.wld";
    { std::ofstream(wld, std::ios::binary) << "START_INITIAL_QUESTS;\r\nEND_INITIAL_QUESTS;\r\n\r\nMapUIDCount 2;\r\nThingManagerUIDCount 10;\r\n"
        "NewMap 7;\r\nMapX 320;\r\nMapY 640;\r\nLevelName \"MyWorld\\Foo.lev\";\r\nLevelScriptName \"Foo\";\r\nMapUID 1;\r\nIsSea FALSE;\r\nLoadedOnPlayerProximity TRUE;\r\nEndMap;\r\n\r\n"
        "NewRegion 1;\r\nRegionName \"Mine\";\r\nNewDisplayName \"Mine\";\r\nRegionDef \"\";\r\nContainsMap \"MyWorld\\Foo.lev\";\r\nEndRegion;\r\n"; }
    ed::Document doc;
    std::string err;
    const ed::Document::ExternalWorld ext{wld, tng};
    CHECK(doc.open(root, "Foo", lev, err, &ext));
    CHECK(doc.external() && doc.hasTerrain() && doc.worldSlot() == 7);
    ed::TerrainBrush b;
    b.mode = ed::TerrainBrush::Mode::Raise; b.x = 2; b.y = 2; b.radius = 1.5f; b.strength = 4.0f;
    doc.beginStroke(b); doc.applyBrush(b, 0.5f); doc.endStroke();
    CHECK(doc.saveTerrainLoose(root, err));
    CHECK(std::fabs(forge::lev::File::open(lev).heightAt(2, 2) - 4.0f) < 1e-4f);
    CHECK(doc.deployWad(root, err));          // the loose .tng beside the .lev is the deploy
    std::vector<std::string> notes;
    CHECK(doc.deployTerrain(root, notes, err));   // no STB for another world: loose .lev only
    CHECK(!notes.empty() && notes.back().find("not re-baked") != std::string::npos);
    CHECK(!fs::exists(levels / "FinalAlbion"));   // nothing written into the game's world
    CHECK(fs::exists(tng));
    // a missing .tng is an error, not a silent FinalAlbion lookup
    ed::Document other;
    const ed::Document::ExternalWorld none{wld, levels / "MyWorld" / "Nope.tng"};
    CHECK(!other.open(root, "Nope", lev, err, &none) && err.find("Nope.tng") != std::string::npos);
}

// A walkable-paint stroke patches the level's navigation quadtree for the
// touched cells only: a 32x32 synthetic map gets a generated tree, one cell is
// painted blocked and one opened, and the saved .lev's tree must drop / gain
// exactly those leaves while the rest of the records stay put.
void testNavPatch(const fs::path& dir) {
    // 32x32 map, every cell walkable except column 5 (a wall with a gap), flat
    const fs::path base = writeSyntheticLev(dir / "nav32.lev", 32, 32, [](int, int) { return 1.0f; });
    auto lev = forge::lev::File::open(base);
    for (int y = 0; y < lev.cellsY(); ++y)
        for (int x = 0; x < lev.cellsX(); ++x) lev.setWalkableAt(x, y, x != 5 || y == 25);   // wall with one gap, so both sides are one component
    lev.save(base);
    const auto generated = forge::navmesh::generateTerrain(forge::lev::File::open(base));
    { std::ofstream(base, std::ios::binary | std::ios::trunc).write(reinterpret_cast<const char*>(generated.levBytes.data()), std::streamsize(generated.levBytes.size())); }
    const auto before = forge::navmesh::parseNavigation(forge::lev::File::open(base));
    CHECK(before.sections.size() == 1 && before.sections[0].layerCount == 1);
    const size_t recordsBefore = before.sections[0].nodes.size();

    albion::editor::Document doc;
    std::string err;
    CHECK(doc.openText("nav32", "Version 2;\r\nXXXSectionStart NULL;\r\nXXXSectionEnd;\r\n", err));
    CHECK(doc.loadLevel(base, err));
    albion::editor::TerrainBrush b;
    b.mode = albion::editor::TerrainBrush::Mode::Blocked; b.radius = 0.4f; b.x = 20.5f; b.y = 12.5f; b.strength = 1;
    doc.beginStroke(b); doc.applyBrush(b, 0.1f); doc.endStroke();
    b.mode = albion::editor::TerrainBrush::Mode::Walkable; b.x = 5.5f; b.y = 7.5f;
    doc.beginStroke(b); doc.applyBrush(b, 0.1f); doc.endStroke();
    CHECK(!doc.terrain().walkable[size_t(12) * 33 + 20] && doc.terrain().walkable[size_t(7) * 33 + 5]);
    const fs::path root = dir / "nav_root";
    std::vector<std::string> notes;
    CHECK(doc.saveTerrainLoose(root, err, &notes));
    CHECK(!notes.empty() && notes[0].rfind("navigation: 2 cell(s) changed", 0) == 0);
    const auto saved = forge::lev::File::open(root / "data" / "Levels" / "FinalAlbion" / "nav32.lev");
    CHECK(!saved.walkableAt(20, 12) && saved.walkableAt(5, 7));
    const auto after = forge::navmesh::parseNavigation(saved);
    auto coveredBy = [&](float x, float y) {
        int hits = 0;
        for (const auto& n : after.sections[0].nodes) {
            if (n.marker || !n.leaf) continue;
            const float half = 32.0f / float(1 << n.level) * 0.5f;
            if (x > n.cx - half && x < n.cx + half && y > n.cy - half && y < n.cy + half) ++hits;
        }
        return hits;
    };
    CHECK(coveredBy(20.5f, 12.5f) == 0);   // the blocked cell lost its leaf
    CHECK(coveredBy(5.5f, 7.5f) == 1);     // the opened wall cell has exactly one
    CHECK(coveredBy(20.5f, 13.5f) == 1 && coveredBy(4.5f, 7.5f) == 1);
    // the opened cell joins its neighbours' region and links both sides of the wall
    int32_t regionLeft = 0, regionRight = 0, regionNew = 0; size_t newNeighbours = 0;
    for (const auto& n : after.sections[0].nodes) {
        if (n.marker || !n.leaf) continue;
        const float half = 32.0f / float(1 << n.level) * 0.5f;
        auto has = [&](float x, float y) { return x > n.cx - half && x < n.cx + half && y > n.cy - half && y < n.cy + half; };
        if (has(4.5f, 7.5f)) regionLeft = n.region;
        if (has(6.5f, 7.5f)) regionRight = n.region;
        if (has(5.5f, 7.5f)) { regionNew = n.region; newNeighbours = n.neighbours.size(); }
    }
    CHECK(regionNew == regionLeft && regionNew == regionRight && newNeighbours == 2);
    // a second save with nothing changed is byte-stable
    CHECK(doc.saveTerrainLoose(root, err, &notes));
    const auto again = forge::lev::File::open(root / "data" / "Levels" / "FinalAlbion" / "nav32.lev");
    CHECK(again.originalBytes() == saved.originalBytes());
    CHECK(after.sections[0].nodes.size() + 0 >= recordsBefore - 1);   // one leaf lost, one gained (+ any split internals)
}

// FinalAlbion.gtg: byte-exact round trip of a retail-shaped file, an entrance added to a
// missing slot (inserted in slot order), moved in place on the second call, retail
// sections untouched; the real retail file round-trips too when an install is at hand.
void testGtg(const fs::path& dir) {
    auto near = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
    const std::string retail =
        "NEWMAP 1\nVersion 2;\n\nXXXSectionStart NULL;\n\nNewThing Thing;\nPlayer 4;\nUID 18446741874686296173;\nDefinitionType \"REGION_ENTRANCE_POINT\";\nScriptName NULL;\nScriptData \"NULL\";\n"
        "ThingGamePersistent FALSE;\nThingLevelPersistent FALSE;\nStartCTCPhysicsStandard;\nPositionX 44.759766;\nPositionY 80.547852;\nPositionZ 33.785801;\nRHSetForwardX 0.773852;\nRHSetForwardY -0.633358;\nRHSetForwardZ 0.0;\n"
        "RHSetUpX 0.000219;\nRHSetUpY 0.000267;\nRHSetUpZ 0.999994;\nEndCTCPhysicsStandard;\nStartCTCDRegionEntrance;\nActive TRUE;\nEndCTCDRegionEntrance;\nEndThing;\n\nXXXSectionEnd;\n\n\nENDMAP\n"
        "NEWMAP 3\nVersion 2;\n\nENDMAP\nNEWMAP 62\nVersion 2;\n\nENDMAP\n";
    const auto f = albion::editor::GtgFile::parse(retail);
    CHECK(f.sections.size() == 3 && f.sections[0].slot == 1 && f.sections[2].slot == 62 && f.serialize() == retail);
    CHECK(f.maxUid() == 18446741874686296173ull);
    const fs::path root = dir / "gtg_root";
    std::error_code ec; fs::remove_all(root, ec); fs::create_directories(root / "data" / "Levels", ec);
    { std::ofstream(root / "data" / "Levels" / "FinalAlbion.gtg", std::ios::binary) << retail; }
    std::vector<std::string> notes; std::string err;
    const float pos[3] = {32.0f, 48.0f, 12.5f}, fwd[2] = {0.0f, 1.0f};
    CHECK(albion::editor::setRegionEntrance(root, 7, "MyLevel", pos, fwd, notes, err));
    CHECK(fs::exists(root / "data" / "Levels" / "FinalAlbion.gtg.forge-orig"));
    auto e = albion::editor::entranceOf(root, 7, err);
    CHECK(e && near(e->pos[0], 32.0f) && near(e->pos[2], 12.5f) && e->startScript == "MyLevelHSP");
    {
        std::ifstream in(root / "data" / "Levels" / "FinalAlbion.gtg", std::ios::binary);
        std::stringstream ss; ss << in.rdbuf();
        const std::string t = ss.str();
        const auto g = albion::editor::GtgFile::parse(t);
        CHECK(g.sections.size() == 4 && g.sections[1].slot == 3 && g.sections[2].slot == 7 && g.sections[3].slot == 62);   // slot order kept
        CHECK(g.sections[0].body == f.sections[0].body);                       // retail bytes untouched
        CHECK(t.find("UID 18446741874686296174;") != std::string::npos && t.find("UID 18446741874686296175;") != std::string::npos);   // fresh uids
        CHECK(g.sections[2].body.rfind("Version 2;\n\nXXXSectionStart NULL;\n\nNewThing Thing;", 0) == 0);
    }
    // moved: same slot, same script -> replaced in place, no duplicate
    const float pos2[3] = {10.0f, 11.0f, 1.0f};
    CHECK(albion::editor::setRegionEntrance(root, 7, "MyLevel", pos2, fwd, notes, err));
    e = albion::editor::entranceOf(root, 7, err);
    CHECK(e && near(e->pos[0], 10.0f));
    {
        std::ifstream in(root / "data" / "Levels" / "FinalAlbion.gtg", std::ios::binary);
        std::stringstream ss; ss << in.rdbuf();
        const std::string t = ss.str();
        size_t n = 0, p = 0; while ((p = t.find("REGION_ENTRANCE_POINT", p)) != std::string::npos) { ++n; p += 5; }
        CHECK(n == 2);   // retail's + ours
        n = 0; p = 0; while ((p = t.find("MyLevelHSP", p)) != std::string::npos) { ++n; p += 5; }
        CHECK(n == 1);
    }
    // the real retail file, when an install is around
    for (const char* cand : {"C:/programs/steam/steamapps/common/Fable The Lost Chapters/data/Levels/FinalAlbion.gtg"}) {
        std::ifstream in(cand, std::ios::binary);
        if (!in) continue;
        std::stringstream ss; ss << in.rdbuf();
        const std::string t = ss.str();
        const auto g = albion::editor::GtgFile::parse(t);
        CHECK(g.sections.size() > 100 && g.eol == "\r\n" && g.serialize() == t);
        CHECK(g.find(1) && g.find(1)->body.find("REGION_ENTRANCE_POINT") != std::string::npos);
    }
    // the same on a CRLF file (retail's line ending): the new blocks use CRLF too
    {
        std::string crlf;
        for (const char c : retail) { if (c == '\n') crlf += "\r\n"; else crlf += c; }
        const auto g = albion::editor::GtgFile::parse(crlf);
        CHECK(g.sections.size() == 3 && g.eol == "\r\n" && g.serialize() == crlf);
        fs::create_directories(root / "data" / "Levels", ec);
        { std::ofstream(root / "data" / "Levels" / "FinalAlbion.gtg", std::ios::binary) << crlf; }
        fs::remove(root / "data" / "Levels" / "FinalAlbion.gtg.forge-orig", ec);
        CHECK(albion::editor::setRegionEntrance(root, 2, "Crlf", pos, fwd, notes, err));
        std::ifstream in(root / "data" / "Levels" / "FinalAlbion.gtg", std::ios::binary);
        std::stringstream ss; ss << in.rdbuf();
        const std::string t = ss.str();
        CHECK(t.find('\n') != std::string::npos && t.find("\n") == t.find("\r\n") + 1);   // still CRLF throughout
        size_t lone = 0; for (size_t k = 0; k < t.size(); ++k) if (t[k] == '\n' && (k == 0 || t[k - 1] != '\r')) ++lone;
        CHECK(lone == 0);
        const auto h = albion::editor::entranceOf(root, 2, err);
        CHECK(h && h->startScript == "CrlfHSP");
    }
    fs::remove_all(root, ec);
}

// STB compaction: a hand-built bank, one replace (which appends a payload and a
// cloned table, leaving the old ones dead), then compact -> same entries, same
// payload bytes, smaller file, and compact(compact(x)) == compact(x).
static void testStbCompaction(const fs::path& dir) {
    fs::create_directories(dir);
    auto put32 = [](std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); };
    auto align = [](std::vector<uint8_t>& b, size_t a) { b.resize((b.size() + a - 1) / a * a, 0); };
    const uint32_t alignment = 64;
    std::vector<uint8_t> chunkA(150, 0xA1), chunkB(70, 0xB2);
    std::vector<uint8_t> common;
    put32(common, 1);
    const std::string level = "Data/Levels/FinalAlbion/T.lev";
    common.insert(common.end(), level.begin(), level.end()); common.push_back(0);
    put32(common, uint32_t(common.size() + 4));   // the record follows its own directory row
    common.resize(common.size() + 0x80, 0x11);

    std::vector<uint8_t> bank;
    put32(bank, 0x42424242u); put32(bank, 0); put32(bank, 0); put32(bank, 1);
    put32(bank, alignment); put32(bank, 4); put32(bank, 3); put32(bank, 0);   // table offset patched below
    struct P { const char* name; uint32_t id; const std::vector<uint8_t>* data; uint32_t offset = 0; };
    std::vector<P> payloads{{"Data/Levels/FinalAlbion/T.lev", 1, &chunkA}, {"__STATIC_MAP_COMMON_HEADER__", 2, &common}, {"Data/Levels/FinalAlbion/U.lev", 3, &chunkB}};
    for (auto& p : payloads) { align(bank, alignment); p.offset = uint32_t(bank.size()); bank.insert(bank.end(), p.data->begin(), p.data->end()); }
    align(bank, alignment);
    const uint32_t table = uint32_t(bank.size());
    put32(bank, 1); put32(bank, 0); put32(bank, 3);   // dev header
    for (const auto& p : payloads) {
        const std::string n = p.name;
        put32(bank, 42); put32(bank, p.id); put32(bank, 0); put32(bank, uint32_t(p.data->size())); put32(bank, p.offset); put32(bank, 0);
        put32(bank, uint32_t(n.size())); bank.insert(bank.end(), n.begin(), n.end());
        put32(bank, 0); put32(bank, 1); put32(bank, uint32_t(n.size())); bank.insert(bank.end(), n.begin(), n.end());
        put32(bank, 0);
    }
    for (int i = 0; i < 4; ++i) bank[28 + i] = uint8_t(table >> (8 * i));
    const fs::path src = dir / "bank.stb", grown = dir / "grown.stb", compact1 = dir / "c1.stb", compact2 = dir / "c2.stb";
    std::ofstream(src, std::ios::binary).write(reinterpret_cast<const char*>(bank.data()), std::streamsize(bank.size()));

    // a replace appends: the old payload and table become dead bytes
    std::vector<uint8_t> chunkA2(200, 0xC3);   // bigger than the slot: the replace appends
    forge::stb::replaceEntryPayload(src, grown, "Data/Levels/FinalAlbion/T.lev", chunkA2);
    const auto before = forge::stb::compactMeasure(grown);
    CHECK(before.entries == 3);
    CHECK(before.bytesBefore > bank.size() && before.deadBytes() > 0);
    const auto rep = forge::stb::compactBank(grown, compact1);
    CHECK(rep.bytesAfter == before.bytesAfter && rep.bytesAfter < rep.bytesBefore);
    const auto a = forge::stb::Archive::open(grown), b = forge::stb::Archive::open(compact1);
    CHECK(a.entries().size() == 3 && b.entries().size() == 3);
    for (size_t i = 0; i < 3; ++i) {
        CHECK(a.entries()[i].id == b.entries()[i].id && a.entries()[i].name == b.entries()[i].name);
        CHECK(a.read(a.entries()[i]) == b.read(b.entries()[i]));
    }
    CHECK(b.read(*b.findEntry("Data/Levels/FinalAlbion/T.lev")) == chunkA2);
    CHECK(b.staticMaps().size() == 1 && b.staticMaps()[0].levelName == level);
    CHECK(forge::stb::compactMeasure(compact1).deadBytes() == 0);
    forge::stb::compactBank(compact1, compact2);
    std::ifstream f1(compact1, std::ios::binary), f2(compact2, std::ios::binary);
    const std::vector<uint8_t> b1((std::istreambuf_iterator<char>(f1)), {}), b2((std::istreambuf_iterator<char>(f2)), {});
    CHECK(b1 == b2);
}

// composeStatic writes what meshpreview reads back: positions exact, winding preserved, normals
// within the 11/11/10 quantisation, UVs at the +8 bias the decoder leaves in, the Info blob's layout
void testMeshCompose() {
    using namespace forge::meshcompose;
    Primitive p;
    p.verts = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0.5f, 0.5f, 2}};
    p.faces = {{0, 1, 2}, {0, 2, 3}, {0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}};
    p.uvs = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0.5f, 0.5f}};
    Material m; m.diffuseId = 1234;
    for (bool compress : {false, true}) {
        const auto c = composeStatic("MESH_TEST_PYRAMID", {p}, {m}, compress, 0);
        CHECK(c.vertices == 5 && c.triangles == 6);
        CHECK(c.info.size() == 60 + 4);
        uint32_t lodCount = 0, lodSize = 0, texCount = 0; int32_t tex = 0;
        std::memcpy(&lodCount, c.info.data() + 44, 4); std::memcpy(&lodSize, c.info.data() + 48, 4); std::memcpy(&texCount, c.info.data() + 56, 4); std::memcpy(&tex, c.info.data() + 60, 4);
        CHECK(lodCount == 1 && lodSize < c.payload.size() && texCount == 1 && tex == 1234);
        const auto g = forge::meshpreview::decodeLod0(c.payload, 1);
        CHECK(g.vertices.size() == 5 && g.triangles.size() == 6);
        CHECK(g.materials.size() >= 1 && g.materials.front().diffuseTexture == 1234);
        for (size_t i = 0; i < 5; ++i) {
            CHECK(std::fabs(g.vertices[i].x - p.verts[i].x) < 1e-6f && std::fabs(g.vertices[i].y - p.verts[i].y) < 1e-6f && std::fabs(g.vertices[i].z - p.verts[i].z) < 1e-6f);
            CHECK(std::fabs(g.vertices[i].u - (p.uvs[i].u + 8.0f)) < 1e-3f);            // the decoder keeps the +8 bias
            CHECK(std::fabs(g.vertices[i].v - ((1.0f - p.uvs[i].v) + 8.0f)) < 1e-3f);   // stored as 1-v
        }
        for (size_t i = 0; i < 6; ++i) CHECK(g.triangles[i].a == p.faces[i][0] && g.triangles[i].b == p.faces[i][1] && g.triangles[i].c == p.faces[i][2]);
        // the apex normal points up
        CHECK(g.vertices[4].nz > 0.9f);
    }
    CHECK(packNormal({0, 0, 1}) == (uint32_t(511) << 22));
    CHECK(compressUv(0.0f) == 16384 && compressUv(1.0f) == 18432);
    bool threw = false;
    try { composeStatic("NOTMESH", {p}, {m}); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

void testNavigationDetail() {
    using namespace albion::navlines;
    HullMesh mesh;
    // No vertex lies at an AABB corner: a farthest-vertex sphere would be too small.
    mesh.verts = {{-2, 0, 0}, {2, 0, 0}, {0, -3, 0}, {0, 3, 0}, {0, 0, -6}, {0, 0, 6}};
    const auto hull = buildHull(mesh);
    CHECK(hull.centre[0] == 0 && hull.centre[1] == 0 && hull.centre[2] == 0);
    CHECK(std::fabs(hull.radius - 7.0f) < 1e-6f);
    CHECK(buildHull({}).radius == 0);
    // A parent's corner requests refinement even if this particular child is outside.
    const std::vector<Box> areas{{16.1f, 84.58f, 25.29f, 93.77f}};
    CHECK(requestsHigherDetail({25, 86, 26, 87}, areas));
    CHECK(!requestsHigherDetail({25.5f, 86.5f, 26, 87}, areas));
    CHECK(!requestsHigherDetail({26, 86, 27, 87}, areas));
    // This is corner containment, not generic overlap, with half-open boundaries.
    CHECK(!requestsHigherDetail({0, 0, 4, 4}, {{1, 1, 3, 3}}));
    CHECK(requestsHigherDetail({0, 0, 1, 1}, {{1, 1, 2, 2}}));
    CHECK(!requestsHigherDetail({2, 2, 3, 3}, {{1, 1, 2, 2}}));
    CHECK(!requestsHigherDetail({0, 0, 1, 1}, {}));
    bool rejected = false;
    try { decodeHull({}, {}); } catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected);
    forge::meshcompose::Primitive p;
    p.verts = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    p.faces = {{0, 1, 2}};
    auto bytes = forge::meshcompose::composePhysicsUncompressed({p});
    CHECK(decodeHull(bytes, {}).tris.size() == 1);
    // Count must fit this chunk, even when the surrounding allocation has more bytes.
    const std::string tris = "TRIS";
    const auto at = std::search(bytes.begin(), bytes.end(), tris.begin(), tris.end());
    CHECK(at != bytes.end());
    if (at != bytes.end()) {
        const size_t offset = size_t(at - bytes.begin()) + 8;
        bytes[offset] = bytes[offset + 1] = bytes[offset + 2] = bytes[offset + 3] = 0xff;
        rejected = false;
        try { decodeHull(bytes, {}); } catch (const std::runtime_error&) { rejected = true; }
        CHECK(rejected);
    }
}

void testGroundNavigation(const fs::path& dir) {
    using namespace forge::navmesh;
    const auto path = writeSyntheticLev(dir / "ground_nav.lev", 32, 32, [](int, int) { return 0.0f; });
    auto lev = forge::lev::File::open(path);
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
        lev.setWalkableAt(x, y, true); lev.setPreferredPathAt(x, y, false);
    }
    RetailSection source;
    source.name = "NULL"; source.width = source.height = 32;
    put<float>(source.positions, 4); put<float>(source.positions, 16); put<int32_t>(source.positions, 0);
    GroundGeometry seedGeometry; seedGeometry.regionSeeds = {{4, 16}};
    const auto empty = generateGround(lev, source, seedGeometry);
    CHECK(empty.section.nodes.size() == 1 && empty.section.nodes[0].leaf);
    CHECK(empty.anchorsUsed == 1 && empty.section.positions == source.positions);
    auto hasPoint = [](const GroundResult& result, float x, float y) {
        for (const auto& n : result.section.nodes) if (n.leaf) {
            const float half = 16.0f / float(1 << n.level);
            if (x >= n.cx - half && x < n.cx + half && y >= n.cy - half && y < n.cy + half) return true;
        }
        return false;
    };
    // Native endpoint containment is half-open. A degenerate point at a cell
    // corner blocks only its containing cell, not all four touching cells.
    GroundGeometry point = seedGeometry; point.blockingLines = {{16, 16, 16, 16}};
    const auto pointTree = generateGround(lev, source, point);
    CHECK(hasPoint(pointTree, 15.5f, 15.5f) && hasPoint(pointTree, 16.5f, 15.5f));
    CHECK(hasPoint(pointTree, 15.5f, 16.5f) && !hasPoint(pointTree, 16.5f, 16.5f));
    // Collinear overlap alone does not count; the top edge is exclusive and
    // this short segment never intersects either vertical side.
    point.blockingLines = {{15.25f, 16, 15.75f, 16}};
    const auto edgeTree = generateGround(lev, source, point);
    CHECK(hasPoint(edgeTree, 15.5f, 15.5f) && !hasPoint(edgeTree, 15.5f, 16.5f));
    // Native edge intersections allow 0.0001 world units of endpoint slack.
    point.regionSeeds = {{4, 4}};
    point.blockingLines = {{-1, 16.00005f, 33, 16.00005f}};
    const auto nearEdge = generateGround(lev, source, point);
    CHECK(hasPoint(nearEdge, 4.5f, 14.5f) && !hasPoint(nearEdge, 4.5f, 15.5f));
    GroundGeometry g = seedGeometry;
    g.blockingLines = {{16.25f, 0, 16.25f, 32}};
    const auto divided = generateGround(lev, source, g);
    CHECK(divided.leavesRemoved > 0);
    for (const auto& n : divided.section.nodes) if (n.leaf) CHECK(n.cx < 16.25f);
    // Saved action points do not make islands reachable. External region seeds
    // work even when no action points were serialized (GuildExterior case).
    auto actionOnRight = source;
    const float rightX = 25; std::memcpy(actionOnRight.positions.data(), &rightX, 4);
    const auto ignoredAction = generateGround(lev, actionOnRight, g);
    CHECK(!hasPoint(ignoredAction, 25, 16) && hasPoint(ignoredAction, 4, 16));
    CHECK(ignoredAction.section.positions == actionOnRight.positions);
    auto noActions = source; noActions.positions.clear();
    const auto externalSeed = generateGround(lev, noActions, g);
    CHECK(hasPoint(externalSeed, 4, 16) && externalSeed.section.positions.empty());
    g.detailedAreas = {{15, 0, 18, 32}};
    // Put the line in the right half: the left half remains connected to the seed.
    g.blockingLines = {{16.75f, 0, 16.75f, 32}};
    const auto detailed = generateGround(lev, source, g);
    size_t halfLeaves = 0;
    for (const auto& n : detailed.section.nodes) if (n.leaf && n.level == 6) ++halfLeaves;
    CHECK(halfLeaves > 0);
    g.blockingLines = {{16.25f, 0, 16.25f, 14.25f}, {16.25f, 18.75f, 16.25f, 32}};
    g.switchableLines = {{123456789, {{16.25f, 14.25f, 16.25f, 18.75f}}}};
    const auto door = generateGround(lev, source, g);
    size_t switches = 0, rightLeaves = 0;
    for (const auto& n : door.section.nodes) if (n.leaf) {
        rightLeaves += n.cx > 17;
        if (n.switchable) { ++switches; CHECK(n.uids == std::vector<uint64_t>{123456789}); }
        for (const auto nb : n.neighbours) {
            const auto it = std::find_if(door.section.nodes.begin(), door.section.nodes.end(),
                [&](const RetailNode& b) { return b.index == nb; });
            CHECK(it != door.section.nodes.end());
            if (it != door.section.nodes.end()) CHECK(std::find(it->neighbours.begin(), it->neighbours.end(), n.index) != it->neighbours.end());
        }
    }
    CHECK(switches > 0 && rightLeaves > 0 && door.section.regionCount == 3);
    const auto bytes = emitNavigation(lev, RetailNav{{door.section}});
    CHECK(std::equal(bytes.begin(), bytes.begin() + lev.navigationOffset(), lev.originalBytes().begin()));
    const auto out = dir / "ground_nav_roundtrip.lev";
    { std::ofstream f(out, std::ios::binary); f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())); }
    const auto reread = forge::lev::File::open(out);
    CHECK(emitNavigation(reread, parseNavigation(reread)) == bytes);
    const auto removed = generateGround(lev, source, {});
    CHECK(removed.leavesRemoved == 1 && removed.section.nodes[0].marker);
    CHECK(removed.section.positions == source.positions);
    // Native pruning retains unanchored switchable leaves as closed doors.
    g.regionSeeds.clear();
    const auto closed = generateGround(lev, source, g);
    size_t closedDoors = 0;
    for (const auto& n : closed.section.nodes) if (n.leaf) {
        ++closedDoors;
        CHECK(n.switchable && n.blocked && n.region == 0);
        for (const auto index : n.neighbours) {
            const auto it = std::find_if(closed.section.nodes.begin(), closed.section.nodes.end(),
                [&](const RetailNode& b) { return b.index == index; });
            CHECK(it != closed.section.nodes.end() && it->switchable && it->blocked);
        }
    }
    CHECK(closedDoors > 0 && closed.section.regionCount == 1);
    // A seed on the right assigns the door to that side, not the first leaf
    // encountered in geometric order. The other side becomes a second region.
    g.regionSeeds = {{25, 16}};
    const auto fromRight = generateGround(lev, source, g);
    for (const auto& n : fromRight.section.nodes) if (n.leaf) {
        if (n.switchable || n.cx > 17) CHECK(n.region == 1);
        else if (n.cx < 15) CHECK(n.region == 2);
    }
    auto layered = source; layered.layerCount = 2;
    bool rejected = false;
    try { generateGround(lev, layered, {}); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    g.switchableLines.push_back(g.switchableLines.front());
    rejected = false;
    try { generateGround(lev, source, g); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "FableForgeTests";
    fs::create_directories(dir);
    const fs::path lev = writeSyntheticLev(dir / "synthetic.lev", 4, 3,
                                           [](int x, int y) { return 10.0f * x + 3.0f * y; });
    testMeshGeometry(lev);
    testGlbStructure(lev, dir);
    testObjStructure(lev, dir);
    testBcDecoders();
    testPng();
    testFoliageGlb(lev, dir);
    testThingBasis();
    testNavigationDetail();
    testGroundNavigation(dir);
    testWater(dir);
    testLevelDocument();
    testTerrainEditing(lev, dir);
    testThemeToolsAndPaths(dir);
    testExternalWorld(dir);
    testThingLinks();
    testQuestSections();
    testGameMapGrid(dir);
    testEnvironmentAndSoundPaint(dir);
    testFractal();
    testAddSoundTheme(dir);
    testThingProperties();
    testNavPatch(dir);
    testGtg(dir);
    testMeshCompose();
    testStbCompaction(dir / "stb_compact");
    if (g_failures) { std::cerr << g_failures << " failure(s)\n"; return 1; }
    std::cout << "fableforge_tests: all passed\n";
    return 0;
}
