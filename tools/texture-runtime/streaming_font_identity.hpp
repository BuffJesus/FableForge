#pragma once
#include <cstdint>
#include <cstddef>

namespace texture_font {
// Established only after verifying the source archive and the live bank during
// initialization. Invalidate before bank Close/reinitialization; addresses can
// be reused. This matcher does not establish file identity or lifecycle itself.
struct BankPin {
    std::uint32_t bank=0, streamingEntry=0, entryIndex=0;
    bool active=false;
};

template<class Reader>
bool matchesLiveBank(Reader&& read,std::uint32_t font,const BankPin& pin) {
    if(!pin.active || !pin.bank || !pin.streamingEntry) return false;
    auto word=[&](std::uint64_t address,std::uint32_t& value) {
        return address>=0x10000 && address+4<=0x100000000ull &&
               read(std::uint32_t(address),&value,std::size_t(4));
    };
    std::uint32_t bank=0,bankVtable=0,entry=0,entryVtable=0,owner=0,index=0;
    // Retail constructor A5F290 writes vtable 129D5CC; streaming entry at188.
    // Retail CBankFileAsyncEntry uses vtable129B880,owner+4,index+8.
    return word(std::uint64_t(font)+0x10,bank) && bank==pin.bank &&
           word(bank,bankVtable) && bankVtable==0x129d5cc &&
           word(std::uint64_t(bank)+0x188,entry) && entry==pin.streamingEntry &&
           word(entry,entryVtable) && entryVtable==0x129b880 &&
           word(std::uint64_t(entry)+4,owner) && owner==bank &&
           word(std::uint64_t(entry)+8,index) && index==pin.entryIndex;
}

// Extract the actual open disk handle, not a bank label or a pathname. Retail
// CBankFile::ReadEntryData uses +7c; CDiskFileWin32::Read uses +10, IsOpen +14.
// The handle still needs OS file identity/content verification before activation.
template<class Reader>
bool liveDiskHandle(Reader&& read,std::uint32_t font,const BankPin& pin,std::uint32_t& output) {
    if(!matchesLiveBank(read,font,pin)) return false;
    auto bounded=[&](std::uint64_t address,void* out,std::size_t size) {
        return address>=0x10000 && address+size<=0x100000000ull &&
               read(std::uint32_t(address),out,size);
    };
    std::uint32_t disk=0,vtable=0,handle=0;std::uint8_t open=0;
    if(!bounded(std::uint64_t(pin.bank)+0x7c,&disk,4) ||
       !bounded(disk,&vtable,4) || vtable!=0x122d06c ||
       !bounded(std::uint64_t(disk)+0x14,&open,1) || open!=1 ||
       !bounded(std::uint64_t(disk)+0x10,&handle,4) || !handle || handle==0xffffffffu) return false;
    output=handle;return true;
}

// Single game-thread lifecycle state. Hooks must call begin before initialization
// and invalidate before cache teardown. Archive verification belongs between
// begin and commit; a late result from an earlier bank cannot reactivate it.
class BankBinding {
    std::uint64_t generation_=0;
    std::uint32_t initializingBank_=0;
    BankPin pin_{};
public:
    std::uint64_t begin(std::uint32_t bank) {
        ++generation_;initializingBank_=bank;pin_={};return generation_;
    }
    bool commit(std::uint64_t generation,const BankPin& verified) {
        if(generation!=generation_ || !initializingBank_ ||
           verified.bank!=initializingBank_ || !verified.streamingEntry || !verified.active) return false;
        pin_=verified;initializingBank_=0;return true;
    }
    void invalidate() { ++generation_;initializingBank_=0;pin_={}; }
    bool close(std::uint32_t bank) {
        if(bank==pin_.bank || bank==initializingBank_) { invalidate();return true; }
        return false;
    }
    template<class Reader> bool matches(Reader&& read,std::uint32_t font) const {
        return matchesLiveBank(read,font,pin_);
    }
};
}
