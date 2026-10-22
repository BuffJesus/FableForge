#pragma once
#include <string>
namespace albion::textdisplay {
// Text metadata is stored as single bytes, unlike UTF-16 content. Map each
// byte to its Latin-1 code point for UTF-8 display/export; writers keep raw bytes.
inline std::string metadataUtf8(const std::string& bytes) {
    std::string out;out.reserve(bytes.size());
    for(unsigned char c:bytes) {
        if(c<128) out.push_back(char(c));
        else {out.push_back(char(0xC0|(c>>6)));out.push_back(char(0x80|(c&0x3F)));}
    }
    return out;
}
} // namespace albion::textdisplay
