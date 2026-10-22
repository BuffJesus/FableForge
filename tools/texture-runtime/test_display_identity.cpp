#include "display_identity.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>
#ifdef HAVE_DISPLAY_RECORDS
#include "generated_display_records.hpp"
#include "generated_frontend_records.hpp"
#endif
#ifdef HAVE_HUD_RECORDS
#include "generated_hud_records.hpp"
#endif

using namespace texture_display;
constexpr std::uint32_t base=0x20000000,bank=base,symbols=base+0x1000,graphics=base+0x2000;
std::vector<unsigned char> memory(0x10000);
std::uint32_t cursor=base+0x4000;
void require(bool pass) { if (!pass) { std::cerr<<"FAIL\n"; std::exit(1); } }
template<class T> void put(std::uint32_t address,const T& value) {
    require(address>=base && std::uint64_t(address)+sizeof(value)<=base+memory.size());
    std::memcpy(memory.data()+address-base,&value,sizeof(value));
}
void text(std::uint32_t address,const char* value) {
    auto shared=cursor; cursor+=32;auto ptr=cursor;
    auto length=static_cast<std::uint32_t>(std::strlen(value));cursor+=length+16;
    put(address,shared);put(shared,std::array<std::uint32_t,2>{ptr,length});
    std::memcpy(memory.data()+ptr-base,value,length+1);
}
int main() {
    static_assert(sizeof(void*)==4,"Test the x86 runtime ABI");
    put(bank+8,std::array<std::uint32_t,2>{symbols,symbols+12});
    put(bank+0x1e0,std::array<std::uint32_t,2>{graphics,graphics+132});
    text(bank+0x88,"GBANK_TEST");
    const Record rows[]={
        {"GBANK_TEST","LARGE",{64,128,0,64,128,1},{32,32}},
        {"GBANK_TEST","UNCHANGED",{32,16,0,32,16,1},{32,16}}};
    text(symbols,"LARGE");text(symbols+4,"UNCHANGED");text(symbols+8,"UNKNOWN");
    put(graphics,rows[0].current);put(graphics+44,rows[1].current);
    put(graphics+88,std::array<std::uint16_t,6>{16,16,0,16,16,1});
    auto read=[](std::uint32_t address,void* out,std::size_t size) {
        if(address<base || std::uint64_t(address)+size>base+memory.size()) return false;
        std::memcpy(out,memory.data()+address-base,size); return true;
    };
    Resolver resolver(read,std::span<const Record>(rows));
    unsigned cases=0;
    auto check=[&](unsigned index,unsigned axis,unsigned native,unsigned value,Reason reason) {
        auto actual=resolver.extent(bank,index,axis,native);
        require(actual.value==value && actual.reason==reason);++cases;
    };
    check(0,0,64,32,Reason::matched);check(0,1,128,32,Reason::matched);
    check(1,0,32,32,Reason::matched);check(2,0,16,16,Reason::unlisted);
    check(919,0,64,64,Reason::unreadable); // disk entry ID must not be used as index
    check(0,0,60,60,Reason::dimensions);check(0,2,64,64,Reason::invalidAxis);
    put(graphics+6,std::uint16_t(63));check(0,0,63,63,Reason::dimensions);
    put(graphics+6,std::uint16_t(64));
    put(bank+12,symbols+11);check(0,0,64,64,Reason::unreadable);
    put(bank+12,symbols+12);
    put(bank+0x1e4,graphics+131);check(0,0,64,64,Reason::unreadable);
    put(bank+0x1e4,graphics+132);
    put(symbols,std::uint32_t(0xfffffffcu));check(0,0,64,64,Reason::unreadable);
    text(symbols,"LARGE");
    put(bank+0x88,std::uint32_t(0));check(0,0,64,64,Reason::unlisted);
    require(resolver.extent(0xffffff80u,0,0,64).reason==Reason::unreadable);++cases;
    std::cout<<"PASS "<<cases<<" native x86 identity cases\n";
#ifdef HAVE_DISPLAY_RECORDS
    Resolver full(read,std::span<const Record>(displayRecords));
    unsigned manifestCases=0;
    // Runtime index zero deliberately differs from almost every disk entry ID.
    for (const auto& row:displayRecords) {
        cursor=base+0x4000;
        text(bank+0x88,row.bank);text(symbols,row.symbol);put(graphics,row.current);
        for (unsigned axis=0;axis<2;++axis) {
            auto actual=full.extent(bank,0,axis,row.current[3+axis]);
            require(actual.reason==Reason::matched && actual.value==row.original[axis]);
            ++manifestCases;
        }
    }
    std::cout<<"PASS "<<manifestCases<<" native manifest extent lookups\n";
    cursor=base+0x8000;text(bank+0x88,"GBANK_FRONT_END");
    put(bank+8,std::array<std::uint32_t,2>{0,0});
    put(bank+0x1e0,std::array<std::uint32_t,2>{graphics,graphics+396*44});
    unsigned frontendCases=0;
    for(const auto& row:frontendRecords) {
        auto address=graphics+row.index*44;
        put(address,row.header);put(address+32,std::array<std::uint32_t,2>{bank,row.index});
        for(unsigned axis=0;axis<2;++axis) {
            std::uint16_t native=0;std::memcpy(&native,row.header.data()+6+axis*2,2);
            auto actual=full.frontendExtent(bank,row.index,axis,native,frontendRecords);
            require(actual.reason==Reason::matched && actual.value==row.original[axis]);
            put(address+27,static_cast<unsigned char>(row.header[27]^1));
            actual=full.frontendExtent(bank,row.index,axis,native,frontendRecords);
            require(actual.reason==Reason::dimensions && actual.value==native);
            put(address,row.header);frontendCases+=2;
        }
    }
    auto refusal=[&](unsigned index,Reason expected) {
        auto actual=full.frontendExtent(bank,index,0,17,frontendRecords);
        require(actual.reason==expected && actual.value==17);++frontendCases;
    };
    refusal(0,Reason::unlisted);refusal(395,Reason::unlisted);refusal(396,Reason::unreadable);
    text(bank+0x88,"GBANK_MAIN");refusal(1,Reason::unlisted);
    text(bank+0x88,"GBANK_FRONT_END");
    put(bank+0xc,std::uint32_t(4));refusal(1,Reason::unlisted);
    put(bank+0xc,std::uint32_t(0));put(graphics+44+32,std::uint32_t(0));refusal(1,Reason::dimensions);
    std::cout<<"PASS "<<frontendCases<<" frontend exact-header and refusal cases\n";
#endif
#ifdef HAVE_HUD_RECORDS
    memory.assign(0x100000,0);cursor=base+0x90000;
    text(bank+0x88,"GBANK_MAIN");
    put(bank+8,std::array<std::uint32_t,2>{0,0});
    put(bank+0x1e0,std::array<std::uint32_t,2>{graphics,graphics+mainRecordCount*44});
    Resolver hud(read,std::span<const Record>{});
    unsigned hudCases=0;
    for(const auto& row:mainRecords) {
        const auto address=graphics+row.index*44;
        put(address,row.header);put(address+32,std::array<std::uint32_t,2>{bank,row.index});
        for(unsigned axis=0;axis<2;++axis) {
            std::uint16_t native=0;std::memcpy(&native,row.header.data()+6+axis*2,2);
            auto lookup=[&] {return hud.indexedExtent(bank,row.index,axis,native,"GBANK_MAIN",mainRecordCount,mainRecords);};
            auto actual=lookup();require(actual.reason==Reason::matched && actual.value==row.original[axis]);
            put(address+32,std::uint32_t(bank+4));
            actual=lookup();require(actual.reason==Reason::dimensions && actual.value==native);
            put(address+32,bank);
            put(address+27,static_cast<unsigned char>(row.header[27]^1));
            actual=lookup();require(actual.reason==Reason::dimensions && actual.value==native);
            put(address,row.header);hudCases+=3;
        }
    }
    auto reject=[&](unsigned index,Reason reason) {
        auto actual=hud.indexedExtent(bank,index,0,17,"GBANK_MAIN",mainRecordCount,mainRecords);
        require(actual.reason==reason && actual.value==17);++hudCases;
    };
    reject(0,Reason::unlisted);reject(mainRecordCount-1,Reason::unlisted);reject(mainRecordCount,Reason::unreadable);
    text(bank+0x88,"GBANK_FRONT_END");reject(1,Reason::unlisted);text(bank+0x88,"GBANK_MAIN");
    put(bank+0xc,std::uint32_t(4));reject(1,Reason::unlisted);put(bank+0xc,std::uint32_t(0));
    put(bank+0x1e4,graphics+(mainRecordCount-1)*44);reject(1,Reason::unlisted);
    std::cout<<"PASS "<<hudCases<<" main-bank exact-header, owner and refusal cases\n";
#endif
}
