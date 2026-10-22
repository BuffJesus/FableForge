#include "streaming_font_identity.hpp"
#include <array>
#include <cstring>
#include <cstdio>
#include <stdexcept>
static void require(bool x){if(!x)throw std::runtime_error("font bank identity check failed");}
int main(){
    std::array<unsigned char,0x4000> memory{};
    auto put=[&](unsigned address,unsigned value){std::memcpy(memory.data()+address-0x20000000,&value,4);};
    auto read=[&](unsigned address,void* out,std::size_t size){
        if(address<0x20000000 || std::uint64_t(address)+size>0x20004000) return false;
        std::memcpy(out,memory.data()+address-0x20000000,size);return true;
    };
    constexpr unsigned font=0x20000000,bank=0x20001000,entry=0x20002000;
    put(font+0x10,bank);put(bank,0x129d5cc);put(bank+0x188,entry);
    put(entry,0x129b880);put(entry+4,bank);put(entry+8,9);
    texture_font::BankPin pin{bank,entry,9,true};
    require(texture_font::matchesLiveBank(read,font,pin));
    unsigned refused=0;
    for(auto address:{font+0x10,bank,bank+0x188,entry,entry+4,entry+8}){
        auto before=memory;put(address,0x20003000);
        require(!texture_font::matchesLiveBank(read,font,pin));memory=before;++refused;
    }
    require(!texture_font::matchesLiveBank(read,0xfffffff8,pin));++refused;
    require(!texture_font::matchesLiveBank(read,0x10000,pin));++refused;
    pin.active=false;require(!texture_font::matchesLiveBank(read,font,pin));++refused;
    pin.active=true;
    constexpr unsigned disk=0x20003000;
    put(bank+0x7c,disk);put(disk,0x122d06c);put(disk+0x10,0x1234);put(disk+0x14,1);
    unsigned handle=0;
    require(texture_font::liveDiskHandle(read,font,pin,handle) && handle==0x1234);
    for(const auto change:std::array<std::array<unsigned,2>,7>{{
        {bank+0x7c,0xfffffff8},{disk,0},{disk+0x14,0},{disk+0x14,2},
        {disk+0x10,0},{disk+0x10,0xffffffff},{entry+4,bank+0x100}}}) {
        auto saved=memory;put(change[0],change[1]);handle=0xabcdef;
        require(!texture_font::liveDiskHandle(read,font,pin,handle) && handle==0xabcdef);
        memory=saved;
    }
    texture_font::BankBinding binding;
    const auto first=binding.begin(bank);
    require(!binding.matches(read,font));
    require(binding.commit(first,pin) && binding.matches(read,font));
    require(!binding.commit(first,pin)); // completed initialization cannot be replayed
    binding.close(bank+0x100);require(binding.matches(read,font));
    binding.close(bank);require(!binding.matches(read,font));
    require(!binding.commit(first,pin));
    const auto reused=binding.begin(bank); // same addresses, new lifetime
    require(!binding.commit(first,pin) && !binding.matches(read,font));
    auto foreign=pin;foreign.bank+=0x100;
    require(!binding.commit(reused,foreign));
    require(binding.commit(reused,pin) && binding.matches(read,font));
    const auto interrupted=binding.begin(bank);
    binding.invalidate();require(!binding.commit(interrupted,pin));
    require(!binding.matches(read,font));
    std::printf("PASS font-bank owner/entry identity and %u mismatch/unreadable/inactive refusals\n",refused);
    std::puts("PASS bank close/reuse and stale initialization completion cannot reactivate a binding");
    std::puts("PASS live disk-handle extraction and 7 malformed/closed/foreign-bank refusals");
}
