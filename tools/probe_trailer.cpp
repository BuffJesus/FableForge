// probe: decode a patch trailer's range blocks and print the records (format RE aid)
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include "forge/rangecodec.hpp"
#include "forge/stbbake.hpp"

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const auto pb = forge::stbbake::parsePatchBody(body);
    const auto& d = pb.trailer;
    size_t p = 0;
    auto i32 = [&]() { int32_t v; std::memcpy(&v, d.data() + p, 4); p += 4; return v; };
    auto u16 = [&]() { uint16_t v; std::memcpy(&v, d.data() + p, 2); p += 2; return v; };
    const size_t strides[4] = {0x14, 0x10, 0x3c, 0x38};
    for (int s = 0; s < 4; ++s) {
        const uint16_t a = u16(), b = u16(); const uint8_t fl = d[p++];
        std::printf("strip %d hdr %u,%u flag %u\n", s, a, b, fl);
        for (int arr = 0; arr < 4; ++arr) {
            const int32_t n = i32();
            if (n <= 0) continue;
            const int32_t len = i32();
            const auto rec = forge::rangecodec::decode(d.data() + p, size_t(len), size_t(n), strides[arr]);
            p += size_t(len);
            std::printf("  arr%d n=%d stride=%zu\n", arr, n, strides[arr]);
            for (int r = 0; r < n && r < 4; ++r) {
                std::printf("    ");
                for (size_t k = 0; k < strides[arr]; ++k) std::printf("%02x%s", rec[size_t(r) * strides[arr] + k], (k % 4 == 3) ? " " : "");
                std::printf("\n");
            }
        }
    }
    const uint8_t water = d[p++];
    std::printf("water %u\n", water);
    if (water) {
        const uint16_t vc = u16(), tc = u16(); const int32_t ty = i32(), st = i32(), len = i32();
        std::printf("  sub v=%u tri=%u type=%d stride=%d len=%d\n", vc, tc, ty, st, len);
        const auto rec = forge::rangecodec::decode(d.data() + p, size_t(len), vc, size_t(st));
        p += size_t(len);
        for (int r = 0; r < vc && r < 4; ++r) {
            std::printf("    ");
            for (int k = 0; k < st; ++k) std::printf("%02x%s", rec[size_t(r) * size_t(st) + size_t(k)], (k % 4 == 3) ? " " : "");
            std::printf("\n");
        }
    }
    std::printf("consumed %zu of %zu\n", p, d.size());
    return 0;
}
