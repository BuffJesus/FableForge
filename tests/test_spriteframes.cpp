#include "texturebrowse.hpp"
#include "forge/terraintex.hpp"
#include "forge/big.hpp"
#include "forge/temporarydirectory.hpp"
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <iostream>

using namespace albion;
#define CHECK(x) do { if(!(x)) { std::cerr << "line " << __LINE__ << ": " #x "\n"; std::exit(1); } } while(false)
static void u16(std::vector<uint8_t>& b,size_t p,uint16_t x) { b[p]=uint8_t(x); b[p+1]=uint8_t(x>>8); }
static void u32(std::vector<uint8_t>& b,size_t p,uint32_t x) { for(int n=0;n<4;++n)b[p+n]=uint8_t(x>>(n*8)); }
static std::vector<uint8_t> header(uint16_t aw,uint16_t ah,uint16_t fw,uint16_t fh,uint16_t frames,uint8_t mips=1) {
    std::vector<uint8_t> b(34); u16(b,0,aw);u16(b,2,ah);u16(b,6,fw);u16(b,8,fh);u16(b,10,frames);
    u32(b,12,forge::terraintex::kFormatARGB);b[17]=mips;u32(b,20,aw*ah*4);
    std::copy_n(forge::terraintex::pixelFormatTail(forge::terraintex::kFormatARGB),6,b.data()+28);return b;
}
static void pixel(std::vector<uint8_t>& b,size_t n,uint8_t red,uint8_t green=0,uint8_t blue=0) {
    b[n*4]=blue;b[n*4+1]=green;b[n*4+2]=red;b[n*4+3]=255;
}
static void textureLookup() {
    forge::big::File file;
    auto& bank=file.addBank("GBANK_MAIN_PC",122);
    auto add=[&](uint32_t id,const std::string& name,uint8_t red) {
        forge::big::Entry entry;entry.id=id;entry.name=name;entry.subHeader=header(2,2,2,2,1);
        entry.data.resize(16);for(size_t n=0;n<4;++n)pixel(entry.data,n,red);
        bank.entries.push_back(std::move(entry));
    };
    add(1,"[dev/Exact.tga]",10);
    add(2,"Exact",20);
    add(3,"[dev/a/Shared.tga]",30);
    add(4,"[dev/b/Shared.tga]",40);
    add(5,"[dev/Unique.tga]",50);
    add(0xffffffffu,"MaxId",60);
    terrainexport::Image image;std::string error;
    CHECK(texbrowse::decodeTexture(file,"Exact",image,error));CHECK(image.rgba[0]==20);
    CHECK(texbrowse::decodeTexture(file,"[dev/Exact.tga]",image,error));CHECK(image.rgba[0]==10);
    CHECK(texbrowse::decodeTexture(file,"Unique",image,error));CHECK(image.rgba[0]==50);
    CHECK(texbrowse::decodeTexture(file,"5",image,error));CHECK(image.rgba[0]==50);
    CHECK(!texbrowse::decodeTexture(file,"Shared",image,error));CHECK(error.find("ambiguous")!=std::string::npos);
    CHECK(!texbrowse::decodeTexture(file,"4294967296",image,error));
    CHECK(!texbrowse::decodeTexture(file,"18446744073709551616",image,error));
    CHECK(texbrowse::decodeTexture(file,"4294967295",image,error));CHECK(image.rgba[0]==60);
    add(7,"Exact",70);
    CHECK(!texbrowse::decodeTexture(file,"Exact",image,error));CHECK(error.find("ambiguous")!=std::string::npos);
    add(5,"OtherIdFive",80);
    CHECK(!texbrowse::decodeTexture(file,"5",image,error));CHECK(error.find("ambiguous")!=std::string::npos);
    add(9,"5",90);
    CHECK(texbrowse::decodeTexture(file,"5",image,error));CHECK(image.rgba[0]==90);
}

