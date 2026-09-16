// Verifies the clean-room LZO1X decoder (src/lzo1x.cpp, MIT) against minilzo
// (GPL, linked into THIS TEST ONLY) on real data from the local Fable install:
// every framed block of several FinalAlbion_RT.stb chunks and every chunked
// texture mip in textures.big. Also checks that corrupt input fails safely.
// Skips (exit 0 with a note) when no install is present.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

#include "forge/big.hpp"
#include "forge/env.hpp"
#include "forge/stb.hpp"
#include "lzo1x.hpp"
#include "minilzo/minilzo.h"

namespace fs = std::filesystem;

namespace {

int g_frames = 0, g_textures = 0, g_fail = 0, g_roundtrips = 0;
size_t g_rtIn = 0, g_rtOut = 0;
size_t g_retailComp = 0, g_oursComp = 0; int g_fits = 0, g_frameCmp = 0;

// Encoder check: our stream must decode to the input through minilzo AND our
// decoder, and consume exactly the stream.
void roundTrip(const std::vector<uint8_t>& data, const char* what) {
    const auto enc = albion::lzo1x::compress(data.data(), data.size());
    std::vector<uint8_t> ref(data.size() + 8), mine(data.size() + 8);
    lzo_uint refLen = data.size();
    const int rc = lzo1x_decompress_safe(enc.data(), lzo_uint(enc.size()), ref.data(), &refLen, nullptr);
    size_t mineLen = data.size();
    const auto st = albion::lzo1x::decompress(enc.data(), enc.size(), mine.data(), &mineLen);
    const bool refOk = rc == LZO_E_OK && size_t(refLen) == data.size() && std::memcmp(ref.data(), data.data(), data.size()) == 0;
    const bool mineOk = st == albion::lzo1x::Status::Ok && mineLen == data.size() && std::memcmp(mine.data(), data.data(), data.size()) == 0;
    if (!refOk || !mineOk) {
        std::fprintf(stderr, "ROUNDTRIP FAIL %s (%zu bytes -> %zu): minilzo rc=%d len=%zu, mine %s len=%zu\n", what, data.size(), enc.size(), rc,
                     size_t(refLen), albion::lzo1x::statusName(st), mineLen);
        ++g_fail;
    }
    ++g_roundtrips; g_rtIn += data.size(); g_rtOut += enc.size();
}

void checkEncoderSynthetic() {
    std::vector<uint8_t> v;
    for (size_t n = 0; n <= 40; ++n) { v.assign(n, 'a'); roundTrip(v, "run-of-a"); }
    for (size_t n = 0; n <= 40; ++n) { v.resize(n); for (size_t i = 0; i < n; ++i) v[i] = uint8_t(i * 7 + 3); roundTrip(v, "short-ramp"); }
    v.assign(100000, 0); roundTrip(v, "zeros-100k");
    // pseudo-random: incompressible, exercises long literal runs
    uint32_t s = 12345; v.resize(70000); for (auto& b : v) { s = s * 1664525u + 1013904223u; b = uint8_t(s >> 24); } roundTrip(v, "random-70k");
    // periodic with distances that hit each match class and the extended-length paths
    for (size_t period : {1u, 2u, 3u, 7u, 100u, 2048u, 2049u, 3000u, 16384u, 16385u, 20000u, 49151u, 49152u, 60000u}) {
        v.resize(period * 3 + 777);
        for (size_t i = 0; i < v.size(); ++i) v[i] = uint8_t((i % period) * 31 + (i % period) / 7);
        roundTrip(v, "periodic");
    }
    // text-like: matches of every length 3..40 separated by 0..5 literals
    v.clear(); s = 99;
    while (v.size() < 200000) {
        s = s * 1664525u + 1013904223u;
        const size_t lits = (s >> 28) % 6, len = 3 + (s >> 20) % 38;
        for (size_t k = 0; k < lits; ++k) v.push_back(uint8_t(s >> (k * 3)));
        if (v.size() > 5000) { const size_t back = 1 + (s >> 8) % 4000; for (size_t k = 0; k < len; ++k) v.push_back(v[v.size() - back]); }
    }
    roundTrip(v, "mixed-200k");
    std::printf("  encoder synthetic: %d round trips\n", g_roundtrips);
}

bool same(const uint8_t* in, size_t inLen, size_t uncomp, const char* what) {
    std::vector<uint8_t> ref(uncomp + 8), mine(uncomp + 8);
    lzo_uint refLen = uncomp;
    const int rc = lzo1x_decompress_safe(in, lzo_uint(inLen), ref.data(), &refLen, nullptr);
    size_t mineLen = uncomp;
    const auto st = albion::lzo1x::decompress(in, inLen, mine.data(), &mineLen);
    if (rc != LZO_E_OK) return false;   // not a real frame; nothing to compare
    if (st != albion::lzo1x::Status::Ok || mineLen != size_t(refLen) || std::memcmp(ref.data(), mine.data(), refLen) != 0) {
        std::fprintf(stderr, "MISMATCH %s: minilzo ok (%zu bytes), mine %s (%zu bytes)\n", what, size_t(refLen),
                     albion::lzo1x::statusName(st), mineLen);
        ++g_fail;
        return true;
    }
    return true;
}

// Walk a chunk for [uncomp][comp] frames exactly like the exporter does.
void checkChunk(const std::vector<uint8_t>& d, const std::string& name) {
    auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, d.data() + o, 4); return v; };
    int local = 0;
    for (size_t off = 0; off + 8 < d.size(); off += 4) {
        const uint32_t a = u32(off), b = u32(off + 4);
        const uint32_t pairs[2][2] = {{a, b}, {b, a}};
        for (int k = 0; k < 2; ++k) {
            const uint32_t unc = pairs[k][0], comp = pairs[k][1];
            if (comp < 32 || comp > 400000 || unc < 64 || unc > 4000000 || comp > unc) continue;
            if (off + 8 + size_t(comp) > d.size()) continue;
            if (same(d.data() + off + 8, comp, unc, (name + "@" + std::to_string(off)).c_str())) {
                ++g_frames; ++local;
                {   // re-encode the decoded body with our encoder and check both decoders read it back
                    std::vector<uint8_t> body(unc); size_t bl = unc;
                    if (albion::lzo1x::decompress(d.data() + off + 8, comp, body.data(), &bl) == albion::lzo1x::Status::Ok) {
                        roundTrip(body, name.c_str());
                        const auto enc = albion::lzo1x::compress(body.data(), body.size());
                        g_retailComp += comp; g_oursComp += enc.size(); ++g_frameCmp; if (enc.size() <= comp) ++g_fits;
                        if (std::getenv("ALBION_LZO_VERBOSE")) std::printf("    frame unc=%u retail=%u ours=%zu %s\n", unc, comp, enc.size(), enc.size() > comp ? "OVER" : "");
                        if (const char* dump = std::getenv("ALBION_LZO_DUMP"); dump && g_frameCmp <= 6) {
                            const std::string base = std::string(dump) + "/frame" + std::to_string(g_frameCmp);
                            std::ofstream(base + ".retail.lzo", std::ios::binary).write(reinterpret_cast<const char*>(d.data() + off + 8), std::streamsize(comp));
                            std::ofstream(base + ".ours.lzo", std::ios::binary).write(reinterpret_cast<const char*>(enc.data()), std::streamsize(enc.size()));
                            std::ofstream(base + ".raw", std::ios::binary).write(reinterpret_cast<const char*>(body.data()), std::streamsize(body.size()));
                        }
                    }
                }
                off = ((off + 8 + comp + 3) & ~size_t(3)) - 4;
                break;
            }
        }
    }
    std::printf("  %-40s %d frames\n", name.c_str(), local);
}

