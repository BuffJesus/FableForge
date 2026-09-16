#include "forge/lzo.hpp"

#include <cstring>
#include <mutex>

// minilzo 2.10, vendored at third_party/minilzo. Single-header-ish C lib.
#include "minilzo/minilzo.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace forge::lzo {
namespace {

void ensureInit() {
    // lzo_init() must run once before any compress/decompress. It only validates
    // sizeof assumptions; cheap and idempotent behind a call_once.
    static std::once_flag once;
    static int rc = LZO_E_OK;
    std::call_once(once, [] { rc = ::lzo_init(); });
    if (rc != LZO_E_OK)
        throw std::runtime_error("forge::lzo: lzo_init failed");
}

void putU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v & 0xFF));
    out.push_back(uint8_t((v >> 8) & 0xFF));
    out.push_back(uint8_t((v >> 16) & 0xFF));
    out.push_back(uint8_t((v >> 24) & 0xFF));
}

uint32_t getU32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}

} // namespace

std::vector<uint8_t> compress(const uint8_t* data, size_t len) {
    ensureInit();
    // Worst-case LZO1X expansion: len + len/16 + 64 + 3.
    std::vector<uint8_t> out(len + len / 16 + 64 + 3);
    static thread_local std::vector<uint8_t> wrk(LZO1X_1_MEM_COMPRESS);
    lzo_uint outLen = out.size();
    int rc = ::lzo1x_1_compress(data, (lzo_uint)len, out.data(), &outLen, wrk.data());
    if (rc != LZO_E_OK)
        throw std::runtime_error("forge::lzo: compress failed");
    out.resize(outLen);
    return out;
}

std::vector<uint8_t> compress999(const uint8_t* data, size_t len) {
    ensureInit();
#ifdef _WIN32
    using Compress999Fn = int(__cdecl*)(const unsigned char*, lzo_uint,
                                        unsigned char*, lzo_uint*, void*);
    static HMODULE module = ::LoadLibraryA("liblzo2-2.dll");
    static Compress999Fn fn = module ? reinterpret_cast<Compress999Fn>(
        ::GetProcAddress(module, "lzo1x_999_compress")) : nullptr;
    if (!fn)
        throw std::runtime_error("forge::lzo: liblzo2-2.dll/lzo1x_999_compress unavailable");
    std::vector<uint8_t> out(len + len / 16 + 64 + 3);
    static thread_local std::vector<uint8_t> wrk(0x70000);
    lzo_uint outLen = out.size();
    const int rc = fn(data, lzo_uint(len), out.data(), &outLen, wrk.data());
    if (rc != LZO_E_OK) throw std::runtime_error("forge::lzo: lzo1x_999_compress failed");
    out.resize(outLen);
    return out;
#else
    (void)data; (void)len;
    throw std::runtime_error("forge::lzo: lzo1x_999 unavailable on this platform");
#endif
}

std::vector<uint8_t> decompress(const uint8_t* data, size_t len, size_t uncompLen) {
    ensureInit();
    std::vector<uint8_t> out(uncompLen);
    lzo_uint outLen = uncompLen;
    int rc = ::lzo1x_decompress_safe(data, (lzo_uint)len, out.data(), &outLen, nullptr);
    if (rc != LZO_E_OK || outLen != uncompLen)
        throw std::runtime_error("forge::lzo: decompress failed / length mismatch");
    return out;
}

std::vector<uint8_t> decompressBounded(const uint8_t* data, size_t len,
                                       size_t maxOut) {
    ensureInit();
    std::vector<uint8_t> out(maxOut);
    lzo_uint outLen = maxOut;
    int rc = ::lzo1x_decompress_safe(data, (lzo_uint)len, out.data(), &outLen, nullptr);
    if (rc != LZO_E_OK)
        throw std::runtime_error("forge::lzo: bounded decompress failed");
    out.resize(outLen);
    return out;
}

std::vector<uint8_t> compressFramed(const uint8_t* data, size_t len) {
    std::vector<uint8_t> body = compress(data, len);
    std::vector<uint8_t> out;
    out.reserve(body.size() + 8);
    putU32(out, uint32_t(len));         // uncompressed length
    putU32(out, uint32_t(body.size())); // compressed length
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

std::vector<uint8_t> compressFramed999(const uint8_t* data, size_t len) {
    std::vector<uint8_t> body = compress999(data, len);
    std::vector<uint8_t> out;
    out.reserve(body.size() + 8);
    putU32(out, uint32_t(len));
    putU32(out, uint32_t(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

std::vector<uint8_t> decompressFramed(const std::vector<uint8_t>& frame, size_t& pos) {
    if (pos + 8 > frame.size())
        throw std::runtime_error("forge::lzo: truncated frame header");
    uint32_t uncompLen = getU32(frame.data() + pos);
    uint32_t compLen = getU32(frame.data() + pos + 4);
    if (pos + 8 + compLen > frame.size())
        throw std::runtime_error("forge::lzo: truncated frame body");
    std::vector<uint8_t> out = decompress(frame.data() + pos + 8, compLen, uncompLen);
    pos += 8 + compLen;
    return out;
}

} // namespace forge::lzo
