#include "collection.hpp"
#include "texturebrowse.hpp"
#include "terrainexport.hpp"
#include "forge/temporarydirectory.hpp"
#include "forge/texturewrite.hpp"
#include "forge/terraintex.hpp"
#include "forge/modorder.hpp"
#include "forge/lzo.hpp"
#include "forge/stb.hpp"
#include "forge/stbbake.hpp"
#include "forge/stbinfo.hpp"
#include "forge/stbstorage.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <numeric>
#include <set>
#include <chrono>
namespace texturecollection {
namespace fs=std::filesystem;
namespace tp=albion::textureproject;
namespace te=albion::terrainexport;
using Bytes=std::vector<uint8_t>;using Json=nlohmann::json;
namespace {
void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
uint32_t u32(const Bytes& b,size_t at){need(at<=b.size() && b.size()-at>=4,"Truncated 32-bit field");return uint32_t(b[at])|(uint32_t(b[at+1])<<8)|(uint32_t(b[at+2])<<16)|(uint32_t(b[at+3])<<24);}
uint16_t u16(const Bytes& b,size_t at){need(at<=b.size() && b.size()-at>=2,"Truncated 16-bit field");return uint16_t(b[at])|(uint16_t(b[at+1])<<8);}
Bytes slice(const Bytes& b,size_t at,size_t n){need(at<=b.size() && n<=b.size()-at,"Source span exceeds payload");return {b.begin()+at,b.begin()+at+n};}
std::string digest(const Bytes& b){return forge::modorder::sha256OfBytes({reinterpret_cast<const char*>(b.data()),b.size()});}
void write(const fs::path& p,const Bytes& b){auto native=p;native.make_preferred();fs::create_directories(native.parent_path());std::ofstream f(native,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());f.close();need(bool(f),"Cannot write "+p.string());}
void json(const fs::path& p,const Json& j){auto s=j.dump(2);write(p,Bytes(s.begin(),s.end()));}
Bytes read(const fs::path& p){std::ifstream f(p,std::ios::binary|std::ios::ate);need(bool(f),"Cannot read "+p.string());auto n=f.tellg();need(n>=0 && n<=512ll*1024*1024,"Image source exceeds 512 MiB limit");Bytes b(static_cast<size_t>(n));f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),b.size());need(bool(f),"Cannot read source bytes");return b;}
bool terrainCandidate(const Bytes& b){if(b.size()<36 || b[8]!=0 || b[16]!=1 || b[21]!=1 || (u32(b,22)&31)!=3)return false;auto w=u16(b,17),h=u16(b,19);return w && h && w<=512 && h<=512;}
std::string label(std::string s){if(s.size()>60)s.resize(60);for(char& c:s)if(!std::isalnum(static_cast<unsigned char>(c)) && c!='-' && c!='_')c='_';return "item_"+s;}
struct Export {
 fs::path root,folder;tp::Progress* progress;int compression;tp::Result result;
 Json index={{"kind","Fable texture collection"},{"schema",1},{"resources",Json::array()},{"sources",Json::array()},{"notes",Json::array()}};
 void check(){if(progress)need(!progress->cancel,"Cancelled. No texture changes were applied.");}
 void tick(){check();if(progress)++progress->done;}
 void png(const std::string& path,const te::Image& image,Json meta){check();need(image.width && image.height && image.rgba.size()==size_t(image.width)*image.height*4,"Invalid decoded image dimensions");const auto b=te::encodePng(image,false,compression);write(folder/path,b);meta["png"]=path;meta["pngHash"]=digest(b);meta["width"]=image.width;meta["height"]=image.height;meta["pixelHash"]=digest(image.rgba);index["resources"].push_back(std::move(meta));++result.exported;}
 void raw(const std::string& path,const Bytes& b,Json meta){write(folder/path,b);meta["original"]=path;meta["sha256"]=digest(b);index["resources"].push_back(std::move(meta));++result.originals;}
 void issue(const std::string& what){++result.skipped;result.details.push_back(what);}
 template<class F> void source(const fs::path& path,F action){check();auto before=forge::modorder::sha256Of(path);action();need(forge::modorder::sha256Of(path)==before,"Source changed during export: "+path.string());index["sources"].push_back({{"file",fs::relative(path,root).generic_string()},{"sha256",before}});}
};
// Native mip-0 chunk framing, bounded by the validator's exact decoded length.
Bytes rawMip(const Bytes& h,const Bytes& b,const forge::terraintex::TextureValidation& v){
 if(!v.info.mipSize0)return slice(b,0,v.mipRawSizes.at(0));
 size_t pos=0,target=v.mipRawSizes.at(0)-3;Bytes out;out.reserve(target+3);
 while(out.size()<target){uint32_t n=u16(b,pos);pos+=2;if(n==65535){n=u32(b,pos);pos+=4;}if(!n){auto tail=slice(b,pos,target-out.size());out.insert(out.end(),tail.begin(),tail.end());pos+=tail.size();break;}
 auto compressed=slice(b,pos,n);auto part=forge::lzo::decompressBounded(compressed.data(),compressed.size(),target-out.size());need(!part.empty(),"Empty LZO texture chunk");out.insert(out.end(),part.begin(),part.end());pos+=n;}
 auto tail=slice(b,pos,3);out.insert(out.end(),tail.begin(),tail.end());need(out.size()==target+3,"Incorrect mip length");return out;
}
void specialTextures(Export& x,const fs::path& archive,const fs::path& project,const std::string& category){
 Json manifest;{std::ifstream f(project/"texture-project.json");f>>manifest;}
 const auto file=forge::big::File::open(archive);
 for(const auto& skipped:manifest["skipped"]){x.check();const auto* bank=file.findBank(skipped.at("bank"));need(bank,"Missing source bank");const auto id=skipped.at("id").get<uint32_t>();const auto it=std::find_if(bank->entries.begin(),bank->entries.end(),[&](const auto& e){return e.id==id;});need(it!=bank->entries.end(),"Missing source identity");
 const auto& e=*it;const auto payload=file.entryData(e);const auto v=forge::terraintex::validateTextureEntry(e.subHeader,payload);const auto stem="images/"+category+"/"+label(bank->name)+"/"+std::to_string(id)+"_"+label(e.name);
 Json meta={{"source",fs::relative(archive,x.root).generic_string()},{"bank",bank->name},{"id",id},{"name",e.name},{"importPolicy","specialized writer required"}};
 try{need(v.ok,v.errors.empty()?"Invalid texture":v.errors.front());const auto& i=v.info;
 if(i.depth){auto mip=forge::terraintex::decodeMip0(e.subHeader,payload);need(mip.ok,mip.error);size_t n=mip.bytes.size()/i.depth;for(unsigned z=0;z<i.depth;++z){te::Image im;im.width=mip.width;im.height=mip.height;const auto* p=mip.bytes.data()+z*n;im.rgba=mip.format==forge::terraintex::PreviewFormat::BC1?te::decodeBc1ToRgba(p,im.width,im.height):mip.format==forge::terraintex::PreviewFormat::BC2?te::decodeBc2ToRgba(p,im.width,im.height):mip.format==forge::terraintex::PreviewFormat::BC3?te::decodeBc3ToRgba(p,im.width,im.height):mip.format==forge::terraintex::PreviewFormat::SignedUV?te::signedUvToRgba(p,im.width,im.height):te::bgra8ToRgba(p,im.width,im.height);auto row=meta;row["slice"]=z;row["sliceCount"]=i.depth;x.png(stem+"/slice_"+std::to_string(z)+".png",im,row);}}
 else if((i.pixelFormat&255)==24){auto raw=rawMip(e.subHeader,payload,v);te::Image im;im.width=i.allocWidth;im.height=i.allocHeight;im.rgba.resize(size_t(im.width)*im.height*4);need(raw.size()==size_t(im.width)*im.height*2,"Unexpected signed bump surface");for(size_t p=0;p<raw.size()/2;++p){im.rgba[p*4]=uint8_t(raw[p*2]+128);im.rgba[p*4+1]=uint8_t(raw[p*2+1]+128);im.rgba[p*4+2]=128;im.rgba[p*4+3]=255;}meta["encoding"]="signed U,V mapped to R,G by adding 128 modulo 256; B=128; not a color texture";x.png(stem+"/signed_UV.png",im,meta);}
 else if((i.pixelFormat&255)==35){auto raw=rawMip(e.subHeader,payload,v);Bytes dds(128);auto put=[&](size_t at,uint32_t n){for(int j=0;j<4;++j)dds[at+j]=uint8_t(n>>(j*8));};put(0,0x20534444);put(4,124);put(8,0x81007);put(12,i.allocHeight);put(16,i.allocWidth);put(20,uint32_t(raw.size()));put(28,1);put(76,32);put(80,4);put(84,0x35545844);put(108,0x1000);dds.insert(dds.end(),raw.begin(),raw.end());auto temp=x.folder/".surface.dds";write(temp,dds);std::string error;auto decoded=forge::texturewrite::loadImage(temp.string(),error);fs::remove(temp);need(error.empty(),error);te::Image im;im.width=decoded.width;im.height=decoded.height;im.rgba=std::move(decoded.rgba);x.png(stem+".png",im,meta);}
 else {albion::texbrowse::SpriteTexture sprite;std::string error;if(albion::texbrowse::decodeSpriteFrames(e.subHeader,payload,sprite,error)){meta["frameCount"]=sprite.frames;meta["frameWidth"]=i.frameWidth;meta["frameHeight"]=i.frameHeight;meta["layout"]="frames stacked vertically in original order";x.png(stem+"/frames.png",sprite.image,meta);}else{auto mip=forge::terraintex::decodeMip0(e.subHeader,payload);need(mip.ok,error+"; "+mip.error);te::Image im;im.width=mip.width;im.height=mip.height;im.rgba=mip.format==forge::terraintex::PreviewFormat::BC1?te::decodeBc1ToRgba(mip.bytes.data(),im.width,im.height):mip.format==forge::terraintex::PreviewFormat::BC2?te::decodeBc2ToRgba(mip.bytes.data(),im.width,im.height):mip.format==forge::terraintex::PreviewFormat::BC3?te::decodeBc3ToRgba(mip.bytes.data(),im.width,im.height):mip.format==forge::terraintex::PreviewFormat::SignedUV?te::signedUvToRgba(mip.bytes.data(),im.width,im.height):te::bgra8ToRgba(mip.bytes.data(),im.width,im.height);meta["declaredFrames"]=i.frameCount;meta["note"]=error;meta["layout"]="stored first surface only; original payload preserved";x.png(stem+"/stored_surface.png",im,meta);x.index["notes"].push_back(e.name+": "+error);}}
 }catch(const std::exception& ex){x.check();x.issue(e.name+": "+ex.what());}
 }
}
// Static font grammar documented by EgoCore FontParser and the reconstruction's
// render_fable_static_font.py. Keep the complete raw entry and glyph metrics.
void staticFont(Export& x,const Bytes& b,const std::string& stem,Json meta){
 auto zero=std::find(b.begin(),b.end(),0);need(zero!=b.end(),"Unterminated font face");size_t p=size_t(zero-b.begin())+1;need(p+33<=b.size(),"Short static font");uint32_t w=u32(b,p+13),h=u32(b,p+17),banks=u32(b,p+29);p+=33;need(banks<=65536,"Excessive glyph banks");
 for(uint32_t n=0;n<banks;++n){auto count=u32(b,p+8);p+=12;need(count<65536,"Excessive static glyph count");p+=size_t(count)*22;need(p<=b.size(),"Truncated static glyph metrics");}
 auto size=u32(b,p);p+=4;auto tga=slice(b,p,size);need(p+size==b.size(),"Trailing static font data");write(x.folder/("originals/Fonts/"+stem+".tga"),tga);std::string error;auto decoded=forge::texturewrite::loadImage((x.folder/("originals/Fonts/"+stem+".tga")).make_preferred().string(),error);need(error.empty(),error);need(decoded.width==w && decoded.height==h,"Font atlas dimensions disagree");te::Image im;im.width=w;im.height=h;im.rgba=std::move(decoded.rgba);meta["tgaOffset"]=p;meta["importPolicy"]="font atlas and metrics must be rebuilt together";x.png("images/Fonts/"+stem+".png",im,meta);
}
void glyphPages(Export& x,const Bytes& b,const std::string& stem,Json meta){
 auto count=u32(b,0),dataStart=u32(b,8),compressedSize=u32(b,12),chunkSize=u32(b,16);need(count && count<=1000000 && chunkSize && chunkSize<=1000000,"Invalid streaming glyph counts");size_t chunks=(size_t(count)+chunkSize-1)/chunkSize,indexAt=20ull+compressedSize;need(indexAt+(chunks+1)*4<=dataStart && dataStart<=b.size(),"Invalid glyph metric directory");need(u32(b,indexAt)==0 && u32(b,indexAt+chunks*4)==compressedSize,"Glyph metric span mismatch");Bytes metrics;metrics.reserve(size_t(count)*8);
 for(size_t c=0;c<chunks;++c){auto start=u32(b,indexAt+c*4),end=u32(b,indexAt+(c+1)*4);need(end>=start && end<=compressedSize,"Invalid metric chunk range");auto part=forge::lzo::decompress(b.data()+20+start,end-start,std::min<size_t>(chunkSize,count-c*chunkSize)*8);metrics.insert(metrics.end(),part.begin(),part.end());}
 const auto adjustmentCount=u32(b,4);const size_t adjustmentAt=indexAt+(chunks+1)*4;
 need(adjustmentCount<=256 && size_t(adjustmentCount)*2<=dataStart-adjustmentAt,"Invalid streaming adjustment table");
 Json adjustments=Json::array();for(uint32_t i=0;i<adjustmentCount;++i)
  adjustments.push_back({{"index",i},{"scale",b[adjustmentAt+i*2]},{"baseline",int(int8_t(b[adjustmentAt+i*2+1]))}});
 json(x.folder/("images/Fonts/"+stem+"/glyph-adjustments.json"),{{"entries",adjustments},{"scaleDivisor",100},{"sourceOffset",adjustmentAt},{"paddingBytes",dataStart-adjustmentAt-size_t(adjustmentCount)*2}});
 Json mapping=Json::array();constexpr unsigned side=1024,cell=64,perPage=256;
 for(size_t base=0;base<count;base+=perPage){x.check();te::Image image;image.width=side;image.height=side;image.rgba.assign(side*side*4,0);bool any=false;
 for(size_t j=0;j<perPage && base+j<count;++j){size_t at=(base+j)*8;auto packed=u32(metrics,at+4);unsigned pitch=packed&0x80000000?64:32,w=std::min<unsigned>(metrics[at+2],pitch),h=std::min<unsigned>(metrics[at+3],pitch);size_t offset=packed&0x3fffff;unsigned dx=(j%16)*cell,dy=(j/16)*cell;Json row={{"metric",base+j},{"page",base/perPage},{"x",dx},{"y",dy},{"width",w},{"height",h},{"offsetX",int(int8_t(metrics[at]))},{"offsetY",int(int8_t(metrics[at+1]))},{"advance",(packed>>23)&255}};
 row["declaredWidth"]=metrics[at+2];row["declaredHeight"]=metrics[at+3];row["cellPitch"]=pitch;row["packedMetric"]=packed;row["pixelOffsetUnits"]=offset;row["pixelOffsetUnitBytes"]=1024;
 bool present=w && h && offset!=0x3fffff;row["present"]=present;mapping.push_back(row);if(!present)continue;size_t start=size_t(dataStart)+offset*1024;need(start<=b.size() && size_t(h-1)*pitch+w<=b.size()-start,"Glyph alpha block exceeds data");any=true;
 for(unsigned y=0;y<h;++y)for(unsigned xx=0;xx<w;++xx){size_t d=(size_t(dy+y)*side+dx+xx)*4;image.rgba[d]=image.rgba[d+1]=image.rgba[d+2]=255;image.rgba[d+3]=b[start+y*pitch+xx];}}
 if(any){auto row=meta;row["page"]=base/perPage;row["metricStart"]=base;row["layout"]="64px cells, 16 columns; glyph-metrics.json maps glyph indices";x.png("images/Fonts/"+stem+"/page_"+std::to_string(base/perPage)+".png",image,row);}}
 json(x.folder/("images/Fonts/"+stem+"/glyph-metrics.json"),mapping);
}
void fontMetadata(Export& x,const Bytes& b,const std::string& stem){
 auto zero=std::find(b.begin(),b.end(),0);need(zero!=b.end(),"Unterminated streaming font name");size_t offsets=size_t(zero-b.begin())+14,data=offsets+1025*4;need(data<=b.size(),"Truncated streaming font directory");need(u32(b,offsets)==0 && data+u32(b,offsets+4096)==b.size(),"Streaming glyph banks do not span metadata");Json chars=Json::array();
 for(unsigned bank=0;bank<1024;++bank){auto start=u32(b,offsets+bank*4),end=u32(b,offsets+(bank+1)*4);need(end>=start && end<=b.size()-data,"Invalid streaming font range");if(start==end)continue;auto block=slice(b,data+start,end-start);auto header=u16(block,0);need((header&32767)==block.size()-2,"Streaming font block size mismatch");auto decoded=header&32768?forge::lzo::decompressBounded(block.data()+2,block.size()-2,260):slice(block,2,block.size()-2);unsigned first=u16(decoded,0),count=u16(decoded,2);need(first+count<=64 && decoded.size()==4+count*4,"Invalid glyph bank");for(unsigned i=0;i<count;++i){auto packed=u32(decoded,4+i*4),index=packed&0xffffff;if(index)chars.push_back({{"codepoint",bank*64+first+i},{"metric",index-1},{"adjustmentIndex",packed>>24},{"packedReference",packed}});}}
 json(x.folder/("images/Fonts/"+stem+"-characters.json"),chars);
}
}
tp::Result exportInstallation(const fs::path& root,const fs::path& destination,tp::Progress* progress,int compression){
 Export x{root,{},progress,compression};const auto started=std::chrono::steady_clock::now();
 try{
 need(compression>=0 && compression<=9,"Invalid PNG compression level");need(!fs::exists(destination),"Choose a new export folder");
 std::vector<fs::path> banks{root/"data/graphics/pc/textures.big"},fonts,loose,terrains;
 if(fs::exists(root/"data/graphics/pc/frontend.big"))banks.push_back(root/"data/graphics/pc/frontend.big");
 const std::set<std::string> extensions={".dds",".png",".tga",".bmp",".jpg",".jpeg"};
 for(const auto& e:fs::recursive_directory_iterator(root/"data")){x.check();if(!e.is_regular_file())continue;auto ext=e.path().extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});if(extensions.contains(ext))loose.push_back(e.path());if(e.path().filename()=="fonts.big")fonts.push_back(e.path());if(ext==".stb")terrains.push_back(e.path());}
 std::sort(fonts.begin(),fonts.end());std::sort(loose.begin(),loose.end());std::sort(terrains.begin(),terrains.end());
 size_t total=loose.size();auto countEntries=[&](const fs::path& path){auto file=forge::big::File::open(path);for(const auto& bank:file.banks())total+=bank.entries.size();};
 for(const auto& path:banks)countEntries(path);for(const auto& path:fonts)countEntries(path);
 for(const auto& path:terrains){auto archive=forge::stb::Archive::open(path);total+=archive.entries().size();std::set<uint32_t> ids;for(const auto& map:archive.staticMaps()){auto record=archive.readStaticMapRecord(map);if(record.size()>=forge::stbinfo::kInfoBlockSize)ids.insert(forge::stbinfo::readInfoBlock(record.data()).bankFileIndex);}
  for(const auto& e:archive.entries()){x.check();if(ids.contains(e.id))for(const auto& frame:forge::stbbake::walkFramedBlocks(archive.read(e)))if(terrainCandidate(frame.data))++total;}
 }
 if(progress){progress->done=0;progress->total=total;progress->totalOverride=total;progress->completedBefore=0;}
 fs::create_directories(fs::absolute(destination).parent_path());forge::TemporaryDirectory pending(fs::absolute(destination).parent_path(),".tx-");x.folder=pending.path()/"collection";