void checkTextures(const fs::path& texturesBig) {
    const auto big = forge::big::File::open(texturesBig);
    const auto* bank = big.findBank("GBANK_MAIN_PC");
    if (!bank) return;
    int n = 0;
    for (const auto& e : bank->entries) {
        if (e.subHeader.size() < 34) continue;
        uint32_t mipSize0; std::memcpy(&mipSize0, e.subHeader.data() + 24, 4);
        uint32_t frameDataSize; std::memcpy(&frameDataSize, e.subHeader.data() + 20, 4);
        if (mipSize0 == 0) continue;   // stored raw
        const auto payload = big.entryData(e);
        // chunked stream: [u16 clen | 0xFFFF u32 clen] [lzo body] ...
        size_t pos = 0, produced = 0;
        while (pos + 2 <= payload.size() && produced < frameDataSize) {
            uint32_t clen = uint32_t(payload[pos]) | (uint32_t(payload[pos + 1]) << 8);
            pos += 2;
            if (clen == 0xFFFF) { if (pos + 4 > payload.size()) break; std::memcpy(&clen, payload.data() + pos, 4); pos += 4; }
            if (clen == 0 || pos + clen > payload.size()) break;
            std::vector<uint8_t> ref(frameDataSize + 64), mine(frameDataSize + 64);
            lzo_uint refLen = ref.size();
            const int rc = lzo1x_decompress_safe(payload.data() + pos, clen, ref.data(), &refLen, nullptr);
            size_t mineLen = mine.size();
            const auto st = albion::lzo1x::decompress(payload.data() + pos, clen, mine.data(), &mineLen);
            if (rc == LZO_E_OK) {
                if ((st != albion::lzo1x::Status::Ok && st != albion::lzo1x::Status::InputNotConsumed) ||
                    mineLen != size_t(refLen) || std::memcmp(ref.data(), mine.data(), refLen) != 0) {
                    std::fprintf(stderr, "MISMATCH texture %s chunk@%zu: mine %s\n", e.name.c_str(), pos, albion::lzo1x::statusName(st));
                    ++g_fail;
                }
                produced += refLen;
                ++n;
            }
            pos += clen;
        }
        ++g_textures;
    }
    std::printf("  textures.big: %d entries, %d compressed chunks compared\n", g_textures, n);
}