static void textureIdRows() {
    std::vector<texbrowse::TextureRow> rows;
    auto add=[&](const char* bank,uint32_t id,const char* name) {
        texbrowse::TextureRow row;row.bank=bank;row.id=id;row.name=name;rows.push_back(row);
    };
    add("OTHER",7,"OtherBank");add("GBANK_MAIN_PC",8,"7");add("GBANK_MAIN_PC",7,"Right");
    std::string error="old";
    const auto* row=texbrowse::findTextureRowById(rows,"GBANK_MAIN_PC",7,error);
    CHECK(row && row->name=="Right" && error.empty());
    add("GBANK_MAIN_PC",7,"Duplicate");
    CHECK(!texbrowse::findTextureRowById(rows,"GBANK_MAIN_PC",7,error));CHECK(error.find("ambiguous")!=std::string::npos);
    row=texbrowse::findTextureRowById(rows,"OTHER",7,error);CHECK(row && row->name=="OtherBank" && error.empty());
    CHECK(!texbrowse::findTextureRowById(rows,"GBANK_MAIN_PC",9,error));CHECK(error.find("not in")!=std::string::npos);
}

static void textureReadFailure() {
    namespace fs=std::filesystem;
    forge::TemporaryDirectory scratch(fs::temp_directory_path(),"forge-texture-read-");
    const auto root=scratch.path(), path=root/"textures.big";
    auto write=[](const fs::path& target,const std::vector<uint8_t>& bytes) {
        fs::create_directories(target.parent_path());
        std::ofstream out(target,std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    };
    // Empty, valid PC definitions suffice for a texture-only context.
    std::vector<uint8_t> names(20,0),defs(29,0);
    u32(names,4,0xa8e36c34);u32(names,12,4);u32(defs,13,1);
    write(root/"data/CompiledDefs/names.bin",names);write(root/"data/CompiledDefs/game.bin",defs);
    forge::big::File file;auto& bank=file.addBank("GBANK_MAIN_PC",122);
    forge::big::Entry entry;entry.id=1;entry.name="READ_FAILURE";entry.subHeader=header(2,2,2,2,1);
    entry.data.resize(16);for(size_t n=0;n<4;++n)pixel(entry.data,n,42);
    bank.entries.push_back(entry);const auto good=file.serialize();write(path,good);
    terrainexport::Context cached,broken;std::string error;
    CHECK(cached.load(root,path,error));CHECK(broken.load(root,path,error));
    const auto* held=cached.texture(1,error);CHECK(held && held->rgba[0]==42);
    fs::resize_file(path,16);
    bool threw=false;const terrainexport::Image* result=nullptr;error.clear();
    try { result=broken.texture(1,error); }
    catch(const std::exception& e) { threw=true;std::cerr << "texture read threw: " << e.what() << "\n"; }
    CHECK(!threw && !result);CHECK(error.find("READ_FAILURE")!=std::string::npos);
    CHECK(!broken.texture(1,error));
    CHECK(cached.texture(1,error)==held && held->rgba[0]==42);
    write(path,good);CHECK(broken.load(root,path,error));
    const auto* restored=broken.texture(1,error);CHECK(restored && restored->rgba[0]==42);
    terrainexport::Context missing;CHECK(missing.load(root,path,error));fs::remove(path);error.clear();
    CHECK(!missing.texture(1,error));CHECK(error.find("READ_FAILURE")!=std::string::npos);
    entry.name="DUPLICATE_TWO";bank.entries.push_back(entry);
    entry.name="DUPLICATE_THREE";bank.entries.push_back(entry);
    entry.id=2;entry.name="UNIQUE";bank.entries.push_back(entry);
    write(path,file.serialize());
    terrainexport::Context duplicate;CHECK(duplicate.load(root,path,error));
    error.clear();CHECK(!duplicate.texture(1,error));CHECK(error.find("ambiguous")!=std::string::npos);
    std::string textureName;forge::terraintex::TextureInfo info;
    CHECK(!duplicate.textureInfo(1,textureName,info));
    const auto* unique=duplicate.texture(2,error);CHECK(unique && unique->rgba[0]==42);
    CHECK(duplicate.textureInfo(2,textureName,info));CHECK(textureName=="UNIQUE");

}

int main() {
    textureLookup();
    textureIdRows();
    textureReadFailure();
    texbrowse::SpriteTexture out; std::string error;
    // Single authored 2x1 frame cropped from 4x2 allocation padding.
    auto h=header(4,2,2,1,1);std::vector<uint8_t> bytes(32,211);pixel(bytes,0,10,20,30);pixel(bytes,1,40,50,60);
    CHECK(texbrowse::decodeSpriteFrames(h,bytes,out,error));CHECK(out.frames==1&&out.image.width==2&&out.image.height==1);
    CHECK(out.image.rgba==std::vector<uint8_t>({10,20,30,255,40,50,60,255}));
    // Four frames in a row-major 2x2 sheet become a vertical atlas, not one giant sprite.
    h=header(4,4,2,2,4);bytes.assign(64,0);
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)pixel(bytes,y*4+x,uint8_t(10+20*(y/2*2+x/2)));
    CHECK(texbrowse::decodeSpriteFrames(h,bytes,out,error));CHECK(out.frames==4&&out.image.width==2&&out.image.height==8);
    for(size_t f=0;f<4;++f)for(size_t n=0;n<4;++n)CHECK(out.image.rgba[(f*4+n)*4]==10+20*f);
    // Repeated whole mip chains: mip1 must not be mistaken for frame1 mip0.
    h=header(4,2,2,1,2,2);bytes.assign(80,0);
    for(size_t f=0;f<2;++f) { for(size_t n=0;n<8;++n)pixel(bytes,f*10+n,uint8_t(30+f*90)); for(size_t n=8;n<10;++n)pixel(bytes,f*10+n,244); }
    CHECK(texbrowse::decodeSpriteFrames(h,bytes,out,error));CHECK(out.frames==2&&out.image.height==2);
    CHECK(out.image.rgba[0]==30&&out.image.rgba[8]==120);
    // Non-power-of-two allocation uses native rounded stride but crops authored pixels.
    h=header(3,2,3,2,1);u32(h,20,32);bytes.assign(32,0);
    for(size_t n=0;n<8;++n)pixel(bytes,n,uint8_t(n+1));
    CHECK(texbrowse::decodeSpriteFrames(h,bytes,out,error));CHECK(out.image.rgba[12]==5);
    // A compressed stored-chunk sheet is reconstructable through decodeMip0.
    h=header(4,4,2,2,4);u32(h,24,66);bytes.assign(66,0);
    for(size_t n=0;n<16;++n) {bytes[2+n*4+2]=uint8_t(n);bytes[2+n*4+3]=255;}
    CHECK(texbrowse::decodeSpriteFrames(h,bytes,out,error));CHECK(out.image.rgba[0]==0&&out.image.rgba[16]==2&&out.image.rgba[32]==8);
    // Missing array frames cannot silently repeat frame0; failed output is empty.
    h=header(2,2,2,2,3);bytes.assign(16,0);
    CHECK(!texbrowse::decodeSpriteFrames(h,bytes,out,error));CHECK(!error.empty()&&out.frames==0&&out.image.rgba.empty());
    // Compressed independent arrays have no verified per-frame byte offsets.
    h=header(2,2,2,2,2);u32(h,24,18);bytes.assign(36,0);
    CHECK(!texbrowse::decodeSpriteFrames(h,bytes,out,error));CHECK(error.find("Compressed")!=std::string::npos);
    h=header(2,2,2,2,1);bytes.assign(15,0);CHECK(!texbrowse::decodeSpriteFrames(h,bytes,out,error));
    h=header(2,2,0,2,1);bytes.assign(16,0);CHECK(!texbrowse::decodeSpriteFrames(h,bytes,out,error));
    h=header(2,2,2,2,65535);CHECK(!texbrowse::decodeSpriteFrames(h,bytes,out,error));
    h=header(4097,2049,1,1,1);CHECK(!texbrowse::decodeSpriteFrames(h,{},out,error));CHECK(error.find("dimensions")!=std::string::npos);
    h=header(2,2,2,2,1);u16(h,4,2);CHECK(!texbrowse::decodeSpriteFrames(h,bytes,out,error));
    CHECK(!texbrowse::decodeSpriteFrames({},bytes,out,error));
    std::cout << "sprite frame decoding passed\n";
}
