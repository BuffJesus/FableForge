#include "hud_geometry.hpp"
#include <vector>
#include <iostream>
#include <cstdlib>

int main() {
    constexpr unsigned base=0x10000,bank=base,graphics=base+0x1000,internal=base+0x2000,primitive=base+0x2200;
    std::vector<unsigned char> memory(0x10000);
    auto put=[&](unsigned at,const auto& value){std::memcpy(memory.data()+at-base,&value,sizeof value);};
    auto read=[&](unsigned at,void* out,std::size_t size) {
        if(at<base || std::uint64_t(at)+size>base+memory.size())return false;
        std::memcpy(out,memory.data()+at-base,size);return true;
    };
    put(bank+0x88,base+0x3000);put(base+0x3000,std::array<unsigned,2>{base+0x3100,10});
    std::memcpy(memory.data()+0x3100,"GBANK_MAIN",11);
    put(bank+0x1e0,std::array<unsigned,2>{graphics,graphics+4*44});
    std::array<texture_display::IndexedRecord,2> rows{};
    for(unsigned i=0;i<2;++i) {
        auto& row=rows[i];row.index=i+1;
        const std::array<std::uint16_t,6> dims=i?std::array<std::uint16_t,6>{16,128,0,16,128,1}:std::array<std::uint16_t,6>{64,32,0,64,32,1};
        std::memcpy(row.header.data(),dims.data(),12);row.original=i?std::array<std::uint16_t,2>{7,63}:std::array<std::uint16_t,2>{31,13};
        auto address=graphics+row.index*44,vector=base+0x4000+i*0x100,header=vector+0x40;
        put(address,row.header);put(address+32,std::array<unsigned,2>{bank,row.index});put(address+40,vector);
        put(vector+12,header);put(header,row.header);
    }
    put(internal+0x5c,1u);put(internal+0x60,0u);
    const texture_hud::Box source{100,50,110,55};put(primitive+0xc,source);
    unsigned cases=0;
    auto check=[&](unsigned kind,std::optional<texture_hud::Box> expected) {
        auto actual=texture_hud::correctedBox(read,bank,internal,primitive,kind,4,rows);
        if(actual!=expected){std::cerr<<"FAIL case "<<cases<<"\n";std::exit(1);}++cases;
    };
    put(primitive+0x38,std::uint8_t(1));put(primitive+0x3c,0u);
    check(0,texture_hud::Box{100,50,141,68});
    put(primitive+0x3c,1u);check(0,texture_hud::Box{85,44,125,61}); // integer halves, including odd dimensions
    put(primitive+0x38,std::uint8_t(0));check(0,{});put(primitive+0x38,std::uint8_t(1));
    put(primitive+0x44,std::uint8_t(1));put(primitive+0x48,0u);
    check(1,texture_hud::Box{100,50,141,68});
    put(internal+0x68,2u);check(1,texture_hud::Box{100,50,141,118});
    put(primitive+0x48,1u);check(1,texture_hud::Box{85,19,125,86});
    put(internal+0x68,3u);check(1,{});put(internal+0x68,0u);
    put(internal+0x60,1u);check(0,{});put(internal+0x60,0u);
    put(graphics+44+32,bank+4);check(0,{});put(graphics+44+32,bank);
    put(base+0x4040+6,std::uint16_t(63));check(0,{});put(base+0x4040,rows[0].header);
    put(bank+12,4u);check(0,{});put(bank+12,0u);
    put(internal+0x5c,0u);check(0,{});put(internal+0x5c,1u);
    put(primitive+0x44,std::uint8_t(0));check(1,{});check(2,{});
    std::cout<<"PASS "<<cases<<" sprite/clock centered and anchored bounds, overlay maxima and refusals\n";
}
