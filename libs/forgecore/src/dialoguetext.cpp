#include "forge/dialoguetext.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <unordered_map>

#include "forge/big.hpp"
#include "forge/lut.hpp"
#include "forge/textbig.hpp"

namespace forge::dialoguetext {
namespace {

uint32_t read32(const std::vector<uint8_t>& bytes,size_t offset) {
    return uint32_t(bytes[offset]) | (uint32_t(bytes[offset+1])<<8) |
           (uint32_t(bytes[offset+2])<<16) | (uint32_t(bytes[offset+3])<<24);
}

uint32_t crc0(const std::string& value) {
    uint32_t crc=0;
    for(const unsigned char ch:value) {
        crc^=ch;
        for(int bit=0;bit<8;++bit)
            crc=(crc>>1)^(0xEDB88320u & uint32_t(-int32_t(crc&1)));
    }
    return crc;
}

std::unordered_map<uint32_t,uint32_t> readSnds(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    if(!input) throw std::runtime_error("cannot open sound name table: "+path.string());
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),{}};
    if(bytes.size()<4) throw std::runtime_error("sound name table is truncated: "+path.string());
    const uint32_t count=read32(bytes,0);
    if(count>(bytes.size()-4)/8 || bytes.size()!=4+size_t(count)*8)
        throw std::runtime_error("sound name table length is invalid: "+path.string());
    std::unordered_map<uint32_t,uint32_t> out;
    out.reserve(count);
    uint32_t previous=0;
    for(uint32_t i=0;i<count;++i) {
        const uint32_t key=read32(bytes,4+size_t(i)*8);
        const uint32_t id=read32(bytes,8+size_t(i)*8);
        if((i && key<=previous) || !id || !out.emplace(key,id).second)
            throw std::runtime_error("sound name table is not sorted and unique: "+path.string());
        previous=key;
    }
    return out;
}

std::string speechKey(std::string value) {
    for(char& ch:value) ch=char(std::tolower(static_cast<unsigned char>(ch)));
    if(value.ends_with(".lut")) value.replace(value.size()-4,4,".lug");
    return value;
}

std::string asciiLower(std::string value) {
    for(char& ch:value) ch=char(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

} // namespace

Index Index::open(const std::filesystem::path& textBig,
                  const std::filesystem::path& defsRoot,
                  const std::string& language) {
    static constexpr std::array<const char*,4> speechBanks={
        "Dialogue.lug","Dialogue2.lug","ScriptDialogue.lug","ScriptDialogue2.lug"};
    struct Table { std::string bank; std::unordered_map<uint32_t,uint32_t> ids; };
    std::unordered_map<std::string,Table> tables;
    for(const char* speechBank:speechBanks) {
        const auto pair=forge::lut::dialoguePair(speechBank,language);
        if(!pair) throw std::runtime_error("invalid dialogue language: "+language);
        tables.emplace(speechKey(speechBank),Table{pair->lipsyncBank,
                          readSnds(defsRoot/pair->sndsFilename)});
    }
    const auto text=forge::big::File::openFully(textBig);
    Index index;
    for(const auto& bank:text.banks()) for(const auto& record:bank.entries) {
        if(record.type!=0 || !record.length || record.name.empty()) continue;
        const auto value=forge::textbig::decode(text.entryData(record),0);
        const auto table=tables.find(speechKey(value.speechBank));
        if(table==tables.end()) continue;
        const auto id=table->second.ids.find(crc0("SND_"+record.name));
        if(id==table->second.ids.end()) continue;
        index.lines_[{table->second.bank,id->second}].push_back(
            {record.name,value.content,value.speaker});
        ++index.resolvedCount_;
    }
    return index;
}

const std::vector<Line>* Index::find(const std::string& lipsyncBank,
                                     uint32_t soundId) const {
    const auto found=lines_.find({lipsyncBank,soundId});
    return found==lines_.end()?nullptr:&found->second;
}

std::vector<Match> Index::search(const std::string& lipsyncBank,
                                 const std::string& query,size_t limit) const {
    std::vector<Match> matches;
    if(query.empty() || !limit) return matches;
    const std::string needle=asciiLower(query);
    for(auto it=lines_.lower_bound({lipsyncBank,0});
        it!=lines_.end() && it->first.first==lipsyncBank;++it) {
        for(const auto& line:it->second) {
            if(asciiLower(line.name).find(needle)==std::string::npos &&
               asciiLower(line.content).find(needle)==std::string::npos &&
               asciiLower(line.speaker).find(needle)==std::string::npos) continue;
            matches.push_back({it->first.second,line});
            if(matches.size()==limit) return matches;
        }
    }
    return matches;
}

} // namespace forge::dialoguetext
