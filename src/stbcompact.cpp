#include "stbcompact.hpp"

#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

namespace albion::stbcompact {

fs::path bankPath(const fs::path& installRoot) {
    return installRoot / "data" / "Levels" / "FinalAlbion_RT.stb";
}

forge::stb::CompactReport measure(const fs::path& installRoot) {
    return forge::stb::compactMeasure(bankPath(installRoot));
}

Result compact(const fs::path& installRoot) {
    Result r;
    const fs::path stb = bankPath(installRoot);
    try {
        const auto before = forge::stb::compactMeasure(stb);
        if (before.deadBytes() == 0) { r.ok = true; r.alreadyCompact = true; r.report = before; return r; }
        const fs::path orig = stb.string() + ".atlas-orig";
        if (!fs::exists(orig)) fs::copy_file(stb, orig);   // the one-time retail backup, like every deploy
        // write beside the bank, verify every payload survived, then swap in
        const fs::path tmp = stb.string() + ".compact-tmp";
        r.report = forge::stb::compactBank(stb, tmp);
        const auto a = forge::stb::Archive::open(stb), b = forge::stb::Archive::open(tmp);
        if (a.entries().size() != b.entries().size() || a.staticMaps().size() != b.staticMaps().size())
            throw std::runtime_error("compacted bank lists a different entry set");
        for (size_t i = 0; i < a.entries().size(); ++i) {
            const auto& x = a.entries()[i];
            const auto& y = b.entries()[i];
            if (x.id != y.id || x.name != y.name || x.size != y.size || a.read(x) != b.read(y))
                throw std::runtime_error("compacted payload differs: " + x.name);
        }
        std::error_code ec;
        fs::rename(tmp, stb, ec);
        if (ec) { fs::remove(tmp, ec); throw std::runtime_error("cannot replace the bank (is Fable.exe running?)"); }
        r.ok = true;
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

}  // namespace albion::stbcompact
