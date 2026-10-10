#include "forge/meshpreview.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

static void require(bool okay,const char* message) {
    if(!okay) throw std::runtime_error(message);
}

int main(int argc,char** argv) {
    try {
        if(argc!=2) throw std::runtime_error("pass retail graphics.big");
        struct Case {uint32_t id;size_t bones;const char* last;uint32_t crc;
                     size_t skinned;uint32_t skinCrc;uint32_t rawCrc;};
        // Independent FableTLC mesh_rw.clone_skeleton oracle: CRC32 of each
        // bone's global ID, NUL-terminated name, parent and normalized IBM.
        // Raw CRC concatenates clone_skeleton's indices/names/bones/TRS/IBMs
        // without normalization or padding changes (independent Python oracle).
        constexpr std::array<Case,5> cases={{{4521,27,"EYE_SET_L",0x12e44699u,1131,0x0a120a63u,0x23ca606au},
            {5121,28,"EYE_SET_R",0xd8ebec00u,1012,0xe8945f52u,0xf62e93d9u},
            {5150,28,"EYE_SET_R",0x480a3c2fu,846,0x9a81b8a7u,0x9d6a7aaau},
            {3985,17,"COLD_BREATH",0x60d20858u,757,0x6398ee17u,0xcaf34886u},
            {5110,27,"EYE_SET_R",0x22d4bb4eu,822,0xfc4567b6u,0x8bc2ee81u}}};
        for(const auto& expected:cases) {
            const auto geometry=forge::meshpreview::readLod0(
                std::filesystem::path(argv[1]),expected.id);
            require(geometry.boneCount==expected.bones &&
                    geometry.bones.size()==expected.bones &&
                    geometry.bones.front().name=="Scene Root" &&
                    geometry.bones.back().name==expected.last,
                    "retail head skeleton count or names");
            uint32_t crc=0xffffffffu;
            const auto byte=[&](uint8_t value) {
                crc^=value;
                for(int bit=0;bit<8;++bit)
                    crc=(crc>>1)^((crc&1)?0xedb88320u:0);
            };
            const auto u32=[&](uint32_t value) {
                for(unsigned shift=0;shift<32;shift+=8) byte(uint8_t(value>>shift));
            };
            for(const auto& bone:geometry.bones) {
                byte(uint8_t(bone.globalId));byte(uint8_t(bone.globalId>>8));
                for(unsigned char ch:bone.name) byte(ch);
                byte(0);
                u32(uint32_t(bone.parent));
                for(float value:bone.inverseBind) u32(std::bit_cast<uint32_t>(value));
            }
            crc=~crc;
            require(geometry.skeletonBlocks.has_value(),"missing raw donor skeleton blocks");
            const auto& raw=*geometry.skeletonBlocks;
            require(raw.indices.size()==expected.bones*2 && raw.bones.size()==expected.bones*60 &&
                    raw.keyframes.size()==expected.bones*48 && raw.inverseBinds.size()==expected.bones*64,
                    "raw donor skeleton span lengths");
            uint32_t rawCrc=0xffffffffu;
            for(const auto* block:{&raw.indices,&raw.names,&raw.bones,&raw.keyframes,&raw.inverseBinds})
                for(uint8_t value:*block) {
                    rawCrc^=value;
                    for(int bit=0;bit<8;++bit) rawCrc=(rawCrc>>1)^((rawCrc&1)?0xedb88320u:0);
                }
            require(~rawCrc==expected.rawCrc,"raw donor skeleton differs from Python clone_skeleton");
            if(crc!=expected.crc)
                std::cerr << "mesh " << expected.id << " CRC " << std::hex << crc
                          << " expected " << expected.crc << std::dec << '\n';
            require(crc==expected.crc,"retail head skeleton differs from Python oracle");
            uint32_t skinCrc=0xffffffffu;
            size_t skinCount=0;
            for(const auto& vertex:geometry.vertices) if(vertex.skinned) {
                ++skinCount;
                for(size_t k=0;k<4;++k) {
                    const uint16_t joint=vertex.joints[k];
                    const uint8_t weight=uint8_t(std::lround(vertex.weights[k]*255.0f));
                    for(uint8_t value:{uint8_t(joint),uint8_t(joint>>8),weight}) {
                        skinCrc^=value;
                        for(int bit=0;bit<8;++bit)
                            skinCrc=(skinCrc>>1)^((skinCrc&1)?0xedb88320u:0);
                    }
                }
            }
            skinCrc=~skinCrc;
            if(skinCrc!=expected.skinCrc)
                std::cerr << "mesh " << expected.id << " skin CRC " << std::hex
                          << skinCrc << " expected " << expected.skinCrc << std::dec << '\n';
            require(skinCount==expected.skinned && skinCrc==expected.skinCrc,
                    "retail head skin differs from Python oracle");
            std::cout << "mesh " << expected.id << ": " << geometry.bones.size()
                      << " bones, " << skinCount << " skinned vertices, CRC "
                      << std::hex << crc << '/' << skinCrc << std::dec << '\n';
        }
        return 0;
    } catch(const std::exception& ex) {
        std::cerr << "mesh skeleton: " << ex.what() << '\n';
        return 1;
    }
}
