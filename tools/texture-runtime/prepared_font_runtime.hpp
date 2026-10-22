#pragma once
#include "verified_font_source.hpp"
#include "streaming_font_cache.hpp"

namespace texture_font {
struct FontAssetPin {
    std::filesystem::path path;
    std::size_t bytes;
    std::string_view sha256;
};
// Startup and glyph staging only; installing/rolling back native cache hooks is
// intentionally separate. Unknown banks fail without touching the enlarged cache.
class PreparedFontRuntime {
    VerifiedFontSource source_;
    std::vector<std::uint8_t> alpha_;
    std::span<const GlyphRecord> records_;
    std::uint32_t bank_=0;
public:
    void invalidate() {source_.invalidate();alpha_.clear();records_={};bank_=0;}
    void close(std::uint32_t bank) {if(bank==bank_) invalidate();}
    // records must have static lifetime (generated compiled identities).
    template<class Reader>
    bool initialize(Reader&& memory,std::uint32_t font,const BankPin& pin,
                    const FontAssetPin& archive,const FontAssetPin& original,
                    const FontAssetPin& alpha,std::span<const GlyphRecord> records) {
        invalidate();
        const auto generation=source_.begin(pin.bank);
        std::uint32_t handle=0;
        if(!liveDiskHandle(memory,font,pin,handle)) return false;
        auto proof=std::make_unique<VerifiedArchive>();
        if(!proof->verify(reinterpret_cast<HANDLE>(std::uintptr_t(handle)),archive.path,archive.bytes,archive.sha256)) return false;
        VerifiedGlyphPixels pixels;
        if(!pixels.load(original.path,original.bytes,original.sha256)) return false;
        std::vector<std::uint8_t> planes;
        if(loadPinnedAlpha(alpha.path,alpha.bytes,alpha.sha256,planes)!=LoadError::none) return false;
        if(!source_.commit(memory,font,generation,pin,std::move(pixels),std::move(proof))) return false;
        alpha_=std::move(planes);records_=records;bank_=pin.bank;return true;
    }
    template<class Reader>
    GlyphSource stage(Reader&& memory,std::uint32_t font,std::span<const std::uint8_t> entry,
                      unsigned slot,std::span<std::uint8_t> cache,std::span<std::uint8_t> dirty) const {
        if(!source_.matches(memory,font)) return GlyphSource::failed;
        auto read=[&](std::uint32_t offset,std::span<std::uint8_t> out) {
            return source_.read(memory,font,offset,out);
        };
        return dispatchGlyph(true,entry,records_,alpha_,slot,read,cache,dirty);
    }
};
}
