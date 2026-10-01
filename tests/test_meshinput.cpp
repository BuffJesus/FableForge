#include "meshimport.hpp"
#include "nlohmann/json.hpp"

#include <bit>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

static void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
static void put32(Bytes& bytes, uint32_t value) {
    for (unsigned shift = 0; shift != 32; shift += 8) bytes.push_back(uint8_t(value >> shift));
}
static void set32(Bytes& bytes, size_t offset, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i) bytes.at(offset + i) = uint8_t(value >> (i * 8));
}
static void chunk(Bytes& bytes, uint32_t type, const Bytes& content) {
    put32(bytes, uint32_t(content.size())); put32(bytes, type);
    bytes.insert(bytes.end(), content.begin(), content.end());
    set32(bytes, 8, uint32_t(bytes.size()));
}

int main() {
    const auto dir = fs::temp_directory_path() / ("FableForgeMeshInput-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directory(dir);
        const auto path = dir / "triangle.glb";
        auto load = [&](const Bytes& bytes) {
            { std::ofstream f(path, std::ios::binary); f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())); }
            return albion::meshimport::loadModel(path);
        };
        size_t checks = 0;
        auto rejects = [&](const Bytes& bytes, const std::string& label) {
            bool rejected = false;
            try { load(bytes); } catch (const std::exception&) { rejected = true; }
            require(rejected, "accepted " + label);
            ++checks;
        };
        auto triangle = [&](const Bytes& bytes, const std::string& label) {
            const auto model = load(bytes);
            require(model.prims.size() == 1 && model.prims[0].verts.size() == 3 && model.prims[0].faces.size() == 1,
                    "lost triangle in " + label);
            require(model.prims[0].verts[1].x == 100 && model.prims[0].verts[2].z == 100,
                    "changed vertex conversion in " + label);
            ++checks;
        };

        std::string json = R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":36}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]})";
        while (json.size() % 4) json += ' ';
        Bytes good;
        put32(good, 0x46546c67); put32(good, 2); put32(good, 0);
        chunk(good, 0x4e4f534a, Bytes(json.begin(), json.end()));
        const size_t binaryHeader = good.size();
        Bytes positions;
        for (float f : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) put32(positions, std::bit_cast<uint32_t>(f));
        chunk(good, 0x004e4942, positions);
        triangle(good, "ordinary GLB");

        const auto document = nlohmann::json::parse(json);
        auto withDocument = [&](const nlohmann::json& doc, const Bytes& binary) {
            std::string encoded = doc.dump();
            while (encoded.size() % 4) encoded += ' ';
            Bytes result; put32(result, 0x46546c67); put32(result, 2); put32(result, 0);
            chunk(result, 0x4e4f534a, Bytes(encoded.begin(), encoded.end()));
            chunk(result, 0x004e4942, binary);
            return result;
        };
        auto changed = document;
        changed["bufferViews"][0]["byteLength"] = 12;
        rejects(withDocument(changed, positions), "accessor outside its declared buffer view");
        auto invalidField = [&](const char* collection, const char* key, nlohmann::json value, const char* label) {
            auto doc = document; doc[collection][0][key] = std::move(value);
            rejects(withDocument(doc, positions), label);
        };
        invalidField("buffers", "byteLength", 40, "truncated logical buffer");
        invalidField("buffers", "byteLength", 32, "view outside logical buffer");
        invalidField("bufferViews", "byteOffset", uint64_t(-4), "overflowing view offset");
        invalidField("bufferViews", "byteLength", uint64_t(-1), "overflowing view length");
        invalidField("bufferViews", "byteStride", 4, "overlapping accessor elements");
        invalidField("bufferViews", "byteStride", 13, "unaligned stride");
        invalidField("bufferViews", "byteStride", 0, "explicit zero stride");
        invalidField("bufferViews", "byteStride", uint64_t(-4), "overflowing stride");
        invalidField("accessors", "byteOffset", uint64_t(-4), "overflowing accessor offset");
        invalidField("accessors", "byteOffset", 1, "unaligned accessor offset");
        invalidField("accessors", "count", uint64_t(-1), "overflowing accessor count");
        invalidField("accessors", "count", uint64_t(0x100000003), "count narrowed to int");
        invalidField("accessors", "count", -1, "negative count");
        invalidField("accessors", "count", 3.5, "fractional count");
        invalidField("accessors", "count", 0, "zero count");
        invalidField("accessors", "componentType", 5124, "unknown component type");
        invalidField("accessors", "sparse", {{"count", 1}}, "unsupported sparse data");
        Bytes interleaved(4, 0);
        for (size_t i = 0; i < 3; ++i) {
            interleaved.insert(interleaved.end(), positions.begin() + i * 12, positions.begin() + (i + 1) * 12);
            interleaved.insert(interleaved.end(), 4, 0);
        }
        changed = document;
        changed["buffers"][0]["byteLength"] = interleaved.size();
        changed["bufferViews"][0] = {{"buffer", 0}, {"byteOffset", 4}, {"byteLength", 48}, {"byteStride", 16}};
        triangle(withDocument(changed, interleaved), "interleaved positions with view offset");
        changed["bufferViews"][0]["byteOffset"] = 0;
        changed["bufferViews"][0]["byteLength"] = 52;
        changed["accessors"][0]["byteOffset"] = 4;
        triangle(withDocument(changed, interleaved), "interleaved positions with accessor offset");
        auto padded = positions; padded.insert(padded.end(), 4, 0);
        triangle(withDocument(document, padded), "extra binary bytes outside the logical buffer");

        auto sceneDoc = document;
        sceneDoc["scenes"] = {{{"nodes", {0}}}};
        sceneDoc["nodes"] = {{{"mesh", 0}}};
        triangle(withDocument(sceneDoc, positions), "single-node scene");
        changed = sceneDoc; changed["nodes"][0]["children"] = {0};
        rejects(withDocument(changed, positions), "self-cycle");
        changed = sceneDoc; changed["nodes"][0]["children"] = {1};
        changed["nodes"].push_back({{"children", {0}}});
        rejects(withDocument(changed, positions), "two-node cycle");
        changed = sceneDoc; changed["scenes"][0]["nodes"] = {0, 0};
        rejects(withDocument(changed, positions), "repeated scene root");
        changed = sceneDoc; changed["nodes"][0]["children"] = {1, 1};
        changed["nodes"].push_back({{"mesh", 0}});
        rejects(withDocument(changed, positions), "repeated child");
        changed = sceneDoc; changed["nodes"][0]["children"] = {-1};
        rejects(withDocument(changed, positions), "negative child index");
        changed = sceneDoc; changed["nodes"][0]["children"] = {0.5};
        rejects(withDocument(changed, positions), "fractional child index");
        changed = sceneDoc; changed["nodes"][0]["children"] = {7};
        rejects(withDocument(changed, positions), "missing child");
        for (const auto* key : {"matrix", "translation", "rotation", "scale"}) {
            changed = sceneDoc; changed["nodes"][0][key] = {1, 2};
            rejects(withDocument(changed, positions), std::string("short node ") + key);
        }
        changed = sceneDoc; changed["nodes"][0]["translation"] = {1e300, 0, 0};
        rejects(withDocument(changed, positions), "non-finite converted transform");
        changed = sceneDoc;
        changed["nodes"][0]["matrix"] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        triangle(withDocument(changed, positions), "matrix node");
        changed["nodes"][0]["translation"] = {0, 0, 0};
        rejects(withDocument(changed, positions), "mixed matrix and TRS node");
        changed = sceneDoc;
        changed["nodes"] = {{{"scale", {1e30, 1e30, 1e30}}, {"children", {1}}}, {{"scale", {1e30, 1e30, 1e30}}, {"mesh", 0}}};
        rejects(withDocument(changed, positions), "overflowing inherited transform");
        changed = sceneDoc;
        changed["nodes"] = {{{"translation", {1, 0, 0}}, {"children", {1}}}, {{"scale", {2, 2, 2}}, {"mesh", 0}}};
        auto transformed = load(withDocument(changed, positions));
        require(transformed.prims.size() == 1 && transformed.prims[0].verts[0].x == 100 &&
                transformed.prims[0].verts[1].x == 300 && transformed.prims[0].verts[2].z == 200,
                "lost inherited scene transform"); ++checks;
        changed = sceneDoc;
        changed["nodes"] = {{{"children", {1, 2}}}, {{"mesh", 0}, {"translation", {1, 0, 0}}}, {{"mesh", 0}, {"translation", {2, 0, 0}}}};
        transformed = load(withDocument(changed, positions));
        require(transformed.prims.size() == 2 && transformed.prims[0].verts[0].x == 100 &&
                transformed.prims[1].verts[0].x == 200, "lost instance order or duplicate mesh instance"); ++checks;
        changed = sceneDoc; changed["nodes"][0]["scale"] = {-1, 1, 1};
        transformed = load(withDocument(changed, positions));
        require(transformed.prims.size() == 1 && transformed.prims[0].verts[1].x == -100 &&
                transformed.prims[0].faces[0][1] == 2 && transformed.prims[0].faces[0][2] == 1,
                "lost mirrored-node winding"); ++checks;
        changed = sceneDoc; changed["nodes"] = nlohmann::json::array();
        for (size_t i = 0; i < 12000; ++i) changed["nodes"].push_back({{"children", {i + 1}}});
        changed["nodes"].push_back({{"mesh", 0}});
        triangle(withDocument(changed, positions), "12001-node hierarchy");
        changed = sceneDoc; changed["nodes"][0].erase("mesh");
        require(load(withDocument(changed, positions)).prims.empty(), "meshless scene imported unused library mesh"); ++checks;
        changed = sceneDoc; changed["scenes"][0].erase("nodes");
        require(load(withDocument(changed, positions)).prims.empty(), "empty scene imported unused library mesh"); ++checks;

        auto bad = good; set32(bad, 4, 1); rejects(bad, "GLB version 1");
        bad = good; set32(bad, 8, uint32_t(good.size() - 4)); rejects(bad, "short declared length");
        bad = good; set32(bad, 8, uint32_t(good.size() + 4)); rejects(bad, "long declared length");
        bad = good; set32(bad, 12, 0xfffffff0); rejects(bad, "overflowing JSON chunk length");
        bad = good; set32(bad, binaryHeader, uint32_t(positions.size() + 4)); rejects(bad, "truncated binary chunk");
        bad = good; set32(bad, 16, 0x004e4942); rejects(bad, "BIN before JSON");
        bad = good; chunk(bad, 0x004e4942, positions); rejects(bad, "duplicate binary chunk");
        bad = good; chunk(bad, 0x4e4f534a, Bytes(json.begin(), json.end())); rejects(bad, "duplicate JSON chunk");
        bad = good; bad.push_back(0); set32(bad, 8, uint32_t(bad.size())); rejects(bad, "incomplete trailing chunk header");
        bad = good; chunk(bad, 0x12345678, {1, 2, 3, 4}); triangle(bad, "unknown extension chunk");
        bad = good; chunk(bad, 0x12345678, {1, 2, 3}); rejects(bad, "unaligned extension chunk");
        bad = good; set32(bad, 12, uint32_t(json.size() - 1)); rejects(bad, "unaligned JSON chunk");
        for (size_t length = 0; length < good.size(); ++length) {
            bad.assign(good.begin(), good.begin() + length);
            if (length >= 12) set32(bad, 8, uint32_t(length));
            rejects(bad, "prefix of length " + std::to_string(length));
        }
        fs::remove(path); fs::remove(dir);
        std::cout << "mesh input: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << " (fixture retained in " << dir.string() << ")\n";
        return 1;
    }
}
