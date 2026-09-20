#include "forge/modorder.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

#include "nlohmann/json.hpp"

namespace forge::modorder {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr const char* kFileName = "forge_mods.json";

// ---- SHA-256 (FIPS 180-4), enough for a pack identity ----
class Sha256 {
public:
    Sha256() { reset(); }
    void reset() {
        h_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        len_ = 0; bufLen_ = 0;
    }
    void update(const uint8_t* p, size_t n) {
        len_ += n;
        while (n) {
            const size_t take = std::min(n, size_t(64) - bufLen_);
            std::memcpy(buf_ + bufLen_, p, take);
            bufLen_ += take; p += take; n -= take;
            if (bufLen_ == 64) { block(buf_); bufLen_ = 0; }
        }
    }
    std::string hex() {
        const uint64_t bits = len_ * 8;
        uint8_t pad = 0x80; update(&pad, 1);
        uint8_t zero = 0;
        while (bufLen_ != 56) update(&zero, 1);
        uint8_t lenBytes[8];
        for (int i = 0; i < 8; ++i) lenBytes[i] = uint8_t(bits >> (56 - 8 * i));
        update(lenBytes, 8);
        static const char* digits = "0123456789abcdef";
        std::string out;
        for (uint32_t v : h_) for (int i = 28; i >= 0; i -= 4) out.push_back(digits[(v >> i) & 0xf]);
        return out;
    }
private:
    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void block(const uint8_t* p) {
        static const uint32_t k[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) | p[i * 4 + 3];
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = h + S1 + ch + k[i] + w[i];
            const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = S0 + maj;
            h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
    }
    std::array<uint32_t, 8> h_{};
    uint64_t len_ = 0;
    uint8_t buf_[64] = {};
    size_t bufLen_ = 0;
};

void hashFile(Sha256& s, const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + p.string());
    char buf[65536];
    while (in.read(buf, sizeof buf) || in.gcount()) s.update(reinterpret_cast<const uint8_t*>(buf), size_t(in.gcount()));
}

std::string lower(std::string s) { for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; }

fs::path resolveSource(const fs::path& gameRoot, const std::string& source) {
    const fs::path p(source);
    return p.is_absolute() ? p : (gameRoot / p);
}

} // namespace

const char* kindName(Kind k) {
    switch (k) {
        case Kind::Fmp: return "fmp";
        case Kind::Patch: return "patch";
        case Kind::Tree: return "tree";
        case Kind::EgoCore: return "egocore";
        case Kind::Qst: return "qst";
        default: return "unknown";
    }
}

Kind classify(const fs::path& source) {
    std::error_code ec;
    if (fs::is_regular_file(source, ec)) {
        const std::string ext = lower(source.extension().string());
        if (ext == ".fmp") return Kind::Fmp;
        if (ext == ".patch") return Kind::Patch;
        if (ext == ".qst") return Kind::Qst;
        return Kind::Unknown;
    }
    if (fs::is_directory(source, ec)) {
        const std::string leaf = source.filename().string();
        if (fs::exists(source / (leaf + ".dll"), ec)) return Kind::EgoCore;
        if (fs::is_directory(source / "Data", ec) || fs::is_directory(source / "data", ec) || fs::is_directory(source / "FSE", ec)) return Kind::Tree;   // an FSE-only pack (quests + scripts) is a tree too
        // a folder holding a single pack file
        int files = 0; fs::path only;
        for (const auto& de : fs::directory_iterator(source, ec)) if (de.is_regular_file(ec)) { ++files; only = de.path(); }
        if (files == 1) return classify(only);
    }
    return Kind::Unknown;
}

fs::path orderPath(const fs::path& gameRoot) { return gameRoot / kFileName; }

Order load(const fs::path& gameRoot) {
    Order order;
    std::ifstream in(orderPath(gameRoot));
    if (!in) return order;
    json j; in >> j;
    order.version = j.value("version", 1);
    for (const auto& m : j.value("mods", json::array())) {
        Entry e;
        e.name = m.value("name", ""); e.source = m.value("source", ""); e.sha256 = m.value("sha256", "");
        e.enabled = m.value("enabled", true); e.note = m.value("note", "");
        const std::string k = m.value("kind", "unknown");
        e.kind = k == "fmp" ? Kind::Fmp : k == "patch" ? Kind::Patch : k == "tree" ? Kind::Tree : k == "egocore" ? Kind::EgoCore : k == "qst" ? Kind::Qst : Kind::Unknown;
        order.mods.push_back(std::move(e));
    }
    return order;
}

void save(const fs::path& gameRoot, const Order& order) {
    json mods = json::array();
    for (const auto& e : order.mods)
        mods.push_back({{"name", e.name}, {"source", e.source}, {"kind", kindName(e.kind)}, {"sha256", e.sha256}, {"enabled", e.enabled}, {"note", e.note}});
    std::ofstream out(orderPath(gameRoot), std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + orderPath(gameRoot).string());
    out << json{{"version", order.version}, {"mods", mods}}.dump(2) << "\n";
}

