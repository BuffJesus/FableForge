#pragma once
#include "pinned_alpha.hpp"
#include <span>
#include <cstring>

namespace texture_font {
// Startup-owned original GlyphData. Loading must happen outside DllMain and
// render callbacks. The caller pins the live bank separately; a matching glyph
// ID/metric is not sufficient to authorize reading this bank's pixel bytes.
class VerifiedGlyphPixels {
    std::vector<std::uint8_t> data_;
    std::size_t start_=0;
public:
    bool ready() const { return !data_.empty(); }
    bool load(const std::filesystem::path& path,std::size_t bytes,std::string_view sha) {
        // A failed replacement invalidates the prior bank rather than leaving
        // stale data available under a newly established bank identity.
        clear();
        std::vector<std::uint8_t> candidate;
        if(loadPinnedAlpha(path,bytes,sha,candidate)!=LoadError::none || candidate.size()<20) return false;
        const auto start=std::uint32_t(candidate[8])|(std::uint32_t(candidate[9])<<8)|
                         (std::uint32_t(candidate[10])<<16)|(std::uint32_t(candidate[11])<<24);
        if(start<20 || start>candidate.size()) return false;
        start_=start;data_=std::move(candidate);return true;
    }
    void clear() { data_.clear();start_=0; }
    bool read(bool currentBankMatches,std::uint32_t offset,std::span<std::uint8_t> output) const {
        if(!currentBankMatches || data_.empty()) return false;
        const auto size=data_.size()-start_;
        if(offset>size || output.size()>size-offset) return false;
        if(!output.empty()) std::memcpy(output.data(),data_.data()+start_+offset,output.size());
        return true;
    }
};
}
