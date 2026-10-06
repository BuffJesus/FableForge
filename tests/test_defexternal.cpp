#include "forge/defexternal.hpp"
#include "forge/terraintex.hpp"
#include "forge/textbig.hpp"
#include "forge/big.hpp"
#include "forge/lzo.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>

using Bytes=std::vector<uint8_t>;
static void require(bool value,const char* reason) {if(!value)throw std::runtime_error(reason);}
static void word(Bytes& bytes,uint32_t value) {
    for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(value>>(i*8)));
}
static void append(Bytes& bytes,const Bytes& more) {bytes.insert(bytes.end(),more.begin(),more.end());}
static Bytes chunk(const char* tag,const Bytes& body) {
    Bytes bytes(tag,tag+4);word(bytes,uint32_t(body.size()));append(bytes,body);return bytes;
}
static Bytes physicsImage(const Bytes& children) {
    Bytes bytes;word(bytes,0x3e3e3e3e);append(bytes,Bytes{'3','D','M','F'});
    word(bytes,1);word(bytes,0);append(bytes,chunk("3DRT",children));word(bytes,0);return bytes;
}
static void physicsChecks() {
    using forge::defexternal::reviewBigEntry;
    auto accepts=[](const Bytes& bytes) {
        return reviewBigEntry("MBANK_ALLMESHES",3,{},bytes).definitionIndependent;
    };
    Bytes material{'M',0};word(material,15360);material.push_back(1);material.resize(material.size()+24);
    Bytes extension;word(extension,1);word(extension,15361);append(material,chunk("MTLE",extension));
    Bytes map;word(map,0);append(map,Bytes{'D',0,'f','i','l','e',0});append(material,chunk("MMAP",map));
    Bytes children=chunk("MTLS",chunk("MTRL",material));
    Bytes helper;word(helper,3);helper.resize(helper.size()+60);helper.push_back(1);
    helper.resize(helper.size()+20);append(helper,Bytes{'H',0});append(children,chunk("HLPR",chunk("HDMY",helper)));
    Bytes vertices;word(vertices,1);vertices.resize(vertices.size()+32);
    Bytes primitive;word(primitive,15360);append(primitive,chunk("VERT",vertices));
    Bytes tris;word(tris,1);tris.resize(tris.size()+6);append(primitive,chunk("TRIS",tris));
    Bytes smoothing;word(smoothing,1);word(smoothing,15360);word(smoothing,0);
    append(primitive,chunk("SMTH",smoothing));append(children,chunk("PRIM",primitive));
    auto raw=physicsImage(children);const auto before=raw;
    require(accepts(raw),"complete physics local domains refused");
    require(raw==before,"physics review modified input");
    Bytes compressed;word(compressed,uint32_t(raw.size()));append(compressed,forge::lzo::compress(raw.data(),raw.size()));
    require(accepts(compressed),"compressed physics refused");
    compressed.push_back(0);require(!accepts(compressed),"physics compressed tail accepted");
    require(!reviewBigEntry("MBANK_ALLMESHES",3,Bytes(4),raw).definitionIndependent,"physics opaque info accepted");
    raw.back()=1;require(!accepts(raw),"physics unknown footer accepted");
    Bytes overflow;word(overflow,UINT32_MAX);
    require(!accepts(physicsImage(chunk("VERT",overflow))),"physics unbounded array accepted");
    smoothing.back()=1;
    require(!accepts(physicsImage(chunk("SMTH",smoothing))),"nonzero smoothing extension accepted");
    require(!accepts(physicsImage(chunk("CLTH",Bytes(8)))),"unreviewed cloth accepted");
    require(!accepts(physicsImage(chunk("BONE",Bytes(63)))),"short bone matrix accepted");
    helper[0]=4;require(!accepts(physicsImage(chunk("HDMY",helper))),"unknown helper version accepted");
    auto deep=chunk("TRFM",Bytes(48));
    for(unsigned i=0;i<18;++i)deep=chunk("HLPR",deep);
    require(!accepts(physicsImage(deep)),"unbounded physics recursion accepted");
    auto cloth=[](std::string_view source,uint8_t compiled=0) {
        Bytes body;word(body,2);append(body,Bytes{'C',0});word(body,15360);
        body.insert(body.end(),source.begin(),source.end());body.push_back(0);body.push_back(compiled);
        return physicsImage(chunk("CLTH",body));
    };
    const std::string declarations=
        "Particle name=P0 x=15360 y=0 z=0 alpha=1\n"
        "Particle name=P1 x=0 y=1 z=0 alpha=0.5\n"
        "Vertex name=V0 x=0 y=0 z=1\nTexCoord u=0 v=1\n";
    const std::string program=declarations+
        "Triangle a=P0 b=P1 c=V0 ta=0 tb=0 tc=0\n"
        "Distance a=P0 b=P1 dist=2.5 strength=0.7\n"
        "SetRenderMode bezier=0\nSetDragging enable=1 rotational=0 strength=0.25\n"
        "SetAcceleration enable=1 damping=0.5\nSetWindStrength strength=1.17\nSetGravityStrength strength=0\n";
    require(accepts(cloth(program)),"supported local cloth language refused");
    require(!accepts(cloth(program,1)),"compiled cloth silently accepted");
    require(!accepts(cloth(declarations+"Distance a=P0 b=V0 dist=1 strength=1\n")),
            "distance constraint accepted a non-particle symbol");
    require(!accepts(cloth(declarations+"Triangle a=P0 b=P1 c=MISSING ta=0 tb=0 tc=0\n")),
            "undefined cloth symbol accepted");
    require(!accepts(cloth(declarations+"Triangle a=P0 b=P1 c=V0 ta=0 tb=1 tc=0\n")),
            "cloth UV index exceeded its local table");
    require(!accepts(cloth(program+"Unknown def=15360\n")),"unknown cloth command accepted");
    require(!accepts(cloth("SetWindStrength strength=1 def=15360\n")),"unknown cloth parameter accepted");
    require(!accepts(cloth("SetWindStrength strength=1 strength=2\n")),"duplicate cloth parameter accepted");
    require(!accepts(cloth("SetWindStrength strength=nan\n")),"nonfinite cloth number accepted");
    require(!accepts(cloth("SetRenderMode bezier=2\n")),"invalid cloth boolean accepted");
    require(!accepts(cloth("SetWindStrength\n")),"missing cloth parameter accepted");
    require(!accepts(cloth(declarations+"Particle name=P0 x=0 y=0 z=0 alpha=1\n")),
            "duplicate cloth symbol accepted");
}
static void half(Bytes& b,uint16_t v) {b.push_back(uint8_t(v));b.push_back(uint8_t(v>>8));}
static Bytes meshBlock(const Bytes& raw,bool compressed=true) {
    require(raw.size()>=3,"test block too short");Bytes b;
    if(raw.size()>3) {
        if(compressed) {
            auto packed=forge::lzo::compress(raw.data(),raw.size()-3);
            half(b,uint16_t(packed.size()));append(b,packed);
        } else {half(b,0);b.insert(b.end(),raw.begin(),raw.end()-3);}
    }
    b.insert(b.end(),raw.end()-3,raw.end());return b;
}
static Bytes meshInfo(const Bytes& payload) {
    Bytes b;word(b,15360);b.resize(44);word(b,1);word(b,uint32_t(payload.size()));
    word(b,0);word(b,1);word(b,15361);return b;
}
static void meshChecks() {
    auto accepts=[](const Bytes& payload) {
        return forge::defexternal::reviewBigEntry("MBANK_ALLMESHES",1,meshInfo(payload),payload).definitionIndependent;
    };
    Bytes b{'M',0,0};b.resize(b.size()+40);
    half(b,1);half(b,0);half(b,3);half(b,0);half(b,0);
    Bytes helper;word(helper,15360);helper.resize(16);word(helper,0xffffffff);
    append(b,meshBlock(helper));append(b,Bytes{'H','P',0});
    word(b,1);word(b,1);word(b,0);word(b,0);b.resize(b.size()+5+48);
    word(b,0);append(b,Bytes{'m',0});for(int i=0;i<5;++i)word(b,15360+i);
    b.resize(b.size()+8+5);
    word(b,0);const auto repeats=b.size();word(b,1);b.resize(b.size()+20);
    word(b,1);word(b,0);word(b,3);word(b,1);word(b,3);
    const auto format=b.size();word(b,4);word(b,1);word(b,0);
    word(b,1);word(b,0);b.resize(b.size()+3);word(b,0); // static block
    b.resize(b.size()+32);word(b,12);word(b,0);
    const auto vertexBlock=b.size();append(b,meshBlock(Bytes(36)));
    append(b,meshBlock(Bytes{0,0,1,0,2,0},false));
    const auto clothCount=b.size();word(b,0);
    const auto original=b;require(accepts(b),"complete mesh storage refused");
    require(b==original,"mesh review modified input");
    auto info=meshInfo(b);auto pair=b;append(pair,b);
    require(forge::defexternal::reviewBigEntry("MBANK_ENGINE",1,info,b).definitionIndependent,"engine mesh refused");
    require(forge::defexternal::reviewBigEntry("MBANK_ALLMESHES",5,info,pair).definitionIndependent,"ghost mesh refused");
    pair.push_back(0);require(!forge::defexternal::reviewBigEntry("MBANK_ALLMESHES",5,info,pair).definitionIndependent,"ghost tail accepted");
    info.push_back(0);require(!forge::defexternal::reviewBigEntry("MBANK_ALLMESHES",1,info,b).definitionIndependent,"mesh info tail accepted");
    b[format]=99;require(!accepts(b),"unknown vertex layout accepted");b=original;
    for(size_t i=0;i<4;++i)b[repeats+i]=255;
    require(!accepts(b),"unbounded repeats accepted");b=original;
    b[49]=1;require(!accepts(b),"unreviewed volume accepted");b=original;
    b[51]=1;require(!accepts(b),"unreviewed generator accepted");b=original;
    b[2]=2;require(!accepts(b),"nonboolean mesh flag accepted");b=original;
    // Add a byte inside the declared compressed packet after its end marker.
    const size_t n=size_t(b[vertexBlock])|(size_t(b[vertexBlock+1])<<8);
    b.insert(b.begin()+vertexBlock+2+n,0);b[vertexBlock]=uint8_t(n+1);b[vertexBlock+1]=uint8_t((n+1)>>8);
    require(!accepts(b),"LZO packet suffix accepted");b=original;
    for(size_t n=0;n<b.size();++n)require(!accepts(Bytes(b.begin(),b.begin()+n)),"truncated mesh accepted");
    Bytes program;word(program,333);word(program,0);program.resize(program.size()+12);word(program,0);
    program.resize(program.size()+19); // gravity/wind, dragging, acceleration, damping
    for(int i=0;i<7;++i)word(program,0); // all geometry/index arrays
    program.resize(program.size()+5);word(program,0);word(program,0);word(program,0);
    auto withCloth=[&](const Bytes& source) {
        Bytes result(original.begin(),original.begin()+clothCount);word(result,1);
        word(result,0);word(result,0);word(result,uint32_t(source.size()));append(result,meshBlock(source));return result;
    };
    require(accepts(withCloth(program)),"complete compiled cloth refused");
    auto constrained=program;Bytes constraints;word(constraints,2);word(constraints,1);word(constraints,16);
    word(constraints,0);word(constraints,1);word(constraints,15360);word(constraints,0);
    constrained[4]=uint8_t(constraints.size());
    constrained[20]=2;constrained.insert(constrained.begin()+24,32,0);
    constrained.insert(constrained.begin()+8,constraints.begin(),constraints.end());
    require(accepts(withCloth(constrained)),"local cloth constraint refused");
    constrained[8+12]=2;
    require(!accepts(withCloth(constrained)),"cloth constraint exceeded local particle table");
    constrained[8+12]=0;constrained[8]=6;
    require(!accepts(withCloth(constrained)),"unknown constraint opcode accepted");
    program[0]=0;require(!accepts(withCloth(program)),"unknown cloth version accepted");program[0]=77;
    program.push_back(0);require(!accepts(withCloth(program)),"compiled cloth suffix accepted");program.pop_back();
    program[program.size()-12]=1;require(!accepts(withCloth(program)),"unreviewed exported cloth particle accepted");
}
static int audit(const std::filesystem::path& root) {
    nlohmann::json report{{"scope","read-only supported external BIG-entry layouts; not installation admission"},
        {"root",root.string()},{"files",nlohmann::json::array()}};
    for(const char* relative:{"data/graphics/pc/textures.big","data/graphics/pc/frontend.big",
                             "data/graphics/graphics.big","data/lang/English/text.big"}) {
        const auto file=forge::big::File::open(root/relative);
        nlohmann::json row{{"file",relative},{"accepted",0},{"refusals",nlohmann::json::array()}};
        for(const auto& bank:file.banks())for(const auto& entry:bank.entries) {
            const auto result=forge::defexternal::reviewBigEntry(bank.name,entry.type,entry.subHeader,file.entryData(entry));
            if(result.definitionIndependent) row["accepted"]=row["accepted"].get<size_t>()+1;
            else row["refusals"].push_back({{"bank",bank.name},{"type",entry.type},{"id",entry.id},
                {"name",entry.name},{"reason",result.reason}});
        }
        report["files"].push_back(std::move(row));
    }
    std::cout<<report.dump(2)<<'\n';return 0;
}
int main(int argc,char** argv) {try {
    if(argc==2)return audit(argv[1]);
    if(argc!=1)return 2;
    physicsChecks();
    meshChecks();
    using forge::defexternal::reviewBigEntry;
    require(!reviewBigEntry("UNKNOWN",0,{},{}).definitionIndependent,"empty unknown entry accepted");
    require(!reviewBigEntry("MBANK_ALLMESHES",1,Bytes(64),{}).definitionIndependent,"mesh implicitly accepted");
    Bytes texture(34);texture[0]=8;texture[2]=8;texture[6]=8;texture[8]=8;texture[10]=1;
    texture[12]=forge::terraintex::kFormatDXT5;texture[16]=3;texture[17]=4;texture[20]=64;
    std::copy_n(forge::terraintex::pixelFormatTail(forge::terraintex::kFormatDXT5),6,texture.begin()+28);
    Bytes pixels(85);const auto original=texture;
    require(reviewBigEntry("GBANK_MAIN_PC",0,texture,pixels).definitionIndependent,"valid DXT5 refused");
    require(texture==original && pixels==Bytes(85),"review modified inputs");
    texture.push_back(0);
    require(!reviewBigEntry("GBANK_MAIN_PC",0,texture,pixels).definitionIndependent,"opaque texture info tail accepted");
    require(!reviewBigEntry("GBANK_MAIN_PC",99,original,pixels).definitionIndependent,"unknown texture type accepted");
    pixels.pop_back();require(!reviewBigEntry("GBANK_MAIN_PC",0,original,pixels).definitionIndependent,"short texture accepted");
    forge::textbig::Entry text;text.content="15360";text.identifier="SAMPLE";text.tags={{0,"ANIM:TEST"}};
    auto payload=forge::textbig::encode(text);
    require(reviewBigEntry("TEXT_ENGLISH_MAIN",0,Bytes(4),payload).definitionIndependent,"text content confused with definition IDs");
    payload.push_back(0);require(!reviewBigEntry("TEXT_ENGLISH_MAIN",0,Bytes(4),payload).definitionIndependent,"text tail accepted");
    text.type=1;text.groupMembers={15360,15361};payload=forge::textbig::encode(text);
    require(reviewBigEntry("TEXT_ENGLISH_MAIN",1,{},payload).definitionIndependent,"local text IDs treated as definition IDs");
    require(!reviewBigEntry("TEXT_ENGLISH_MAIN",1,Bytes(4),payload).definitionIndependent,"opaque group info accepted");
    payload.pop_back();require(!reviewBigEntry("TEXT_ENGLISH_MAIN",1,{},payload).definitionIndependent,"truncated group accepted");
    Bytes narrators;word(narrators,2);narrators.insert(narrators.end(),{'A',0,'B',0});
    require(reviewBigEntry("TEXT_ENGLISH_MAIN",2,narrators,{}).definitionIndependent,"narrator strings refused");
    narrators.pop_back();require(!reviewBigEntry("TEXT_ENGLISH_MAIN",2,narrators,{}).definitionIndependent,"unterminated narrator accepted");
    // Minimal complete raw 3DAF root with no tracks, then its observed zero footer.
    Bytes animation;word(animation,0x3e3e3e3e);animation.insert(animation.end(),{'3','D','A','F'});
    word(animation,0);word(animation,0);animation.insert(animation.end(),{'A','N','R','T'});
    word(animation,5);animation.push_back(0);word(animation,0);word(animation,0);
    require(reviewBigEntry("MBANK_ALLMESHES",9,Bytes(24),animation).definitionIndependent,"complete animation refused");
    require(!reviewBigEntry("MBANK_ALLMESHES",9,Bytes(25),animation).definitionIndependent,"opaque animation info accepted");
    animation[20]=100;require(!reviewBigEntry("MBANK_ALLMESHES",9,Bytes(24),animation).definitionIndependent,"malformed animation accepted");
    std::cout<<"external definition-domain review checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
