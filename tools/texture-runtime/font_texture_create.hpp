#pragma once
#include <cstdint>

namespace texture_font {
struct TextureExtent { std::int32_t width,height; };
using NativeTextureCreate=bool(__thiscall*)(void*,const TextureExtent*,std::int32_t,
                                          const void*,std::uint32_t,std::uint32_t,bool,bool);
// Redirect only the three font-cache creation calls. Changing the caller's
// EBX/EDI dimensions also changes palette sizes in native fallback paths.
inline bool createFontCacheTexture(NativeTextureCreate native,void* texture,
                                  const TextureExtent* extent,std::int32_t levels,
                                  const void* format,std::uint32_t flags,
                                  std::uint32_t pool,bool dynamic,bool automaticMipmaps,
                                  bool doubled) {
    if(!native || !extent) return false;
    TextureExtent requested=*extent;
    if(doubled) {
        if(requested.width!=1024 || requested.height!=512) return false;
        requested={2048,1024};
    }
    return native(texture,&requested,levels,format,flags,pool,dynamic,automaticMipmaps);
}
}
