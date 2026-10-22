#include "textureproject.hpp"
#include "texturefileio.hpp"
#include "texturebrowse.hpp"
#include "modpack.hpp"
#include "pendingbanks.hpp"
#include "resolvedpath.hpp"
#include "forge/big.hpp"
#include "forge/modorder.hpp"
#include "forge/terraintex.hpp"
#include "forge/texturewrite.hpp"
#include "forge/temporarydirectory.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <fstream>
#include <set>
#include <sstream>
#include <chrono>
#include <numeric>
#include <iomanip>

namespace albion::textureproject {
namespace fs = std::filesystem;
using Json = nlohmann::json;
namespace {
void require(bool ok, const std::string& why) { if (!ok) throw std::runtime_error(why); }
void tick(Progress* p) { if (p) { require(!p->cancel, "Cancelled. No texture changes were applied."); ++p->done; } }
std::string hash(const std::vector<uint8_t>& bytes) {
    return forge::modorder::sha256OfBytes(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}
std::string entryHash(const forge::big::File& file, const forge::big::Entry& entry) {
    return hash(entry.subHeader) + hash(file.entryData(entry));
}
std::string pixelHash(int w, int h, const std::vector<uint8_t>& pixels) {
    return std::to_string(w)+"x"+std::to_string(h)+":"+hash(pixels);
}
Json textureMetadata(const forge::big::Entry& entry, const forge::terraintex::TextureValidation& valid) {
    Json result={{"entryType",entry.type},{"valid",valid.ok},
                 {"errors",valid.errors},{"warnings",valid.warnings}};
    forge::terraintex::TextureInfo i;
    std::string error;
    if (!forge::terraintex::parseTextureInfo(entry.subHeader.data(),entry.subHeader.size(),i,error)) {
        result["headerError"]=error;
        return result;
    }
    const auto format=i.pixelFormat & 255;
    const char* name=format==1?"ARGB8888":format==31?"DXT1":format==32?"DXT3":
                     format==35?"DXT5":format==24?"V8U8":"unknown";
    result["pixelFormat"]=i.pixelFormat;
    result["pixelFormatName"]=name;
    result["allocation"]={{"width",i.allocWidth},{"height",i.allocHeight},{"depth",i.depth}};
    result["storedSurface"]={{"width",std::bit_ceil(unsigned(i.allocWidth))},{"height",std::bit_ceil(unsigned(i.allocHeight))}};
    result["imageLayout"]=format==24?"signed U/V: R=U+128, G=V+128, B=128, A=255":entry.type==4?"vertical strip of ordered volume slices":entry.type==5?"sprite atlas; row-major frame cells":entry.type==1?"vertical strip of full allocated frame surfaces, including padding":"single surface";
    result["frames"]={{"width",i.frameWidth},{"height",i.frameHeight},{"count",i.frameCount}};
    result["mipLevels"]=i.mipLevels;
    result["transparency"]=i.transparency;
    result["flags"]=i.flags;
    result["padding"]=i.padding;
    result["frameDataSize"]=i.frameDataSize;
    result["mipSize0"]=i.mipSize0;
    result["pixelFormatInit"]=std::vector<uint8_t>(std::begin(i.tail),std::end(i.tail));
    return result;
}
std::string unsupported(const forge::big::Entry& e, const forge::terraintex::TextureInfo& i, const std::vector<uint8_t>& payload) {
    if (e.type != 0 && e.type != 1 && e.type != 2 && e.type != 4 && e.type != 5) return "special texture type " + std::to_string(e.type);
    const bool sharedFrames = forge::terraintex::isSharedFrameSurface(e.type, e.subHeader, payload);
    if ((e.type!=5 && e.type!=1 && i.frameCount != 1 && !sharedFrames) || (e.type!=4 && i.depth != 0)) return "animated or volume texture";
    if (!i.allocWidth || !i.allocHeight || (e.type!=1 && ((i.allocWidth & (i.allocWidth-1)) || (i.allocHeight & (i.allocHeight-1)))))
        return "non-power-of-two allocation";
    const auto f = i.pixelFormat & 255;
    if (f != 1 && f != 31 && f != 32 && f != 35 && f != 24) return "unsupported pixel format";
    if(f==24 && (e.type!=0 && e.type!=2)) return "signed U/V editing requires a single surface";
    if ((i.transparency > 1 && !(f == 31 && i.transparency == 2) && !(f == 32 && i.transparency == 4) && !(f == 35 && i.transparency == 3) &&
         !(f == 1 && (i.transparency == 2 || i.transparency == 4))) ||
        (f == 31 && i.transparency == 1)) return "unsupported transparency mode";
    return {};
}
terrainexport::Image decode(const forge::big::Entry& e, const std::vector<uint8_t>& payload) {
    if(e.type==4) {
        const auto valid=forge::terraintex::validateVolume(e.subHeader,payload);
        require(valid.ok,valid.errors.empty()?"Invalid volume":valid.errors.front());
        const auto& i=valid.info;const size_t stride=payload.size()/i.depth;
        terrainexport::Image strip;strip.width=i.allocWidth;strip.height=i.allocHeight*i.depth;
        auto single=e;single.type=0;single.subHeader[4]=single.subHeader[5]=0;
        for(unsigned k=0;k<4;++k) single.subHeader[20+k]=uint8_t(stride>>(8*k));
        for(unsigned z=0;z<i.depth;++z) {
            const auto image=decode(single,std::vector<uint8_t>(payload.begin()+z*stride,payload.begin()+(z+1)*stride));
            strip.rgba.insert(strip.rgba.end(),image.rgba.begin(),image.rgba.end());
        }
        return strip;
    }
    if(e.type==1) {
        const auto valid=forge::terraintex::validateFrameSequence(e.subHeader,payload);
        require(valid.ok,valid.errors.empty()?"Invalid frame sequence":valid.errors.front());
        const auto& info=valid.info;const size_t stride=std::accumulate(valid.mipRawSizes.begin(),valid.mipRawSizes.end(),size_t(0));
        terrainexport::Image strip;strip.width=std::bit_ceil(unsigned(info.allocWidth));
        const auto height=std::bit_ceil(unsigned(info.allocHeight));strip.height=height*info.frameCount;
        strip.rgba.reserve(size_t(strip.width)*strip.height*4);
        auto single=e;single.type=0;single.subHeader[10]=1;single.subHeader[11]=0;
        for(unsigned frame=0;frame<info.frameCount;++frame) {
            const auto image=decode(single,std::vector<uint8_t>(payload.begin()+frame*stride,payload.begin()+(frame+1)*stride));
            strip.rgba.insert(strip.rgba.end(),image.rgba.begin(),image.rgba.end());
        }
        return strip;
    }
    const auto mip = forge::terraintex::decodeMip0(e.subHeader, payload);
    require(mip.ok, mip.error);
    terrainexport::Image image; image.width=mip.width; image.height=mip.height;
    using F = forge::terraintex::PreviewFormat;
    if (mip.format == F::BC1) image.rgba=terrainexport::decodeBc1ToRgba(mip.bytes.data(),mip.width,mip.height);
    else if (mip.format == F::BC2) image.rgba=terrainexport::decodeBc2ToRgba(mip.bytes.data(),mip.width,mip.height);
    else if (mip.format == F::BC3) image.rgba=terrainexport::decodeBc3ToRgba(mip.bytes.data(),mip.width,mip.height);
    else if (mip.format == F::SignedUV) image.rgba=terrainexport::signedUvToRgba(mip.bytes.data(),mip.width,mip.height);
    else image.rgba=terrainexport::bgra8ToRgba(mip.bytes.data(),mip.width,mip.height);
    return image;
}
bool contains(std::string a, std::string b) {
    auto lower=[](unsigned char c){return char(std::tolower(c));};
    std::transform(a.begin(),a.end(),a.begin(),lower); std::transform(b.begin(),b.end(),b.begin(),lower);
    return a.find(b)!=std::string::npos;
}
std::string folderLabel(const std::string& name) {
    std::string label;
    for(unsigned char c:name) label+=std::isalnum(c)||c=='_'||c=='-'?char(c):'_';
    if(label.size()>40) label.resize(40);
    // Prefix protects Windows device names; digest avoids sanitized-name collisions.
    if(label!=name || label.empty() || label=="CON" || label=="PRN" || label=="AUX" || label=="NUL" ||
       label.starts_with("COM") || label.starts_with("LPT"))
        label="bank_"+label+"_"+forge::modorder::sha256OfBytes(name).substr(0,8);
    return label;
}
std::set<std::string> textureTokens(const forge::big::Entry& entry) {
    std::string evidence=entry.name;
    for(const auto& source:entry.devSources) evidence+=" "+source;
    std::transform(evidence.begin(),evidence.end(),evidence.begin(),[](unsigned char c){return char(std::toupper(c));});
    // Match complete tokens so HERO_GUIDE and a hero's balverine tattoo are not
    // mistaken for creature skins. These labels are navigation, never identities.
    std::set<std::string> tokens; std::string token;
    for(unsigned char c:evidence) { if(std::isalnum(c)) token+=char(c); else if(!token.empty()) {tokens.insert(token);token.clear();} }
    if(!token.empty()) tokens.insert(token);
    return tokens;
}
std::string subjectFolder(const forge::big::Entry& entry) {
    const auto tokens=textureTokens(entry);
    auto has=[&](const char* s){return tokens.contains(s);};
    if(has("HEROGUIDE") || (has("HERO") && has("GUIDE"))) return "Hero guide";
    if(has("HERO") || has("YOUNGHERO")) return "Hero";
    // Character symbol prefixes are stronger evidence than a word somewhere in
    // a developer path (a BANDITCAMP prop is not a bandit character texture).
    auto symbol=entry.name;
    std::transform(symbol.begin(),symbol.end(),symbol.begin(),[](unsigned char c){return char(std::toupper(c));});
    if(symbol.starts_with("TEXTURE_APPRENTICE_") || symbol.starts_with("TEXTURE_EVIL_APPRENTICE_")) return "Apprentices";
    if(symbol.starts_with("TEXTURE_BANDIT_")) return "Bandits";
    for(const auto* creature:{"BALVERINE","HOBBE","TROLL","WASP","SCORPION","NYMPH","SUMMONER","DRAGON"}) {
        if(has(creature)) {std::string label=creature;std::transform(label.begin()+1,label.end(),label.begin()+1,[](unsigned char c){return char(std::tolower(c));});return label;}
    }
    if(has("HUD") || has("GUI") || has("EDITORGUI") || has("ICON") || has("INVENTORY")) return "Interface";
    if(has("SPRITE") || has("TRAIL") || has("PARTICLE") || has("PARTICLES") || has("EFFECT") || has("EFFECTS")) return "Effects";
    if(has("TROPHY") || has("INVENTORYITEMS")) return "Items";
    if(has("WEAPON") || has("WEAPONS")) return "Weapons";
    if(has("BIPEDS") || has("VILLAGERS")) return "Characters";
    if(has("CREATURES")) return "Creatures";
    if(has("LANDSCAPE") || has("TERRAIN") || has("BUILDINGS") || has("FOLIAGE") ||
       has("WORLD") || has("REGIONS") || has("FLORA") || has("BUYABLEHOUSEFURNITURE") ||
       has("SKY") || has("WEATHER") || has("ATMOSPHERIC")) return "Environment";
    return "Uncategorized";
}
std::string typeFolder(const forge::big::Entry& entry) {
    const auto tokens=textureTokens(entry);
    auto has=[&](const char* s){return tokens.contains(s);};
    // Role precedence matters: an icon of a trophy is an icon, and a tattoo
    // containing SKIN in its symbol is still a tattoo.
    if(entry.subHeader.size()>=16 && entry.subHeader[12]==24) return "Signed vector maps";
    if(entry.type==4) return "Volume strips";
    if(entry.type==1) return "Animation strips";
    if(entry.type==5) return "Sprite atlases";
    if(entry.type==2 || has("BUMP")) return "Bump maps";
    if(has("ICON")) return "Icons";
    if(has("HUD") || has("GUI") || has("EDITORGUI") || has("INVENTORY")) return "Interface";
    if(has("SPRITE") || has("TRAIL") || has("PARTICLE") || has("PARTICLES") || has("EFFECT") || has("EFFECTS")) return "Effects";
    if(has("TROPHY")) return "Trophies";
    if(has("TATTOO")) return "Tattoos";
    if(has("HAIR") || has("BEARD") || has("MOUSTACHE")) return "Hair";
    if(has("CLOTHING") || has("ARMOUR") || has("SHIRT") || has("TROUSERS") || has("GLOVES") || has("GLOVE") || has("BOOTS")) return "Clothing";
    if(has("SKIN") || has("BODY") || has("TORSO") || has("FACE") || has("EYE")) return "Skins";
    return "Other";
}
// Recovered CTextureManager::CalculateTextureSize @ 00a698f0. This is full-chain
// pool accounting, not a prediction that every texture is resident simultaneously.
uint64_t allocation(const forge::terraintex::TextureInfo& i) {
    const auto f=i.pixelFormat & 255; const uint64_t minimum=(f==1 || f==24)?1:4, bits=f==31?4:(f==32 || f==35)?8:f==24?16:32;
    uint64_t pixels=0, w=std::bit_ceil(unsigned(i.allocWidth)), h=std::bit_ceil(unsigned(i.allocHeight));
    while (w || h) { pixels+=std::max(w,minimum)*std::max(h,minimum); if(i.mipLevels<=1) break; w>>=1; h>>=1; }
    return pixels*bits/8;
}
Json sizePlanEntry(const forge::big::Bank& bank,const forge::big::Entry& entry,
                   const forge::terraintex::TextureValidation& validation) {
    const auto& original=validation.info;
    Json row={{"bank",bank.name},{"id",entry.id},{"name",entry.name},
              {"subject",subjectFolder(entry)},{"role",typeFolder(entry)},
              {"sourcePayloadBytes",entry.length},{"scenarios",Json::array()}};
    for(unsigned scale:{1u,2u,4u}) {
        auto info=original;
        unsigned actual=scale;
        // Preserve aspect ratio, and never shrink an existing oversized source.
        while(actual>1 && (unsigned(info.allocWidth)*actual>2048 || unsigned(info.allocHeight)*actual>2048 ||
            ((entry.type==1 || entry.type==4) && uint64_t(std::bit_ceil(unsigned(info.allocWidth)))*std::bit_ceil(unsigned(info.allocHeight))*actual*actual*(entry.type==4?info.depth:info.frameCount)>64ull*1024*1024))) actual/=2;
        const bool resize=actual>1;
        uint64_t bytes=std::accumulate(validation.mipRawSizes.begin(),validation.mipRawSizes.end(),uint64_t(0));
        if(resize) {
            info.allocWidth*=actual; info.allocHeight*=actual;
            info.frameWidth*=actual; info.frameHeight*=actual;
            info.mipLevels=entry.type==4?1:(info.pixelFormat&255)==24?std::max(1u,unsigned(info.mipLevels)):forge::texturewrite::fullMipCount(std::bit_ceil(unsigned(info.allocWidth)),std::bit_ceil(unsigned(info.allocHeight)),info.pixelFormat&255);
            bytes=0;
            for(unsigned level=0;level<info.mipLevels;++level)
                bytes+=forge::texturewrite::mipRawLength(info.pixelFormat&255,std::bit_ceil(unsigned(info.allocWidth))>>level,std::bit_ceil(unsigned(info.allocHeight))>>level)*(entry.type==4?info.depth:1);
        }
        row["scenarios"].push_back({{"requestedScale",scale},{"appliedScale",resize?actual:1u},
            {"width",info.allocWidth},{"height",info.allocHeight},{"frameWidth",info.frameWidth},{"frameHeight",info.frameHeight},{"frameCount",info.frameCount},{"mips",info.mipLevels},
            {"nativeMipBytes",bytes*(entry.type==1?info.frameCount:1)},{"engineAllocationBytes",allocation(info)*(entry.type==4?info.depth:entry.type==1?info.frameCount:1)},
            {"status",scale==1?"baseline":actual==1?"dimension limit retained":"resize"}});
    }
    return row;
}
void writeSizePlan(const fs::path& folder,const fs::path& archive,const Json& entries,size_t skipped) {
    Json plan={{"kind","FableForge texture size plan"},{"schema",1},{"maxDimension",2048},
        {"sourceArchiveBytes",fs::file_size(archive)},{"supportedEntries",entries.size()},{"excludedEntries",skipped},
        {"notes",{"Native mip bytes retain DXT compression but exclude bank LZO compression, archive metadata and alignment.",
                   "These are planning estimates, not a final archive size or measured RAM/VRAM usage. Review edited images for the exact bank forecast.",
                   "Scales preserve aspect ratio and padded frame proportions, and are reduced to fit 2048. Sources already at the limit retain their original dimensions and mip chain.",
                   "Resized entries use the current PNG writer's full supported mip chain. Supplied DDS mip counts can differ.",
                   "Unsupported entries are excluded from these totals and remain listed in the project manifest. 2048 requires a compatible engine patch."}},
        {"entries",entries},{"groups",Json::array()},{"totals",Json::array()}};
    std::map<std::string,Json> groups;
    for(const auto& row:entries) {
        const auto key=row["bank"].get<std::string>()+" / "+row["subject"].get<std::string>()+" / "+row["role"].get<std::string>();
        if(!groups.contains(key)) groups[key]={{"label",key},{"entries",0},{"sourcePayloadBytes",0},{"scenarios",Json::array()}};
        auto& group=groups[key];group["entries"]=group["entries"].get<size_t>()+1;
        group["sourcePayloadBytes"]=group["sourcePayloadBytes"].get<uint64_t>()+row["sourcePayloadBytes"].get<uint64_t>();
        if(group["scenarios"].empty()) for(unsigned scale:{1u,2u,4u}) group["scenarios"].push_back({{"requestedScale",scale},{"nativeMipBytes",0},{"engineAllocationBytes",0},{"resized",0}});
        for(size_t n=0;n<3;++n) {
            auto& total=group["scenarios"][n];const auto& scenario=row["scenarios"][n];
            for(const char* field:{"nativeMipBytes","engineAllocationBytes"}) total[field]=total[field].get<uint64_t>()+scenario[field].get<uint64_t>();
            total["resized"]=total["resized"].get<size_t>()+(scenario["appliedScale"].get<unsigned>()>1);
        }
    }
    for(const auto& [key,group]:groups) plan["groups"].push_back(group);
    for(size_t n=0;n<3;++n) {
        Json total={{"requestedScale",n==0?1:n==1?2:4},{"nativeMipBytes",0},{"engineAllocationBytes",0},{"resized",0}};
        for(const auto& [key,group]:groups) for(const char* field:{"nativeMipBytes","engineAllocationBytes","resized"})
            total[field]=total[field].get<uint64_t>()+group["scenarios"][n][field].get<uint64_t>();
        plan["totals"].push_back(total);
    }
    std::ofstream output(folder/"texture-size-plan.json");output<<plan.dump(2)<<'\n';output.close();require(bool(output),"Could not write texture size plan");
    std::ofstream summary(folder/"SIZE-PLAN.txt");
    summary<<"TEXTURE UPSCALE PLANNING\n\n";
    for(const auto& note:plan["notes"]) summary<<note.get<std::string>()<<"\n";
    summary<<"\nSupported entries: "<<entries.size()<<"; excluded: "<<skipped<<"\nNative mip data in MiB (before bank LZO):\n"<<std::fixed<<std::setprecision(2);
    for(const auto& total:plan["totals"]) summary<<total["requestedScale"]<<"x: "<<double(total["nativeMipBytes"].get<uint64_t>())/1048576<<" MiB; "<<total["resized"]<<" resized\n";
    summary<<"\nBy bank / subject / role: baseline -> 2x -> 4x, native mip MiB\n";
    for(const auto& [key,group]:groups) {
        summary<<key<<" ("<<group["entries"]<<"): ";
        for(size_t n=0;n<3;++n) summary<<(n?" -> ":"")<<double(group["scenarios"][n]["nativeMipBytes"].get<uint64_t>())/1048576;
        summary<<"\n";
    }
    summary<<"\ntexture-size-plan.json contains per-texture dimensions, mip counts and estimates.\n";
    summary.close();require(bool(summary),"Could not write texture size summary");
}
uint64_t rebuiltSize(const forge::big::File& file) {
    uint64_t size=16;
    const bool retail=file.magic()=="BIGB";
    for(const auto& bank:file.banks()) for(const auto& entry:bank.entries) {
        const uint64_t align=retail && bank.blockSize?bank.blockSize:1;
        size+=(align-size%align)%align;
        size+=entry.data.empty()?entry.length:entry.data.size();
    }
    for(const auto& bank:file.banks()) {
        std::set<uint32_t> types;for(const auto& e:bank.entries) types.insert(e.type);
        size+=4+types.size()*8;
        for(const auto& e:bank.entries) {
            size+=40+e.name.size()+e.subHeader.size();
            for(const auto& source:e.devSources) size+=4+source.size();
        }
    }
    size+=4;
    for(const auto& bank:file.banks()) size+=bank.name.size()+1+20;
    return size;
}
std::string archiveRelative(const Json& manifest) {
    std::string relative;
    if(manifest.contains("targetArchive")) relative=manifest.at("targetArchive").get<std::string>();
    else {
        auto source=manifest.value("sourceArchive",std::string{});
        std::replace(source.begin(),source.end(),'\\','/');
        auto name=fs::path(source).filename().string();
        std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
        relative=name=="frontend.big"?"data/graphics/pc/frontend.big":"data/graphics/pc/textures.big";
    }
    require(relative=="data/graphics/pc/textures.big" || relative=="data/graphics/pc/frontend.big","Unsupported texture project archive destination");
    return relative;
}
bool frontendProject(const Json& manifest) {return archiveRelative(manifest)=="data/graphics/pc/frontend.big";}
struct Prepared { Result result; forge::big::File file; std::vector<uint8_t> bytes; std::string sourceHash; Json manifest; std::set<std::pair<std::string,uint32_t>> missing; };
Prepared prepare(const fs::path& archive, const fs::path& folder, Options options, Progress* progress) {
    Prepared p; auto& r=p.result;
    require(options.maxDimension==512 || options.maxDimension==2048, "Choose a 512 or 2048 pixel size limit");
    const auto manifest=folder/"texture-project.json";
    require(fs::file_size(manifest)<32*1024*1024, "Texture project manifest is too large");
    std::ifstream in(manifest); Json j; in>>j;
    p.manifest=j;
    require(archive.filename()==fs::path(archiveRelative(j)).filename(),"Project belongs to a different texture archive");
    require(j.at("schema")==1 && j.at("kind")=="FableForge texture project", "Unsupported texture project");
    const auto& entries=j.at("entries"); require(entries.is_array() && entries.size()<=100000, "Invalid project entry list");
    p.sourceHash=forge::modorder::sha256Of(archive);
    p.file=forge::big::File::open(archive); r.beforeBytes=fs::file_size(archive);
    std::string token=p.sourceHash+j.dump()+std::to_string(options.imageSize)+std::to_string(options.maxDimension)+
        ":unsigned-archive-offsets="+std::to_string(options.unsignedArchiveOffsets);
    std::set<std::pair<std::string,uint32_t>> identities;
    std::set<std::string> imagePaths;
    size_t failedEntries=0, entryNumber=0;
    std::vector<std::string> failures;
    if(progress) { progress->done=0; progress->total=entries.size(); }
    for(const auto& row:entries) {
        tick(progress);
        ++entryNumber;
        try {
        const auto bankName=row.at("bank").get<std::string>(); const auto id=row.at("id").get<uint32_t>();
        const auto name=row.at("name").get<std::string>(); const auto path=row.at("file").get<std::string>();
        require(identities.emplace(bankName,id).second, "Duplicate texture identity in project");
        require(imagePaths.insert(path).second, "Duplicate image path in project");
        const fs::path relative(path);
        require(!relative.empty() && !relative.is_absolute() && !relative.has_root_name() && *relative.begin()=="images" &&
                relative.filename().extension()==".png" && path.find(':')==std::string::npos, "Invalid project image path");
        for(const auto& component:relative) require(component!=".." && component!=".","Invalid project image path");
        auto imagePath=folder/relative;
        require(paths::resolvedPath(imagePath)==paths::resolvedPath(folder)/relative, "Project image resolves outside its expected folder");
        auto dds=imagePath;dds.replace_extension(".dds");
        if(fs::exists(dds)) {
            require(paths::resolvedPath(dds)==paths::resolvedPath(folder)/relative.parent_path()/dds.filename(),"DDS image resolves outside its expected folder");
            if(fs::exists(imagePath)) {
                const auto originalPngHash=forge::modorder::sha256Of(imagePath);token+=path+originalPngHash;
                if(originalPngHash!=row.at("pngHash").get<std::string>()) {
                    std::string error;const auto png=forge::texturewrite::loadImage(imagePath.string(),error);
                    require(!png.rgba.empty() && pixelHash(png.width,png.height,png.rgba)==row.at("pixelHash").get<std::string>(),
                            "Both PNG and DDS are edited: "+path+". Keep one edited version before reviewing.");
                }
            } else token+="png-absent:"+path;
            imagePath=dds;
            r.details.push_back("DDS source: "+fs::relative(dds,folder).generic_string());
        }
        auto* bank=p.file.findBank(bankName); require(bank!=nullptr,"Missing subbank: "+bankName);
        forge::big::Entry* target=nullptr;
        for(auto& entry:bank->entries) if(entry.id==id) { require(!target,"Ambiguous texture ID in "+bankName); target=&entry; }
        require(target && target->name==name, "Texture identity changed: "+bankName+" / "+std::to_string(id));
        require(entryHash(p.file,*target)==row.at("sourceHash").get<std::string>(), "Source texture changed since export: "+name+". Export a fresh project.");
        if(!fs::exists(imagePath)) { ++r.missing; p.missing.emplace(bankName,id); token+="missing:"+path; r.details.push_back("Missing, no update: "+name); continue; }
        const auto pngHash=forge::modorder::sha256Of(imagePath); token+=imagePath.filename().string()+path+pngHash;
        if(pngHash==row.at("pngHash").get<std::string>()) { ++r.unchanged; continue; }
        std::string error; auto image=forge::texturewrite::loadImage(imagePath.string(),error);
        if(image.rgba.empty()) {
            const auto at=error.find(imagePath.string());
            if(at!=std::string::npos) error.replace(at,imagePath.string().size(),fs::relative(imagePath,folder).generic_string());
            throw std::runtime_error(error);
        }
        if(!image.fromDds && pixelHash(image.width,image.height,image.rgba)==row.at("pixelHash").get<std::string>()) { ++r.unchanged; continue; }
        forge::terraintex::TextureInfo original;
        require(forge::terraintex::parseTextureInfo(target->subHeader.data(),target->subHeader.size(),original,error),error);
        const auto reason=unsupported(*target,original,p.file.entryData(*target)); require(reason.empty(),name+": "+reason);
        forge::terraintex::ImportRequest request; request.png=imagePath; request.entryName=name;
        request.exactIdentity=true; request.frameStrip=target->type==1; request.volumeStrip=target->type==4; request.signedUv=(original.pixelFormat&255)==24; request.entryId=id; request.subBank=bankName; request.format.clear();
        request.useImageSize=options.imageSize; request.maxDimension=options.maxDimension;
        auto applied=forge::terraintex::applyImage(p.file,request);
        if(!applied.output.empty()) r.details.push_back(path+": "+applied.output);
        require(applied.ok,path+": "+(applied.validation.errors.empty()?"Texture encoding failed":applied.validation.errors.front()));
        require(forge::modorder::sha256Of(imagePath)==pngHash,"Image changed while encoding: "+path+". Review again.");
        const auto& info=applied.validation.info;
        ++r.changed;
        r.details.push_back(name+": "+std::to_string(original.allocWidth)+"x"+std::to_string(original.allocHeight)+" -> "+
            std::to_string(info.allocWidth)+"x"+std::to_string(info.allocHeight)+" (ID "+std::to_string(id)+", "+bankName+")");
        if(!options.imageSize && (image.width!=int(std::bit_ceil(unsigned(info.allocWidth))) || image.height!=int(std::bit_ceil(unsigned(info.allocHeight))*(target->type==4?info.depth:target->type==1?info.frameCount:1))))
            r.details.push_back("Resampled to original size: "+path+". Choose Image size to keep the edited dimensions.");
        r.allocationBytes+=allocation(info)*(target->type==4?info.depth:target->type==1?info.frameCount:1);
        for(auto bytes:applied.validation.mipRawSizes) r.storedMipBytes+=bytes*(target->type==1?info.frameCount:1);
        } catch(const std::bad_alloc&) { throw; }
        catch(const std::exception& error) {
            ++failedEntries;
            if(failures.size()<20) failures.push_back("Entry "+std::to_string(entryNumber)+": "+error.what());
        }
    }
    if(failedEntries) {
        std::string error=std::to_string(failedEntries)+(failedEntries==1?" texture entry":" texture entries")+" could not be reviewed. No changes were saved.";
        for(const auto& failure:failures) error+="\n"+failure;
        if(failedEntries>failures.size()) error+="\n"+std::to_string(failedEntries-failures.size())+" additional failures omitted.";
        throw std::runtime_error(error);
    }
    r.skipped=j.at("skipped").size();
    if(r.changed) { tick(progress); r.afterBytes=rebuiltSize(p.file); }
    else r.afterBytes=r.beforeBytes;
    require(forge::modorder::sha256Of(archive)==p.sourceHash,"Source archive changed during review. Review again.");
    if(r.afterBytes>=0x80000000ull) r.details.push_back(options.unsignedArchiveOffsets?
        "Archive requires the audited Archive4GB runtime. File size must remain below 4 GiB.":
        "Archive reaches the signed 2 GiB file boundary. Applying to the current engine is blocked.");
    r.token=forge::modorder::sha256OfBytes(token); r.ok=true; return p;
}
struct PackPlan { forge::big::File edits; Json manifest; std::string manifestHash, editsHash; };
PackPlan planPack(Prepared& p,const fs::path& pack,const fs::path& archive) {
    const auto metadata=modpack::load(pack);
    PackPlan plan;
    plan.manifestHash=forge::modorder::sha256Of(pack/modpack::kFileName);
    std::ifstream in(pack/modpack::kFileName);in>>plan.manifest;in.close();
    const bool frontend=frontendProject(p.manifest);
    const auto destination=pack/(frontend?"assets/frontend-texture-edits.big":"assets/texture-edits.big");
    if(frontend?metadata.frontendTextureEdits:metadata.textureEdits) {plan.editsHash=forge::modorder::sha256Of(destination);plan.edits=forge::big::File::open(destination);}
    else {require(!fs::exists(destination),"Untracked texture edits already exist in this pack");plan.edits.setMagic("BIGB");plan.edits.setVersion(p.file.version());}
    auto overlay=[&](forge::big::File& targetFile) {
        size_t count=0;
        for(const auto& bank:plan.edits.banks()) {
            auto* target=targetFile.findBank(bank.name);require(target!=nullptr,"Missing pack subbank: "+bank.name);
            for(const auto& e:bank.entries) {
                auto entry=std::find_if(target->entries.begin(),target->entries.end(),[&](const auto& x){return x.id==e.id && x.name==e.name;});
                require(entry!=target->entries.end(),"Pack texture no longer matches the source: "+e.name);
                *entry=e;entry->data=plan.edits.entryData(e);++count;
            }
        }
        return count;
    };
    auto current=forge::big::File::open(archive);
    p.result.beforeBytes=overlay(current)?rebuiltSize(current):fs::file_size(archive);
    auto& owners=plan.manifest[frontend?"frontendTextureProjectEntries":"textureProjectEntries"];
    if(owners.is_null()) owners=Json::object();
    require(owners.is_object(),"Invalid texture project ownership metadata");
    const auto project=p.manifest.value("projectId",std::string{});
    require(!project.empty(),"Export a fresh project to use reversible pack saves.");
    size_t updates=0;
    for(const auto& row:p.manifest.at("entries")) {
        const auto bankName=row.at("bank").get<std::string>();const auto id=row.at("id").get<uint32_t>();
        if(p.missing.contains({bankName,id})) continue; // Missing images never remove saved overrides.
        const auto* sourceBank=p.file.findBank(bankName);
        const auto source=std::find_if(sourceBank->entries.begin(),sourceBank->entries.end(),[&](const auto& e){return e.id==id;});
        const auto key=Json::array({bankName,id}).dump();
        auto* bank=plan.edits.findBank(bankName);
        forge::big::Entry* existing=nullptr;
        if(bank) for(auto& e:bank->entries) if(e.id==id || e.name==source->name) {
            require(!existing && e.id==id && e.name==source->name,"Pack contains a conflicting texture identity");existing=&e;
        }
        const bool changed=!source->data.empty();
        const bool owned=owners.contains(key) && owners.at(key).at("project")==project;
        if(!changed && !owned) continue;
        require(!owners.contains(key) || owned,"Texture is managed by another project: "+source->name);
        if(owned) require(existing && entryHash(plan.edits,*existing)==owners.at(key).at("hash").get<std::string>(),
                          "Saved texture changed outside this project: "+source->name+". Export a new project or resolve the pack edit first.");
        else require(!existing,"Pack already contains an unmanaged override: "+source->name+". Choose another pack.");
        if(changed) {
            const auto fingerprint=entryHash(p.file,*source);
            if(!existing || entryHash(plan.edits,*existing)!=fingerprint) ++updates;
            if(!bank) bank=&plan.edits.addBank(bankName,sourceBank->id);
            if(existing) *existing=*source; else bank->entries.push_back(*source);
            owners[key]={{"project",project},{"hash",fingerprint}};
        } else {
            std::erase_if(bank->entries,[&](const auto& e){return e.id==id;});owners.erase(key);++p.result.removed;
            p.result.details.push_back("Remove reverted pack override: "+bankName+" / "+source->name);
        }
    }
    p.result.unchanged+=p.result.changed-updates;
    p.result.unchanged-=p.result.removed;
    p.result.changed=updates;
    // Forecast the complete destination layer, including edits from other projects.
    p.result.afterBytes=overlay(p.file)?rebuiltSize(p.file):fs::file_size(archive);
    p.result.token=forge::modorder::sha256OfBytes(p.result.token+paths::resolvedPath(pack).string()+plan.manifestHash+plan.editsHash);
    require(forge::modorder::sha256Of(pack/modpack::kFileName)==plan.manifestHash,"Pack changed during review. Review again.");
    require(plan.editsHash.empty() ? !fs::exists(destination) : forge::modorder::sha256Of(destination)==plan.editsHash,"Pack textures changed during review. Review again.");
    require(forge::modorder::sha256Of(archive)==p.sourceHash,"Source archive changed during pack review. Review again.");
    return plan;
}
}
fs::path projectArchiveRelative(const fs::path& folder) {
    const auto file=folder/"texture-project.json";
    require(fs::file_size(file)<32*1024*1024,"Texture project manifest is too large");
    std::ifstream in(file);Json manifest;in>>manifest;
    require(manifest.at("kind")=="FableForge texture project" && manifest.at("schema")==1,"Unsupported texture project");
    return archiveRelative(manifest);
}
uint64_t rebuiltArchiveSize(const forge::big::File& file) { return rebuiltSize(file); }

std::vector<std::string> inspectSetup(const fs::path& root) {
    std::vector<std::string> notes;
    const auto exe=root/"Fable.exe";
    try {
        std::ifstream file(exe,std::ios::binary);
        auto word=[&](std::streamoff at,unsigned count)->uint32_t {
            file.seekg(at); unsigned char bytes[4]={};file.read(reinterpret_cast<char*>(bytes),count);
            require(bool(file),"Executable header is truncated");uint32_t n=0;for(unsigned i=0;i<count;++i)n|=uint32_t(bytes[i])<<(8*i);return n;
        };
        if(!file) notes.push_back("Executable: Fable.exe was not found in this installation.");
        else {
            require(word(0,2)==0x5a4d,"Executable is not a PE image");const auto pe=word(0x3c,4);
            require(word(pe,4)==0x4550,"Invalid executable PE signature");
            const auto machine=word(pe+4,2),flags=word(pe+22,2);
            notes.push_back(machine==0x14c ? ((flags&0x20)?"Executable: 32-bit, large-address-aware. Up to 4 GiB user address space on 64-bit Windows.":"Executable: 32-bit, not large-address-aware. Default user address space is 2 GiB.") : "Executable: not the expected 32-bit x86 build.");
            notes.push_back("Executable SHA256: "+forge::modorder::sha256Of(exe));
        }
        const auto dll=root/"Mods/Fable2KTextureLimit/Fable2KTextureLimit.dll";
        if(fs::is_regular_file(dll)) {
            bool enabled=false,registered=false,inMods=false;
            std::ifstream ini(root/"Mods.ini");std::string line;
            while(std::getline(ini,line)) {
                line.erase(std::remove_if(line.begin(),line.end(),[](unsigned char c){return std::isspace(c);}),line.end());
                std::transform(line.begin(),line.end(),line.begin(),[](unsigned char c){return char(std::tolower(c));});
                if(line.starts_with("[")) inMods=line=="[mods]";
                if(inMods && line.starts_with("fable2ktexturelimit\\fable2ktexturelimit.dll=")) {registered=true;enabled=line.ends_with("=1");}
            }
            notes.push_back(enabled?"2K DLL: correct EgoCore folder and enabled in Mods.ini. Runtime activation and executable compatibility are not verified.":
                registered?"2K DLL: found in the correct EgoCore folder, but disabled in Mods.ini.":"2K DLL: found in the correct EgoCore folder, but not registered in Mods.ini.");
        } else if(fs::is_regular_file(root/"Mods/Fable2KTextureLimit.dll") || fs::is_regular_file(root/"Fable2KTextureLimit.dll"))
            notes.push_back("2K DLL: found loose. EgoCore expects Mods/Fable2KTextureLimit/Fable2KTextureLimit.dll, enabled in its mod manager.");
        else notes.push_back("2K DLL: not found in the standard EgoCore location. Other loaders or patches are not detected by this check.");
        notes.push_back("The dimension patch, process address-space limit and archive file-offset limit are separate capabilities.");
    } catch(const std::exception& e) {notes.push_back(std::string("Setup inspection: ")+e.what());}
    return notes;
}

Result exportProject(const fs::path& archive,const fs::path& folder,const std::string& bankFilter,const std::string& filter,Progress* progress,bool includeOriginals,int pngCompressionLevel) {
    Result r;
    auto timed=[&](const char* stage,auto action) {const auto start=std::chrono::steady_clock::now();auto value=action();r.timings[stage]+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();return value;};
    try {
        require(pngCompressionLevel>=0 && pngCompressionLevel<=9,"PNG compression level must be between 0 and 9.");
        require(!fs::exists(folder),"Choose a new project folder; existing files will not be overwritten.");
        auto absoluteFolder=folder;
#ifdef _WIN32
        if(!folder.native().starts_with(L"\\\\?\\")) absoluteFolder=fs::absolute(folder);
#else
        absoluteFolder=fs::absolute(folder);
#endif
        const auto parent=absoluteFolder.parent_path(); fs::create_directories(parent);
        forge::TemporaryDirectory pending(parent,".tx-");
        const auto staged=pending.path();
        fs::create_directories(staged/"images");
        const auto sourceHash=timed("sourceFingerprint",[&]{return forge::modorder::sha256Of(archive);});
        const auto file=timed("bankDirectory",[&]{return forge::big::File::open(archive);});
        Json manifest={{"kind","FableForge texture project"},{"schema",1},{"projectId",forge::modorder::sha256OfBytes(sourceHash+staged.string())},{"sourceArchive",fs::absolute(archive).string()},
                       {"entries",Json::array()},{"skipped",Json::array()},{"originals",Json::array()}};
        manifest["targetArchive"]=archiveRelative(manifest);
        std::set<std::pair<std::string,uint32_t>> ids;
        Json sizePlan=Json::array();
        if(progress) { progress->done=progress->completedBefore; progress->total=progress->totalOverride; if(!progress->totalOverride) for(const auto& b:file.banks()) progress->total+=b.entries.size(); }
        for(const auto& bank:file.banks()) for(const auto& entry:bank.entries) {
            tick(progress);
            if(!bankFilter.empty() && bank.name!=bankFilter) continue;
            if(!filter.empty() && !contains(entry.name,filter) && std::to_string(entry.id)!=filter) continue;
            require(ids.emplace(bank.name,entry.id).second,"Ambiguous texture identity in "+bank.name);
            std::string label=entry.name;
            const auto slash=label.find_last_of("/\\");if(slash!=std::string::npos)label=label.substr(slash+1);
            if(label.size()>40) label.resize(40);
            for(char& c:label) if(!std::isalnum(static_cast<unsigned char>(c)) && c!='-' && c!='_') c='_';
            auto id=std::to_string(entry.id); if(id.size()<5) id.insert(0,5-id.size(),'0');
            const auto stem=folderLabel(bank.name)+"/"+subjectFolder(entry)+"/"+typeFolder(entry)+"/"+id+"_"+label;
            const auto payload=timed("entryRead",[&]{return file.entryData(entry);});
            const auto payloadHash=hash(payload), headerHash=hash(entry.subHeader);
            const auto valid=timed("validation",[&]{return entry.type==4?forge::terraintex::validateVolume(entry.subHeader,payload):entry.type==1?forge::terraintex::validateFrameSequence(entry.subHeader,payload):entry.type==5?forge::terraintex::validateSpriteAtlas(entry.subHeader,payload):forge::terraintex::validateTextureEntry(entry.subHeader,payload);});
            const auto metadata=textureMetadata(entry,valid);
            if(includeOriginals) {
                auto write=[&](const std::string& relative,const std::vector<uint8_t>& bytes) {
                    fs::create_directories((staged/relative).parent_path().make_preferred());std::ofstream out((staged/relative).make_preferred(),std::ios::binary);
                    texturefileio::write(out,bytes);out.close();require(bool(out),"Could not write original texture "+relative);
                };
                const auto raw="originals/"+stem;
                timed("originalWrite",[&]{write(raw+".bin",payload);write(raw+".info",entry.subHeader);return true;});
                manifest["originals"].push_back({{"bank",bank.name},{"bankId",bank.id},{"bankBlockSize",bank.blockSize},
                    {"id",entry.id},{"name",entry.name},{"type",entry.type},{"magic",entry.magic},{"devFileType",entry.devFileType},
                    {"devCrc",entry.devCrc},{"devSources",entry.devSources},{"payload",raw+".bin"},{"header",raw+".info"},
                    {"payloadHash",payloadHash},{"headerHash",headerHash},{"texture",metadata}});++r.originals;
            }
            std::string reason=valid.ok?unsupported(entry,valid.info,payload):(valid.errors.empty()?"Invalid texture":valid.errors.front());
            if(!reason.empty()) { ++r.skipped; manifest["skipped"].push_back({{"bank",bank.name},{"id",entry.id},{"name",entry.name},{"reason",reason},{"texture",metadata}}); r.details.push_back(entry.name+": "+reason); continue; }
            const auto image=timed("decode",[&]{return decode(entry,payload);});
            const auto relative="images/"+stem+".png";
            fs::create_directories((staged/relative).parent_path().make_preferred());
            // The whole export is already staged. Avoid a second transaction per PNG
            // (thousands of directories and unnecessarily long Windows paths).
            const auto pngBytes=timed("pngCompression",[&]{return terrainexport::encodePng(image,false,pngCompressionLevel);});
            require(!pngBytes.empty(),"PNG encoding failed for "+entry.name);
            timed("pngWrite",[&]{std::ofstream png((staged/relative).make_preferred(),std::ios::binary);
                png.write(reinterpret_cast<const char*>(pngBytes.data()),pngBytes.size());png.close();
                require(bool(png),"Could not write exported image "+relative);return true;});
            const auto& i=valid.info;
            sizePlan.push_back(sizePlanEntry(bank,entry,valid));
            manifest["entries"].push_back({{"bank",bank.name},{"id",entry.id},{"name",entry.name},{"file",relative},
                {"format",i.pixelFormat},{"width",i.allocWidth},{"height",i.allocHeight},{"imageWidth",image.width},{"imageHeight",image.height},{"mips",i.mipLevels},{"texture",metadata},
                {"sourceHash",headerHash+payloadHash},{"pngHash",hash(pngBytes)},
                {"pixelHash",pixelHash(image.width,image.height,image.rgba)}});
            ++r.exported;
        }
        require(r.exported>0 || r.originals>0,"No supported textures matched. Narrow the bank or search filter, or inspect the skipped entries.");
        require(timed("sourceFingerprint",[&]{return forge::modorder::sha256Of(archive);})==sourceHash,"Source archive changed during export. Export again.");
        manifest["exportProfileSeconds"]=r.timings;
        manifest["pngCompressionLevel"]=pngCompressionLevel;
        writeSizePlan(staged,archive,sizePlan,r.skipped);
        std::ofstream output(staged/"texture-project.json"); output<<manifest.dump(2)<<'\n'; output.close(); require(bool(output),"Could not write project manifest");
        std::ofstream guide(staged/"README.txt"); guide<<"images/ contains editable PNGs of the highest-resolution mip level.\noriginals/ (when included) preserves untouched payloads (.bin) and headers (.info), including stored mip levels.\nEntries without PNG conversion are still preserved in originals/. These files are not ordinary images.\nKeep texture-project.json: it records identity, formats, paths and hashes.\n\nImages are grouped by subbank and subject (Hero, Balverine, etc.).\nSubjects use texture symbols and original source paths; unclear entries go in Uncategorized.\nEdit PNG files in place, keeping their folder structure and filenames. Keep texture-project.json with them.\nThe manifest retains original bank, ID and symbol; folders are for navigation only.\nReview in FableForge before applying. Missing images keep the original texture.\nImage size enables resizing; Original size resamples to the existing slot.\nSigned vector maps encode U/V+128 in R/G; keep B=128 and alpha=255. These are vector data, not ordinary color images.\nVolume strips stack slices vertically in depth order. Keep slice count and order; resizing changes width/height only. Single-mip layout is retained.\nType-1 animation strips stack full allocated surfaces vertically, including padding. Preserve frame order and count.\nType-5 sprite images are complete atlases: keep frame cells in their original positions. Resize the entire atlas proportionally; frame count stays fixed.\nSkipped textures and reasons are in texture-project.json.\n"; guide.close(); require(bool(guide),"Could not write project guide");
        if(includeOriginals) {
            Json report={{"tool","Fable Texture Export"},{"version",1},{"images",r.exported},{"originalEntries",r.originals},{"notConvertedToPng",r.skipped},{"pngCompressionLevel",pngCompressionLevel},{"details",r.details},{"profileSeconds",r.timings}};
            std::ofstream out(staged/"export-report.json");out<<report.dump(2)<<'\n';out.close();require(bool(out),"Could not write export report");
        }
        if(progress) require(!progress->cancel,"Cancelled. No texture changes were applied.");
        require(!fs::exists(folder),"Project folder appeared during export; choose a new folder");
        fs::rename(staged,folder); r.ok=true;
    } catch(const std::exception& e) { r.error=e.what(); }
    return r;
}
Result review(const fs::path& archive,const fs::path& folder,Options options,Progress* progress) {
    try { return prepare(archive,folder,options,progress).result; }
    catch(const std::exception& e) { Result r; r.error=e.what(); return r; }
}
Result reviewPack(const fs::path& archive,const fs::path& folder,const fs::path& pack,Options options,Progress* progress) {
    try {auto p=prepare(archive,folder,options,progress);(void)planPack(p,pack,archive);return p.result;}
    catch(const std::exception& e) {Result r;r.error=e.what();return r;}
}
Result publishPack(const fs::path& archive,const fs::path& folder,const fs::path& pack,Options options,const std::string& expectedToken,Progress* progress) {
    try {
        auto p=prepare(archive,folder,options,progress);
        auto plan=planPack(p,pack,archive);
        require(!expectedToken.empty() && p.result.token==expectedToken,"Project changed. Review again before saving to a pack.");
        if(!p.result.changed && !p.result.removed) return p.result;
        const bool frontend=frontendProject(p.manifest);
        const auto relative=fs::path(frontend?"assets/frontend-texture-edits.big":"assets/texture-edits.big");
        const auto destination=pack/relative;
        detail::PendingBanks pending(pack,".forge-texture-pack-");
        auto bytes=plan.edits.serialize(); const auto staged=pending.prepare(relative);
        std::ofstream out(staged,std::ios::binary); texturefileio::write(out,bytes); out.close(); require(bool(out),"Could not write texture pack entries");
        const auto checked=forge::big::File::open(staged);
        size_t count=0;
        for(const auto& bank:checked.banks()) for(const auto& entry:bank.entries) {
            const auto v=entry.type==4?forge::terraintex::validateVolume(entry.subHeader,checked.entryData(entry)):entry.type==1?forge::terraintex::validateFrameSequence(entry.subHeader,checked.entryData(entry)):entry.type==5?forge::terraintex::validateSpriteAtlas(entry.subHeader,checked.entryData(entry)):
                                      forge::terraintex::validateTextureEntry(entry.subHeader,checked.entryData(entry));
            require(v.ok,"Pack texture validation failed"); ++count;
        }
        // Preserve every unrelated manifest field, including future extensions.
        plan.manifest[frontend?"frontendTextureEdits":"textureEdits"]=true;
        const auto stagedManifest=pending.prepare(modpack::kFileName);
        std::ofstream mout(stagedManifest); mout<<plan.manifest.dump(2)<<'\n'; mout.close(); require(bool(mout),"Could not write pack manifest");
        tick(progress);
        require(forge::modorder::sha256Of(pack/modpack::kFileName)==plan.manifestHash,"Pack manifest changed. Try again.");
        require(plan.editsHash.empty() ? !fs::exists(destination) : forge::modorder::sha256Of(destination)==plan.editsHash,"Pack texture edits changed. Try again.");
        std::string error; require(pending.install(false,error),error);
        p.result.details.push_back("Saved "+std::to_string(p.result.changed)+" edits and removed "+std::to_string(p.result.removed)+" reverted overrides. Use Mods to review conflicts and deploy.");
        return p.result;
    } catch(const std::exception& e) { Result r; r.error=e.what(); return r; }
}

Result apply(const fs::path& root,const fs::path& folder,Options options,const std::string& expectedToken,Progress* progress) {
    try {
        require(!backups::gameRunningIn(root),"Quit Fable before applying textures.");
        const auto relative=projectArchiveRelative(folder);
        const auto archive=root/relative;
        auto p=prepare(archive,folder,options,progress);
        require(!expectedToken.empty() && p.result.token==expectedToken,"The project, options or destination changed. Review again before applying.");
        if(!p.result.changed) return p.result;
        require(p.result.afterBytes<=0xffffffffull,"Rebuilt archive exceeds the BIG format's 32-bit file limit; reduce the texture selection or dimensions.");
        require(options.unsignedArchiveOffsets || p.result.afterBytes<0x80000000ull,"Rebuilt archive reaches the signed 2 GiB file boundary; reduce the texture selection or dimensions.");
        p.bytes=p.file.serialize();
        require(p.bytes.size()==p.result.afterBytes,"Archive size changed after review");
        detail::PendingBanks pending(root,".forge-tp-");
        const auto staged=pending.prepare(relative);
        std::ofstream out(staged,std::ios::binary); texturefileio::write(out,p.bytes); out.close(); require(bool(out),"Could not write the prepared archive");
        const auto check=forge::big::File::open(staged);
        require(check.banks().size()==p.file.banks().size(),"Prepared archive bank count changed");
        for(size_t b=0;b<check.banks().size();++b) {
            const auto& actual=check.banks()[b]; const auto& expected=p.file.banks()[b];
            require(actual.name==expected.name && actual.id==expected.id && actual.entries.size()==expected.entries.size(),"Prepared bank identity changed");
            for(size_t e=0;e<actual.entries.size();++e) {
                const auto& a=actual.entries[e]; const auto& x=expected.entries[e];
                require(a.id==x.id && a.name==x.name && a.type==x.type && a.magic==x.magic && a.devFileType==x.devFileType &&
                    a.devCrc==x.devCrc && a.devSources==x.devSources && a.subHeader==x.subHeader && check.entryData(a)==p.file.entryData(x),"Prepared archive failed preservation check");
            }
        }
        tick(progress);
        require(forge::modorder::sha256Of(archive)==p.sourceHash,"Destination changed during preparation. Review again.");
        require(!backups::gameRunningIn(root),"Fable started during preparation. Quit it before applying textures.");
        std::string error; require(pending.install(true,error),error);
        return p.result;
    } catch(const std::exception& e) { Result r; r.error=e.what(); return r; }
}
}
