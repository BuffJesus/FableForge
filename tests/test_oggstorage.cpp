#include "forge/oggstorage.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

static void require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}
static void patch(std::vector<uint8_t>& data, size_t at, uint32_t value) {
    for (unsigned i=0; i<4; ++i) data.at(at+i)=uint8_t(value>>(8*i));
}
// Deliberately bit-at-a-time, independent of the production lookup table.
static void checksum(std::vector<uint8_t>& data) {
    patch(data,22,0); uint32_t crc=0;
    for (uint8_t value : data) {
        crc^=uint32_t(value)<<24;
        for (unsigned i=0; i<8; ++i)
            crc=(crc&0x80000000u) ? (crc<<1)^0x04c11db7u : crc<<1;
    }
    patch(data,22,crc);
}
static std::vector<uint8_t> page(uint8_t flags, uint32_t sequence,
                               std::vector<uint8_t> lacing, uint64_t granule=0) {
    std::vector<uint8_t> result(27);
    result[0]='O'; result[1]='g'; result[2]='g'; result[3]='S'; result[5]=flags;
    patch(result,6,uint32_t(granule)); patch(result,10,uint32_t(granule>>32));
    patch(result,14,123); patch(result,18,sequence); result[26]=uint8_t(lacing.size());
    result.insert(result.end(),lacing.begin(),lacing.end());
    for (auto length : lacing) result.insert(result.end(),length,0x5a);
    checksum(result); return result;
}
static std::vector<uint8_t> join(std::vector<uint8_t> first, const std::vector<uint8_t>& second) {
    first.insert(first.end(),second.begin(),second.end()); return first;
}
static bool accepts(const std::vector<uint8_t>& bytes) {
    try { forge::oggstorage::validateEnvelope(bytes); return true; }
    catch (const std::runtime_error&) { return false; }
}
int main() { try {
    const auto first=page(2,0,{255},UINT64_MAX), last=page(5,1,{0,3},123);
    const auto valid=join(first,last), original=valid;
    const auto result=forge::oggstorage::validateEnvelope(valid);
    require(result.pages==2 && result.packets==2 && result.payloadBytes==258 && result.serial==123,
            "wrong envelope metrics");
    require(valid==original,"input changed");
    require(accepts(page(6,0,{0})),"nil packet refused");
    for (size_t length=0; length<valid.size(); ++length)
        require(!accepts({valid.begin(),valid.begin()+length}),"truncated stream accepted");
    for (auto [offset,value] : std::vector<std::pair<size_t,uint32_t>>{{14,124},{18,2},{10,0x80000000u}}) {
        auto bad=last; patch(bad,offset,value); checksum(bad);
        require(!accepts(join(first,bad)),"invalid stream metadata accepted");
    }
    for (auto [offset,value] : std::vector<std::pair<size_t,uint8_t>>{{0,'X'},{4,1},{5,4},{5,7},{5,13},{26,0}}) {
        auto bad=last; bad[offset]=value; checksum(bad);
        require(!accepts(join(first,bad)),"invalid page header accepted");
    }
    auto bad=valid; bad.back()^=1; require(!accepts(bad),"damaged payload accepted");
    bad=valid; bad.push_back(0); require(!accepts(bad),"trailing byte accepted");
    require(!accepts(join(valid,page(6,0,{1}))),"chained stream accepted");
    require(!accepts(page(6,0,{255},UINT64_MAX)),"unfinished EOS packet accepted");
    require(!accepts(page(6,0,{1},UINT64_MAX)),"finished packet with missing granule accepted");
    require(!accepts(page(6,0,{})),"unsupported empty page accepted");
    require(!accepts(page(6,1,{1})),"nonzero initial sequence accepted");
    std::cout << "Ogg page framing, CRC, continuation, truncation and envelope scope passed\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
