#include "streaming_font_source.hpp"
#include "verified_font_source.hpp"
#include <array>
#include <fstream>
#include <cstdio>
#include <stdexcept>
#include <windows.h>
static void require(bool x) { if(!x) throw std::runtime_error("font source lifecycle mismatch"); }
int main() {
    wchar_t folder[MAX_PATH],name[MAX_PATH];
    require(GetTempPathW(MAX_PATH,folder)!=0 && GetTempFileNameW(folder,L"ffs",0,name)!=0);
    struct Cleanup { std::filesystem::path path; ~Cleanup(){std::error_code ec;std::filesystem::remove(path,ec);} } cleanup{name};
    std::array<unsigned char,24> fixture{};fixture[8]=20;
    for(unsigned i=0;i<4;++i) fixture[20+i]=i+1;
    {std::ofstream out(cleanup.path,std::ios::binary);out.write(reinterpret_cast<const char*>(fixture.data()),fixture.size());}
    constexpr auto hash="35cb4c2aa4c18cd706332a3c9c814eb141d543af0eadca44058f4011f75c9e12";
    auto load=[&] {
        texture_font::VerifiedGlyphPixels pixels;
        require(pixels.load(cleanup.path,fixture.size(),hash));return pixels;
    };
    std::array<unsigned char,0x4000> memory{};
    constexpr unsigned font=0x20000000,bank=0x20001000,entry=0x20002000;
    auto put=[&](unsigned at,unsigned value){std::memcpy(memory.data()+at-font,&value,4);};
    auto read=[&](unsigned at,void* out,std::size_t size) {
        if(at<font || std::uint64_t(at)+size>font+memory.size()) return false;
        std::memcpy(out,memory.data()+at-font,size);return true;
    };
    put(font+0x10,bank);put(bank,0x129d5cc);put(bank+0x188,entry);
    put(entry,0x129b880);put(entry+4,bank);put(entry+8,9);
    texture_font::BankPin pin{bank,entry,9,true};
    texture_font::StreamingFontSource source;
    std::array<std::uint8_t,4> output{};
    auto refused=[&] {
        output.fill(0xaa);const auto before=output;
        require(!source.read(read,font,0,output) && output==before);
    };
    auto accepted=[&] {
        require(source.read(read,font,0,output));
        require((output==std::array<std::uint8_t,4>{1,2,3,4}));
    };
    refused();
    const auto first=source.begin(bank);refused();
    texture_font::VerifiedGlyphPixels empty;
    require(!source.commit(first,pin,std::move(empty)));refused();
    auto original=load();require(source.commit(first,pin,std::move(original)));accepted();
    source.close(bank+0x100);accepted();
    put(entry+4,bank+0x100);refused();put(entry+4,bank);accepted();
    source.close(bank);refused();
    auto late=load();require(!source.commit(first,pin,std::move(late)) && late.ready());refused();
    const auto second=source.begin(bank); // identical native addresses, different lifetime
    require(!source.commit(first,pin,std::move(late)) && late.ready());refused();
    auto wrong=pin;wrong.bank+=0x100;
    require(!source.commit(second,wrong,std::move(late)) && late.ready());refused();
    require(source.commit(second,pin,std::move(late)));accepted();
    auto replay=load();require(!source.commit(second,pin,std::move(replay)));accepted();
    const auto third=source.begin(bank);refused();
    source.invalidate();require(!source.commit(third,pin,std::move(replay)));refused();
    const auto fourth=source.begin(bank);source.close(bank);
    require(!source.commit(fourth,pin,std::move(replay)));refused();
    std::puts("PASS verified font pixels follow initialization, close, address reuse and cache teardown; stale completions cannot replace active data");
    struct Handle {HANDLE value;~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
    Handle native{CreateFileW(name,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr)};
    require(native.value!=INVALID_HANDLE_VALUE);
    constexpr unsigned disk=0x20003000;
    put(bank+0x7c,disk);put(disk,0x122d06c);put(disk+0x14,1);
    put(disk+0x10,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(native.value)));
    auto proof=std::make_unique<texture_font::VerifiedArchive>();
    require(proof->verify(native.value,cleanup.path,fixture.size(),hash));
    texture_font::VerifiedFontSource verified;
    const auto generation=verified.begin(bank);
    auto pixels=load();
    auto absent=std::make_unique<texture_font::VerifiedArchive>();
    require(!verified.commit(read,font,generation,pin,std::move(pixels),std::move(absent)));
    require(pixels.ready() && absent);
    require(verified.commit(read,font,generation,pin,std::move(pixels),std::move(proof)) && !proof);
    require(verified.read(read,font,0,output));
    require((output==std::array<std::uint8_t,4>{1,2,3,4}));
    const auto expected=output;
    put(disk+0x10,0xffffffff);
    require(!verified.read(read,font,0,output) && output==expected);
    put(disk+0x10,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(native.value)));
    verified.close(bank+0x100);require(verified.read(read,font,0,output));
    verified.close(bank);require(!verified.read(read,font,0,output) && output==expected);
    Handle writable{CreateFileW(name,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr)};
    require(writable.value!=INVALID_HANDLE_VALUE); // close released the archive proof
    std::puts("PASS runtime source requires held archive proof, rechecks live handles and releases proof on bank close");
}
