#include "forge/stbbake.hpp"
#include "forge/lzo.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #x); } while (false)
template<class F> void refuses(F f) {
    bool threw = false;
    try { f(); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}
std::vector<uint8_t> fixture(bool emptyCell) {
    using namespace forge::stbbake;
    std::vector<uint8_t> body(1024);
    for (size_t i = 0; i < body.size(); ++i) body[i] = uint8_t(i * 73 + i / 7);
    const auto frame = forge::lzo::compressFramed(body);
    std::vector<QuadEntry> entries(56);
    for (size_t i = 0; i < entries.size(); ++i) {
        auto& e = entries[i];
        e.frameOffset = 4096;
        e.frameSpan = uint32_t(frame.size());
        e.aabb[0] = float(i % 4) * 16;
        e.aabb[1] = float(i / 4) * 16;
        e.aabb[2] = -4;
        e.aabb[3] = e.aabb[0] + 16;
        e.aabb[4] = e.aabb[1] + 16;
        e.aabb[5] = 8;
        e.flags = 1;
    }
    if (emptyCell) entries[8] = {};
    const auto directory = generateQuadDir(entries);
    std::vector<uint8_t> bytes(4096 + frame.size() + 80, 0);
    // Reproduce the 64x224 native boundary: 56*36 bytes, only 32 padding
    // bytes, then a frame. There is no complete 36-byte zero terminator.
    std::copy_n(directory.begin(), 56 * 36, bytes.begin() + 0x800);
    std::copy(frame.begin(), frame.end(), bytes.begin() + 4096);
    return bytes;
}
}

int main() {
    try {
        using namespace forge::stbbake;
        const QuadDirectoryLayout layout{0x800, 56};
        for (bool empty : {false, true}) {
            const auto original = fixture(empty);
            const auto chunk = parseChunk(original);
            CHECK(chunk.frameIndices.size() == 1);
            const auto fixed = parseQuadDir(chunk, layout);
            CHECK(fixed.size() == 56 && fixed.back().dirOffset == 0x800 + 55 * 36);
            CHECK(parseQuadDir(chunk).size() == (empty ? 8 : 56));
            if (empty) CHECK(fixed[8].frameOffset == 0 && fixed[9].frameOffset == 4096);
            refuses([&] { parseQuadDir(chunk, QuadDirectoryLayout{0x800, 57}); });
            refuses([&] { parseQuadDir(chunk, QuadDirectoryLayout{4096, 1}); });
            refuses([&] { parseQuadDir(chunk, QuadDirectoryLayout{std::numeric_limits<size_t>::max(), 1}); });
            refuses([&] { parseQuadDir(chunk, QuadDirectoryLayout{0x800, std::numeric_limits<size_t>::max()}); });
            auto changed = original;
            CHECK(translateQuadDirXY(changed, 11, -7, layout) == 56);
            std::vector<bool> allowed(changed.size(), false);
            for (const auto& entry : fixed) {
                for (size_t axis : {size_t(0), size_t(1), size_t(3), size_t(4)}) {
                    const size_t at = entry.dirOffset + 8 + axis * 4;
                    float value;
                    std::memcpy(&value, changed.data() + at, 4);
                    CHECK(value == entry.aabb[axis] + (axis == 0 || axis == 3 ? 11 : -7));
                    std::fill_n(allowed.begin() + at, 4, true);
                }
            }
            for (size_t i = 0; i < changed.size(); ++i)
                if (!allowed[i]) CHECK(changed[i] == original[i]);
            changed = original;
            std::vector<std::pair<float,float>> ranges(56, {-12, 19});
            CHECK(updateQuadDirZBounds(changed, ranges, layout) == 56);
            std::fill(allowed.begin(), allowed.end(), false);
            for (const auto& entry : fixed) for (size_t axis : {size_t(2), size_t(5)}) {
                const size_t at = entry.dirOffset + 8 + axis * 4;
                float value; std::memcpy(&value, changed.data() + at, 4);
                CHECK(value == (axis == 2 ? -12 : 19));
                std::fill_n(allowed.begin() + at, 4, true);
            }
            for (size_t i = 0; i < changed.size(); ++i)
                if (!allowed[i]) CHECK(changed[i] == original[i]);
            changed = original;
            refuses([&] { updateQuadDirZBounds(changed, {{0, 1}}, layout); });
            CHECK(changed == original);
            refuses([&] { translateQuadDirXY(changed, 1, 2, QuadDirectoryLayout{0x800,57}); });
            CHECK(changed == original);
            EmitOptions options; options.foregroundDirectory = layout;
            const auto emitted = emitChunk(chunk, options);
            CHECK(emitted.ok && emitted.identity && emitted.chunk == original);
        }
        Chunk shortHeader; shortHeader.raw.resize(71);
        CHECK(parseQuadDir(shortHeader, QuadDirectoryLayout{0,1}).size() == 1);
        refuses([&] { parseQuadDir(shortHeader, QuadDirectoryLayout{0,2}); });
        CHECK(parseQuadDir(shortHeader, QuadDirectoryLayout{71,0}).empty());
        std::cout << checks << " foreground directory checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