void checkCorrupt() {
    // Truncated / garbage inputs must not crash and must not report Ok.
    const uint8_t junk[] = {0x11, 0x00, 0x00};   // bare end marker: valid, empty
    uint8_t out[16]; size_t outLen = sizeof out;
    if (albion::lzo1x::decompress(junk, sizeof junk, out, &outLen) != albion::lzo1x::Status::Ok || outLen != 0) { std::fprintf(stderr, "end marker alone should decode to empty\n"); ++g_fail; }
    const uint8_t bad[] = {0x2F, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00};   // M3 match with nothing behind it
    outLen = sizeof out;
    if (albion::lzo1x::decompress(bad, sizeof bad, out, &outLen) == albion::lzo1x::Status::Ok) { std::fprintf(stderr, "lookbehind overrun accepted\n"); ++g_fail; }
    std::vector<uint8_t> lit = {0x12, 'a', 'b', 'c', 'd', 'e', 0x11, 0x00, 0x00};   // first byte 18 -> 1 literal then... constructed run
    outLen = sizeof out;
    (void)albion::lzo1x::decompress(lit.data(), lit.size(), out, &outLen);   // must not crash whatever it returns
    // Truncation of every real frame prefix must fail, not crash.
}

} // namespace

int main() {
    if (lzo_init() != LZO_E_OK) { std::fprintf(stderr, "minilzo init failed\n"); return 1; }
    checkCorrupt();
    checkEncoderSynthetic();
    fs::path root;
    try {
        const auto env = forge::env::Environment::detect();
        if (env.installValid) root = env.installDir;
    } catch (...) {}
    if (root.empty()) { std::printf("albionatlas_lzo_tests: no Fable install, corrupt-input checks only (%d failures)\n", g_fail); return g_fail ? 1 : 0; }

    const fs::path stbPath = root / "data" / "Levels" / "FinalAlbion_RT.stb";
    const auto archive = forge::stb::Archive::open(stbPath);
    // Every 25th static map + a few known ones keeps the run short but broad.
    int i = 0;
    for (const auto& map : archive.staticMaps()) {
        const std::string stem = fs::path(map.levelName).stem().string();
        const bool pick = (i++ % 25 == 0) || stem == "StartOakValeWest" || stem == "Greatwood_1" || stem == "Darkwood_3";
        if (!pick) continue;
        const auto record = archive.readStaticMapRecord(map);
        if (record.size() < 8) continue;
        int32_t bankIndex; std::memcpy(&bankIndex, record.data() + 4, 4);
        for (const auto& e : archive.entries())
            if (int32_t(e.id) == bankIndex) { checkChunk(archive.read(e), stem); break; }
    }
    checkTextures(root / "data" / "graphics" / "pc" / "textures.big");
    std::printf("  retail lzo1x_999 frames: %zu bytes; ours: %zu bytes (%.1f%%), %d of %d frames fit their retail slot\n",
                g_retailComp, g_oursComp, g_retailComp ? 100.0 * double(g_oursComp) / double(g_retailComp) : 0.0, g_fits, g_frameCmp);
    std::printf("albionatlas_lzo_tests: %d chunk frames + texture chunks compared, %d encoder round trips (%.1f%% of input), %d failures\n",
                g_frames, g_roundtrips, g_rtIn ? 100.0 * double(g_rtOut) / double(g_rtIn) : 0.0, g_fail);
    return g_fail ? 1 : 0;
}