std::string sha256Of(const fs::path& p) {
    Sha256 s;
    std::error_code ec;
    if (fs::is_regular_file(p, ec)) { hashFile(s, p); return s.hex(); }
    if (!fs::is_directory(p, ec)) throw std::runtime_error("no such file or folder: " + p.string());
    std::vector<fs::path> files;
    for (const auto& de : fs::recursive_directory_iterator(p, ec)) if (de.is_regular_file(ec)) files.push_back(de.path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        const std::string rel = fs::relative(f, p, ec).generic_string();
        s.update(reinterpret_cast<const uint8_t*>(rel.data()), rel.size());
        s.update(reinterpret_cast<const uint8_t*>("\n"), 1);
        hashFile(s, f);
    }
    return s.hex();
}

int indexOf(const Order& order, const std::string& nameOrIndex) {
    if (!nameOrIndex.empty() && std::all_of(nameOrIndex.begin(), nameOrIndex.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
        const int i = std::stoi(nameOrIndex);
        return i >= 0 && size_t(i) < order.mods.size() ? i : -1;
    }
    for (size_t i = 0; i < order.mods.size(); ++i)
        if (lower(order.mods[i].name) == lower(nameOrIndex)) return int(i);
    return -1;
}

Entry& add(Order& order, const fs::path& gameRoot, const fs::path& source, const std::string& name, int at) {
    std::error_code ec;
    // as typed (relative to the current directory) first, then relative to the game root
    fs::path abs = fs::absolute(source, ec);
    if (!fs::exists(abs, ec)) abs = resolveSource(gameRoot, source.string());
    if (!fs::exists(abs, ec)) throw std::runtime_error("no such source: " + source.string());
    abs = fs::weakly_canonical(abs, ec);
    Entry e;
    e.kind = classify(abs);
    if (e.kind == Kind::Unknown) throw std::runtime_error("not a mod source (need .fmp / .patch / .qst / a folder with Data/ or FSE/ / an EgoCore Mods/<Name>/ folder): " + abs.string());
    e.sha256 = sha256Of(abs);
    for (const auto& m : order.mods)
        if (m.sha256 == e.sha256) throw std::runtime_error("already in the order as \"" + m.name + "\" (same contents)");
    e.name = name.empty() ? (abs.filename().string().empty() ? abs.parent_path().filename().string() : abs.stem().string()) : name;
    if (indexOf(order, e.name) >= 0) throw std::runtime_error("a mod named \"" + e.name + "\" is already in the order; pass --name");
    // stored root-relative when the pack lives inside the game root (a portable order), absolute otherwise
    const fs::path rootAbs = fs::weakly_canonical(gameRoot, ec);
    const fs::path rel = fs::relative(abs, rootAbs, ec);
    const bool inside = !ec && !rel.empty() && rel.generic_string().rfind("..", 0) != 0;
    e.source = inside ? rel.generic_string() : abs.generic_string();
    if (at < 0 || size_t(at) >= order.mods.size()) { order.mods.push_back(std::move(e)); return order.mods.back(); }
    order.mods.insert(order.mods.begin() + at, std::move(e));
    return order.mods[size_t(at)];
}

void remove(Order& order, const std::string& nameOrIndex) {
    const int i = indexOf(order, nameOrIndex);
    if (i < 0) throw std::runtime_error("no mod \"" + nameOrIndex + "\" in the order");
    order.mods.erase(order.mods.begin() + i);
}

void move(Order& order, const std::string& nameOrIndex, int to) {
    const int i = indexOf(order, nameOrIndex);
    if (i < 0) throw std::runtime_error("no mod \"" + nameOrIndex + "\" in the order");
    Entry e = order.mods[size_t(i)];
    order.mods.erase(order.mods.begin() + i);
    to = std::clamp(to, 0, int(order.mods.size()));
    order.mods.insert(order.mods.begin() + to, std::move(e));
}

void setEnabled(Order& order, const std::string& nameOrIndex, bool enabled) {
    const int i = indexOf(order, nameOrIndex);
    if (i < 0) throw std::runtime_error("no mod \"" + nameOrIndex + "\" in the order");
    order.mods[size_t(i)].enabled = enabled;
}

std::vector<std::string> buildLabels(const Order& order) {
    std::vector<std::string> out;
    for (const auto& e : order.mods) if (e.enabled) out.push_back(e.name);
    return out;
}

std::vector<std::string> buildSources(const Order& order, const fs::path& gameRoot) {
    std::vector<std::string> out;
    for (const auto& e : order.mods) {
        if (!e.enabled) continue;
        fs::path p = resolveSource(gameRoot, e.source);
        std::error_code ec;
        // a folder that holds one pack file stands for that file
        if (fs::is_directory(p, ec) && (e.kind == Kind::Fmp || e.kind == Kind::Patch || e.kind == Kind::Qst))
            for (const auto& de : fs::directory_iterator(p, ec)) if (de.is_regular_file(ec)) { p = de.path(); break; }
        out.push_back(p.generic_string());
    }
    return out;
}

} // namespace forge::modorder
