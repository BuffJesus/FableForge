#pragma once
#include "streaming_font_identity.hpp"
#include "streaming_font_pixels.hpp"
#include <utility>

namespace texture_font {
// Single game-thread source lifetime. Call begin before bank initialization,
// commit only after archive/live-bank identity verification, and close/invalidate
// before native destruction. This couples data lifetime to the verified binding;
// it does not discover which on-disk archive the native bank actually opened.
class StreamingFontSource {
    BankBinding binding_;
    VerifiedGlyphPixels pixels_;
public:
    std::uint64_t begin(std::uint32_t bank) {
        pixels_.clear();return binding_.begin(bank);
    }
    bool commit(std::uint64_t generation,const BankPin& verified,VerifiedGlyphPixels&& pixels) {
        if(!pixels.ready() || !binding_.commit(generation,verified)) return false;
        pixels_=std::move(pixels);return true;
    }
    void invalidate() { binding_.invalidate();pixels_.clear(); }
    void close(std::uint32_t bank) { if(binding_.close(bank)) pixels_.clear(); }
    template<class Reader>
    bool read(Reader&& memory,std::uint32_t font,std::uint32_t offset,
              std::span<std::uint8_t> output) const {
        return pixels_.read(binding_.matches(memory,font),offset,output);
    }
};
}
