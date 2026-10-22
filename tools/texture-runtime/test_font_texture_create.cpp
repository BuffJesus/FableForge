#include "font_texture_create.hpp"
#include <cstdio>
#include <stdexcept>
static void require(bool ok){if(!ok)throw std::runtime_error("font texture creation ABI mismatch");}
static bool doubled=false,result=false;static unsigned calls=0;
static void* const object=reinterpret_cast<void*>(0x12340000);
static const void* const format=reinterpret_cast<void*>(0x23450000);
// Fastcall's ignored EDX plus seven stack arguments has the same call frame as
// the retail thiscall target. Executed in Win32 to check compiler ABI forwarding.
static bool __fastcall fake(void* self,void*,const texture_font::TextureExtent* size,
                           std::int32_t levels,const void* pixelFormat,
                           std::uint32_t flags,std::uint32_t pool,bool dynamic,bool mips) {
    require(self==object && levels==1 && pixelFormat==format && flags==0x12345678 && pool==3 && dynamic && !mips);
    require(size->width==(doubled?2048:1024) && size->height==(doubled?1024:512));
    ++calls;return result;
}
int main(){
    static_assert(sizeof(void*)==4,"this probe requires retail x86 calling conventions");
    const auto target=reinterpret_cast<texture_font::NativeTextureCreate>(&fake);
    texture_font::TextureExtent extent{1024,512};
    for(bool d:{false,true})for(bool r:{false,true}) {
        doubled=d;result=r;
        require(texture_font::createFontCacheTexture(target,object,&extent,1,format,0x12345678,3,true,false,d)==r);
        require(extent.width==1024 && extent.height==512);
    }
    require(calls==4);
    extent.width=512;
    require(!texture_font::createFontCacheTexture(target,object,&extent,1,format,0,0,false,false,true));
    require(!texture_font::createFontCacheTexture(target,object,nullptr,1,format,0,0,false,false,true));
    require(calls==4);
    std::puts("PASS Win32 texture creation ABI, original/doubled dimensions, return propagation and immutable caller extents");
}
