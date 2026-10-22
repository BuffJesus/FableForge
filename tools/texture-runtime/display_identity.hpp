#pragma once
// Experimental retail x86 identity lookup. No process access or patching here.
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace texture_display {
struct Record {
    const char* bank;
    const char* symbol;
    std::array<std::uint16_t, 6> current; // allocation W/H/depth, frame W/H/count
    std::array<std::uint16_t, 2> original;
};
enum class Reason { matched, unreadable, unlisted, dimensions, invalidAxis };
struct Result { std::uint32_t value; Reason reason; };
struct IndexedRecord {
    std::uint32_t index;
    std::array<unsigned char,28> header;
    std::array<std::uint16_t,2> original;
};

// Reader returns false on inaccessible memory; must copy exactly size bytes.
template<class Reader> class Resolver {
    Reader read_;
    std::span<const Record> records_; // sorted strictly by (bank, symbol)
    bool take(std::uint64_t address, void* destination, std::size_t size) const {
        return address >= 0x10000 && address + size <= 0x100000000ull &&
            read_(static_cast<std::uint32_t>(address), destination, size);
    }
    bool string(std::uint64_t address, std::array<char,4097>& out,
                std::string_view& value) const {
        std::uint32_t shared=0;
        if (!take(address,&shared,4)) return false;
        if (!shared) { value={}; return true; }
        std::array<std::uint32_t,2> data{};
        if (!take(shared,data.data(),8) || data[1]>4096 ||
            !take(data[0],out.data(),data[1]+1) || out[data[1]]!=0) return false;
        for (std::size_t i=0;i<data[1];++i)
            if (out[i]==0 || static_cast<unsigned char>(out[i])>127) return false;
        value={out.data(),data[1]}; return true;
    }
    bool element(std::uint32_t bank, std::uint32_t index, unsigned offset,
                 unsigned stride, std::uint32_t& address) const {
        std::array<std::uint32_t,2> range{};
        if (!take(std::uint64_t(bank)+offset,range.data(),8) || range[1]<range[0]) return false;
        auto bytes=range[1]-range[0];
        if (bytes%stride || bytes/stride>1000000 || index>=bytes/stride) return false;
        address=range[0]+index*stride; return true;
    }
public:
    Resolver(Reader reader, std::span<const Record> records):read_(reader),records_(records) {}
    // Only for a captured/pinned bank. Caller must verify its archive hash.
    Result indexedExtent(std::uint32_t bank,std::uint32_t index,unsigned axis,
                         std::uint32_t native,std::string_view expectedHandle,
                         unsigned count,std::span<const IndexedRecord> rows) const {
        if(axis>1) return {native,Reason::invalidAxis};
        std::array<char,4097> text{};std::string_view handle;
        std::array<std::uint32_t,2> symbols{},range{},owner{};
        std::uint32_t address=0;
        if(!string(std::uint64_t(bank)+0x88,text,handle) ||
           !take(std::uint64_t(bank)+8,symbols.data(),8) ||
           !take(std::uint64_t(bank)+0x1e0,range.data(),8)) return {native,Reason::unreadable};
        if(handle!=expectedHandle || symbols[0]!=symbols[1] || range[1]<range[0] ||
           std::uint64_t(range[1])-range[0]!=std::uint64_t(count)*44) return {native,Reason::unlisted};
        if(!element(bank,index,0x1e0,44,address)) return {native,Reason::unreadable};
        std::size_t lo=0,hi=rows.size();
        while(lo<hi) {
            auto mid=lo+(hi-lo)/2;const auto& row=rows[mid];
            if(index<row.index) hi=mid;
            else if(index>row.index) lo=mid+1;
            else {
                std::array<unsigned char,28> header{};
                if(!take(address,header.data(),28) || !take(std::uint64_t(address)+32,owner.data(),8))
                    return {native,Reason::unreadable};
                std::uint16_t extent=0;std::memcpy(&extent,header.data()+6+axis*2,2);
                if(header!=row.header || owner[0]!=bank || owner[1]!=index ||
                   native!=extent || !row.original[axis]) return {native,Reason::dimensions};
                return {row.original[axis],Reason::matched};
            }
        }
        return {native,Reason::unlisted};
    }
    Result frontendExtent(std::uint32_t bank,std::uint32_t index,unsigned axis,
                          std::uint32_t native,std::span<const IndexedRecord> rows) const {
        return indexedExtent(bank,index,axis,native,"GBANK_FRONT_END",396,rows);
    }
    Result extent(std::uint32_t bank,std::uint32_t index,unsigned axis,std::uint32_t native) const {
        if (axis>1) return {native,Reason::invalidAxis};
        std::uint32_t symbolAddress=0,graphicAddress=0;
        std::array<char,4097> bankText{},symbolText{};
        std::string_view bankName,symbolName;
        std::array<std::uint16_t,6> current{};
        if (!element(bank,index,8,4,symbolAddress) ||
            !element(bank,index,0x1e0,44,graphicAddress) ||
            !string(std::uint64_t(bank)+0x88,bankText,bankName) ||
            !string(symbolAddress,symbolText,symbolName) ||
            !take(graphicAddress,current.data(),12) || !current[0] || !current[1] ||
            !current[3] || !current[4] || current[3]>current[0] || current[4]>current[1])
            return {native,Reason::unreadable};
        std::size_t lo=0,hi=records_.size();
        while (lo<hi) {
            auto mid=lo+(hi-lo)/2; const auto& row=records_[mid];
            auto order=bankName.compare(row.bank);
            if (!order) order=symbolName.compare(row.symbol);
            if (order>0) lo=mid+1;
            else if (order<0) hi=mid;
            else {
                if (current!=row.current || native!=current[3+axis] || !row.original[axis])
                    return {native,Reason::dimensions};
                return {row.original[axis],Reason::matched};
            }
        }
        return {native,Reason::unlisted};
    }
};
}
