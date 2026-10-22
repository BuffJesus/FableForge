#pragma once
#include "streaming_font_source.hpp"
#include "verified_archive.hpp"
#include <memory>

namespace texture_font {
// Runtime-facing gate: archive proof and pixels share one bank lifetime. The
// startup manifest must associate the pinned GlyphData with this archive hash.
class VerifiedFontSource {
    StreamingFontSource source_;
    std::unique_ptr<VerifiedArchive> archive_;
    BankPin pin_{};
    std::uint32_t bank_=0;
public:
    std::uint64_t begin(std::uint32_t bank) {
        archive_.reset();pin_={};bank_=bank;return source_.begin(bank);
    }
    template<class Reader>
    bool commit(Reader&& memory,std::uint32_t font,std::uint64_t generation,
                const BankPin& pin,VerifiedGlyphPixels&& pixels,
                std::unique_ptr<VerifiedArchive>&& proof) {
        std::uint32_t handle=0;
        if(!proof || !liveDiskHandle(memory,font,pin,handle) ||
           !proof->matches(reinterpret_cast<HANDLE>(std::uintptr_t(handle))) ||
           !source_.commit(generation,pin,std::move(pixels))) return false;
        archive_=std::move(proof);pin_=pin;return true;
    }
    void invalidate() {source_.invalidate();archive_.reset();pin_={};bank_=0;}
    void close(std::uint32_t bank) {if(bank==bank_) invalidate();}
    template<class Reader>
    bool matches(Reader&& memory,std::uint32_t font) const {
        std::uint32_t handle=0;
        return archive_ && liveDiskHandle(memory,font,pin_,handle) &&
            archive_->matches(reinterpret_cast<HANDLE>(std::uintptr_t(handle)));
    }
    template<class Reader>
    bool read(Reader&& memory,std::uint32_t font,std::uint32_t offset,
              std::span<std::uint8_t> output) const {
        if(!matches(memory,font)) return false;
        return source_.read(memory,font,offset,output);
    }
};
}