#ifdef _WIN32
 auto native=fs::absolute(x.folder).wstring();
 if(!native.starts_with(L"\\\\?\\")) native=native.starts_with(L"\\\\")?L"\\\\?\\UNC\\"+native.substr(2):L"\\\\?\\"+native;
 x.folder=fs::path(native);
#endif
 for(size_t n=0;n<banks.size();++n){x.check();auto project=n?x.folder/"Front end":x.folder;if(progress)progress->completedBefore=progress->done;
 auto result=tp::exportProject(banks[n],project,{},{},progress,true,compression);need(result.ok,result.error);x.result.exported+=result.exported;x.result.originals+=result.originals;
 for(const auto& [stage,seconds]:result.timings)x.result.timings[stage]+=seconds;
 if(n==0)fs::rename(x.folder/"export-report.json",x.folder/"main-bank-report.json");
 x.source(banks[n],[&]{specialTextures(x,banks[n],project,n?"Front end special":"Special textures");});
 x.index["resources"].push_back({{"project",n?"Front end/texture-project.json":"texture-project.json"},{"source",fs::relative(banks[n],root).generic_string()},{"originalEntries",result.originals},{"editableImages",result.exported},{"importPolicy",n?"Forge texture project targeting frontend.big":"Forge texture project"}});
 }
 for(const auto& path:loose){x.source(path,[&]{auto relative=fs::relative(path,root).generic_string();auto bytes=read(path);Json meta={{"source",relative},{"kind","loose image"},{"importPolicy","replace original-relative path"}};x.raw("originals/Loose/"+relative,bytes,meta);std::string error;auto decoded=forge::texturewrite::loadImage(path.string(),error);if(!error.empty()){x.issue(relative+": "+error);return;}te::Image image;image.width=decoded.width;image.height=decoded.height;image.rgba=std::move(decoded.rgba);x.png("images/Loose/"+relative+".png",image,meta);});x.tick();}
 for(const auto& path:fonts){x.source(path,[&]{const auto file=forge::big::File::open(path);for(const auto& bank:file.banks())for(const auto& e:bank.entries){x.check();auto bytes=file.entryData(e);auto stem=label(path.parent_path().filename().string())+"/"+label(bank.name)+"/"+std::to_string(e.id)+"_"+label(e.name);Json meta={{"source",fs::relative(path,root).generic_string()},{"bank",bank.name},{"id",e.id},{"name",e.name},{"type",e.type},{"kind","font"}};x.raw("originals/Fonts/"+stem+".bin",bytes,meta);write(x.folder/("originals/Fonts/"+stem+".info"),e.subHeader);
 try{if(bank.name.starts_with("FONT_"))staticFont(x,bytes,stem,meta);else if(bank.name.ends_with("_PC")){if(e.name=="GlyphData")glyphPages(x,bytes,stem,meta);else fontMetadata(x,bytes,stem);}else x.index["notes"].push_back(bank.name+"/"+e.name+": non-PC font data preserved without raster conversion");}catch(const std::exception& error){x.check();x.issue(bank.name+"/"+e.name+": "+error.what());}x.tick();}});}
 for(const auto& path:terrains){x.source(path,[&]{auto archive=forge::stb::Archive::open(path);std::map<uint32_t,std::string> mapNames;for(const auto& map:archive.staticMaps()){auto record=archive.readStaticMapRecord(map);if(record.size()>=forge::stbinfo::kInfoBlockSize)mapNames[forge::stbinfo::readInfoBlock(record.data()).bankFileIndex]=map.levelName;}
 for(const auto& e:archive.entries()){x.check();if(!mapNames.contains(e.id)){x.tick();continue;}auto bytes=archive.read(e);auto frames=forge::stbbake::walkFramedBlocks(bytes);for(const auto& frame:frames){x.check();const auto& b=frame.data;if(!terrainCandidate(b))continue;x.tick();auto w=u16(b,17),h=u16(b,19);
 try {forge::stbstorage::validateBackground(b);}catch(const std::exception& error){x.issue(mapNames[e.id]+": unsupported terrain frame: "+error.what());continue;}
 auto tex=forge::stbbake::parseInlineTexture(slice(b,17,19+size_t(w)*h/2));if((tex.pixelFormat0&31)!=3 || tex.levels!=1){x.issue(mapNames[e.id]+": unsupported terrain inline format");continue;}need(tex.mipData.size()>=size_t((w+3)/4)*((h+3)/4)*8,"Truncated terrain DXT1 surface");
 auto mapName=mapNames[e.id];std::replace(mapName.begin(),mapName.end(),'\\','/');auto stem=label(path.stem().string())+"/"+std::to_string(e.id)+"_"+label(fs::path(mapName).stem().string())+"/patch_"+std::to_string(frame.frameOffset);Json meta={{"source",fs::relative(path,root).generic_string()},{"kind","terrain patch"},{"entryId",e.id},{"map",mapNames[e.id]},{"entryOffset",e.offset},{"frameOffset",frame.frameOffset},{"inlineOffset",17},{"importPolicy","terrain rebake or specialized patch writer required"}};
 x.raw("originals/Terrain/"+stem+".frame",slice(bytes,frame.frameOffset,8+frame.compLen),meta);te::Image image;image.width=w;image.height=h;image.rgba=te::decodeBc1ToRgba(tex.mipData.data(),w,h);x.png("images/Terrain/"+stem+".png",image,meta);
 }x.tick();}});}
 x.check();x.result.timings["collectionTotal"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
 json(x.folder/"collection-index.json",x.index);
 json(x.folder/"export-report.json",{{"tool","Fable Texture Export"},{"version",2},{"images",x.result.exported},{"originalEntries",x.result.originals},{"notConvertedToPng",x.result.skipped},{"pngCompressionLevel",compression},{"details",x.result.details},{"profileSeconds",x.result.timings},{"coverage",{"main and GUI banks","front-end bank when present","loose data images","static font atlases and PC streaming glyph pages","recognized STB background texture patches"}},{"notes",x.index["notes"]}});
 std::ofstream guide(x.folder/"README.txt",std::ios::app);guide<<"\nADDITIONAL SOURCES\nFront end/ is a separate texture project. Current Forge routes review and import to frontend.big automatically.\nimages/Special textures contains frame strips, volume slices and special-format previews.\nimages/Fonts contains static atlases and streaming glyph pages, with character/metric JSON mappings.\nimages/Loose mirrors original-relative image paths. images/Terrain contains decoded baked terrain patches.\ncollection-index.json records source archives, identities, hashes, frame/slice layouts and import policies.\nSupplemental PNGs are extraction assets; they are not automatically importable through the ordinary texture-project writer.\nSigned UV PNGs preserve signed channels using the mapping recorded in the index.\nOriginals retain native mip chains; PNG previews show the top mip. Non-PC font data is preserved raw.\n";guide.close();need(bool(guide),"Cannot write collection guide");
 x.check();need(!fs::exists(destination),"Export destination appeared during export");fs::rename(x.folder,destination);x.result.ok=true;
 }catch(const std::exception& error){x.result.error=error.what();}
 if(progress){progress->completedBefore=0;progress->totalOverride=0;}
 return x.result;
}
}
