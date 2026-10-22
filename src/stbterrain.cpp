#include "profile.hpp"
#include "stbterrain.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <set>

#include "forge/lzo.hpp"
#include "forge/stbbake.hpp"
#include "forge/stb.hpp"
#include "forge/stbinfo.hpp"
#include "forge/rangecodec.hpp"

namespace albion::stbterrain {

namespace fs = std::filesystem;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

struct Layer {
    std::vector<std::pair<uint16_t, uint16_t>> xy;
    std::vector<uint16_t> strip;
    bool shared = false;
};

// CLandscapeForegroundPatch frame body:
//   u16 layerCount; per layer: u16 vertexCount, u16 polygonCount, u8 mappingDirection,
//   3*u32 textures, u8 sharedIndexBuffer, u32 textureMaxSize, u32 bumpMaxSize,
//   f32 selfIllumination, vertexCount*(u16 x, u16 y, f32 height, u32 normal, u8 blend,
//   u8 cliffU, u8 cliffV), [polygonCount+2 u16 strip unless shared]; u8 hasWater; ...
bool parseForeground(const std::vector<uint8_t>& b, std::vector<Layer>& out) {
    size_t p = 0;
    auto need = [&](size_t n) { return p + n <= b.size(); };
    auto r8 = [&]() { return b[p++]; };
    auto r16 = [&]() { const uint16_t v = uint16_t(b[p] | (b[p + 1] << 8)); p += 2; return v; };
    auto r32 = [&]() { uint32_t v; std::memcpy(&v, b.data() + p, 4); p += 4; return v; };
    if (!need(2)) return false;
    const uint16_t layers = r16();
    if (layers == 0 || layers > 64) return false;
    for (uint16_t li = 0; li < layers; ++li) {
        if (!need(2 + 2 + 1 + 12 + 1 + 4 + 4 + 4)) return false;
        Layer L;
        const uint16_t vc = r16();
        const uint16_t pc = r16();
        const uint8_t dir = r8();
        if (vc == 0 || vc > 4096 || dir > 4) return false;
        for (int i = 0; i < 3; ++i) { const uint32_t tex = r32(); if (tex > 100000) return false; }
        L.shared = r8() != 0;
        const uint32_t tms = r32(), bms = r32(); (void)tms; (void)bms;
        r32();  // self illumination
        if (!need(size_t(vc) * 15)) return false;
        L.xy.reserve(vc);
        for (uint16_t vi = 0; vi < vc; ++vi) {
            const uint16_t x = r16(), y = r16();
            p += 4 + 4 + 3;
            L.xy.emplace_back(x, y);
        }
        if (!L.shared) {
            if (pc > 20000 || !need((size_t(pc) + 2) * 2)) return false;
            L.strip.reserve(size_t(pc) + 2);
            for (size_t i = 0; i < size_t(pc) + 2; ++i) L.strip.push_back(r16());
            for (uint16_t idx : L.strip) if (idx >= vc) return false;
        }
        out.push_back(std::move(L));
    }
    return need(1);   // hasWater byte at least
}

template <typename Fn>
void forEachFrame(const std::vector<forge::stbbake::FramedBlock>& frames, Fn&& onFrame) {
    for (const auto& b : frames) onFrame(b.data);
}

template <typename Fn>
void forEachFrame(const std::vector<uint8_t>& d, Fn&& onFrame) {
    // A chunk's LZO frames are packed back-to-back at byte granularity: retail
    // OakValeWest_v2 holds 895, and 530 of them start off the 4-byte lattice the
    // old probe stepped on (the town-square oak and three quarters of the grass
    // lived in those). Walk them with the writer's proven byte-granular walker.
    for (auto& b : forge::stbbake::walkFramedBlocks(d)) onFrame(b.data);
}

// Shared: locate the map's chunk + world origin.
bool findChunk(const fs::path& gameRoot, const std::string& mapName, std::vector<uint8_t>& chunk,
               int& worldX, int& worldY, std::string& note,uint32_t* backgroundRoot=nullptr) {
    const fs::path stbPath = gameRoot / "data" / "Levels" / "FinalAlbion_RT.stb";
    if (!fs::exists(stbPath)) { note = "no STB"; return false; }
    try {
        const auto archive = forge::stb::Archive::open(stbPath);
        const std::string stem = lower(mapName);
        const forge::stb::StaticMap* map = nullptr;
        for (const auto& c : archive.staticMaps())
            if (lower(fs::path(c.levelName).stem().string()) == stem) { map = &c; break; }
        if (!map) { note = "no STB entry"; return false; }
        const auto record = archive.readStaticMapRecord(*map);
        if (record.size() < forge::stbinfo::kInfoBlockSize) { note = "short record"; return false; }
        const auto info = forge::stbinfo::readInfoBlock(record.data());
        worldX = info.worldX; worldY = info.worldY;
        for (const auto& e : archive.entries())
            if (int32_t(e.id) == info.bankFileIndex) { chunk = archive.read(e); break; }
        if (chunk.empty()) { note = "no chunk"; return false; }
        if(backgroundRoot) {
            *backgroundRoot=0;
            if(info.landscapeMapPtr>0)try {
                if(uint32_t(info.landscapeMapPtr)<map->relativeOffset)
                    throw std::runtime_error("saved landscape descriptor precedes its map record");
                const auto parsed=forge::stbbake::parseLandscapeRootDescriptor(record,
                    uint32_t(info.landscapeMapPtr)-map->relativeOffset);
                if(parsed.backgroundRootPos && parsed.backgroundRootSize &&
                    uint64_t(parsed.backgroundRootPos)+parsed.backgroundRootSize<=chunk.size())
                    *backgroundRoot=uint32_t(parsed.backgroundRootPos);
                else note="no saved background tree";
            } catch(const std::exception& e) {note=e.what();}
            else note="no saved landscape";
        }
        return true;
    } catch (const std::exception& e) {
        note = e.what();
        return false;
    }
}

} // namespace

BackgroundAlbedo backgroundAlbedo(const std::vector<forge::stbbake::FramedBlock>& frames,
                                       int worldX, int worldY, int mapWidth, int mapHeight) {
    FORGE_ZONE("STB background decode");
    BackgroundAlbedo out;
    if (mapWidth <= 0 || mapHeight <= 0) return out;
    const int tpc = out.texelsPerCell;
    out.image.width = uint32_t(mapWidth * tpc);
    out.image.height = uint32_t(mapHeight * tpc);
    out.image.rgba.assign(size_t(out.image.width) * out.image.height * 4, 0);
    out.image.name = "background";
    std::vector<uint8_t> covered(size_t(mapWidth) * mapHeight, 0);
    std::vector<float> density(size_t(out.image.width)*out.image.height,-1.f);
    forEachFrame(frames, [&](const std::vector<uint8_t>& b) {
        // CLandscapeBackgroundPatch: 17-byte header, 19-byte inline texture header, DXT1 mip 0.
        if (b.size() < 17 + 19) return;
        auto r16 = [&](size_t o) { return uint16_t(b[o] | (b[o + 1] << 8)); };
        auto r32 = [&](size_t o) { uint32_t v; std::memcpy(&v, b.data() + o, 4); return v; };
        const uint16_t pw = r16(0), ph = r16(2), c0 = r16(4), c1 = r16(6);
        const bool waterOnly = b[8] != 0;
        const uint16_t vertexCount = r16(12);
        const bool isDXT = b[16] != 0;
        if (waterOnly || pw == 0 || ph == 0 || pw > mapWidth || ph > mapHeight || !vertexCount) return;
        const uint16_t tw = r16(17), th = r16(19);
        const uint8_t levels = b[21];
        const uint32_t fmt0 = r32(22);
        if (!isDXT || levels == 0 || tw == 0 || th == 0 || tw > 512 || th > 512) return;
        // CTexture packs flags above the five-bit format value (retail DXT1
        // commonly uses 0x423). Match the core baker and storage validator.
        if ((fmt0 & 0x1fu) != 3u) return;
        size_t textureBytes = 0;
        unsigned mipW = tw, mipH = th;
        for (unsigned level = 0; level < levels; ++level) {
            textureBytes += size_t((mipW + 3) / 4) * ((mipH + 3) / 4) * 8;
            if (level + 1 < levels && mipW == 1 && mipH == 1) return;
            mipW = std::max(1u, mipW / 2);
            mipH = std::max(1u, mipH / 2);
        }
        if (textureBytes > b.size() - (17 + 19)) return;
        // Patch origin: world grid coords (subtract the map origin), else map-local.
        int px = int(c0) - worldX, py = int(c1) - worldY;
        if (px < 0 || py < 0 || px + pw > mapWidth || py + ph > mapHeight) { px = c0; py = c1; }
        if (px < 0 || py < 0 || px + pw > mapWidth || py + ph > mapHeight) return;
        std::vector<forge::stbbake::PatchVertex> vertices;
        std::vector<std::array<uint16_t,3>> triangles;
        try {
            const auto patch=forge::stbbake::parsePatchBody(b);
            if(!patch.valid || patch.waterOnly || patch.texture.size()!=19+textureBytes)return;
            vertices=forge::stbbake::decodePatchVertices(patch);
            if(patch.header.detailMode==1) {
                const auto shared=forge::terrain::buildSharedLayerTopology(pw,ph);
                if(shared.vertices.size()!=vertices.size() || shared.indices.size()-2!=patch.header.indexCount)return;
                for(size_t i=2;i<shared.indices.size();++i)
                    triangles.push_back({shared.indices[i-2],shared.indices[i-1],shared.indices[i]});
            } else {
                const size_t count=size_t(patch.header.indexCount)*3;
                const auto ib=forge::rangecodec::decode(patch.ibBlock.data()+4,patch.ibBlock.size()-4,count,2);
                if(ib.size()!=count*2)return;
                for(size_t i=0;i<count;i+=3) {
                    std::array<uint16_t,3> ids;
                    for(size_t j=0;j<3;++j)ids[j]=uint16_t(ib[(i+j)*2]|(uint16_t(ib[(i+j)*2+1])<<8));
                    triangles.push_back(ids);
                }
            }
        } catch(const std::exception&) {return;}
        if(vertices.empty() || triangles.empty())return;
        bool absolute=true,local=true;
        for(const auto& v:vertices) {
            if(v.uv0>255 || v.uv1>255 || !std::isfinite(v.height))return;
            absolute&=int(v.gridX)-worldX>=0 && int(v.gridX)-worldX<=mapWidth &&
                      int(v.gridY)-worldY>=0 && int(v.gridY)-worldY<=mapHeight;
            local&=v.gridX<=mapWidth && v.gridY<=mapHeight;
        }
        if(!absolute && !local)return;
        struct PreviewVertex {int x,y;double u,v;};
        std::vector<PreviewVertex> projected;
        int minX=mapWidth,minY=mapHeight,maxX=0,maxY=0;
        for(const auto& v:vertices) {
            const int x=int(v.gridX)-(absolute?worldX:0),y=int(v.gridY)-(absolute?worldY:0);
            minX=std::min(minX,x);minY=std::min(minY,y);maxX=std::max(maxX,x);maxY=std::max(maxY,y);
            projected.push_back({x,y,double(v.uv0)/255,double(v.uv1)/255});
        }
        if(minX!=px || minY!=py || maxX<=minX || maxY<=minY)return;
        // Validate every index before changing the image: malformed later
        // triangles must not leave partially accepted preview fragments.
        for(const auto& ids:triangles)for(auto id:ids)if(id>=projected.size())return;
        const auto rgba = terrainexport::decodeBc1ToRgba(b.data() + 17 + 19, tw, th);
        // Sample actual mesh UVs, including adaptive local IBs and scaled
        // shared grids. Header subdivision counts are not physical extents.
        const float detail=float(tw)*th/(float(maxX-minX)*(maxY-minY));
        bool painted=false;
        for(const auto& ids:triangles) {
            const auto& a=projected[ids[0]];const auto& c=projected[ids[2]];const auto& d=projected[ids[1]];
            const double area=double(d.x-a.x)*(c.y-a.y)-double(d.y-a.y)*(c.x-a.x);
            if(area==0)continue;
            const int x0=std::min({a.x,d.x,c.x})*tpc,x1=std::max({a.x,d.x,c.x})*tpc;
            const int y0=std::min({a.y,d.y,c.y})*tpc,y1=std::max({a.y,d.y,c.y})*tpc;
            for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x) {
                const size_t pixel=size_t(y)*out.image.width+x;
                if(detail<=density[pixel])continue;
                const double wx=(double(x)+.5)/tpc,wy=(double(y)+.5)/tpc;
                const double wa=((d.x-wx)*(c.y-wy)-(d.y-wy)*(c.x-wx))/area;
                const double wb=((c.x-wx)*(a.y-wy)-(c.y-wy)*(a.x-wx))/area;
                const double wc=1-wa-wb;
                if(wa < -1e-10 || wb < -1e-10 || wc < -1e-10)continue;
                const double u=std::clamp(wa*a.u+wb*d.u+wc*c.u,0.,1.);
                const double v=std::clamp(wa*a.v+wb*d.v+wc*c.v,0.,1.);
                const int sx=std::min(int(tw)-1,int(u*tw)),sy=std::min(int(th)-1,int(v*th));
                const uint8_t* s=&rgba[(size_t(sy)*tw+sx)*4];
                uint8_t* target=&out.image.rgba[pixel*4];
                target[0]=s[0];target[1]=s[1];target[2]=s[2];target[3]=255;
                density[pixel]=detail;covered[size_t(y/tpc)*mapWidth+x/tpc]=1;painted=true;
            }
        }
        if(painted)++out.patches;
    });
    const size_t cov = size_t(std::count(covered.begin(), covered.end(), uint8_t(1)));
    out.found = out.patches > 0;
    out.note = std::to_string(out.patches) + " background patches cover " + std::to_string(cov) + " of " + std::to_string(size_t(mapWidth) * mapHeight) + " cells";
    return out;
}

BackgroundAlbedo backgroundAlbedoFromTree(const std::vector<uint8_t>& chunk,size_t root,
                                         int worldX,int worldY,int mapWidth,int mapHeight) {
    BackgroundAlbedo out;
    if(!root){out.note="no saved background tree";return out;}
    try {
        const auto tree=forge::stbbake::parseBackgroundTree(chunk,root);
        std::set<size_t> seen;
        std::vector<forge::stbbake::FramedBlock> frames;
        auto visit=[&](auto&& self,const forge::stbbake::BackgroundTreeNode& node)->void {
            for(size_t i=0;i<node.header.lod.size();++i) {
                const auto& lod=node.header.lod[i];
                const int owner=int(lod.optimizedBandRemap)-node.header.firstNonSplitBand;
                if(owner<0 || size_t(owner)>i || node.header.lod[size_t(owner)].optimizedBandRemap!=lod.optimizedBandRemap)
                    throw std::runtime_error("saved background has an invalid band remap");
                if(size_t(owner)!=i)continue;
                if(lod.fileBlockPos<=0 || lod.fileBlockSize<=0 || lod.offsetIntoFileBlock<0 ||
                    lod.offsetIntoFileBlock>=lod.fileBlockSize ||
                    uint64_t(lod.fileBlockPos)+lod.fileBlockSize>chunk.size())
                    throw std::runtime_error("saved background frame exceeds its owned file block");
                const size_t at=size_t(lod.fileBlockPos)+lod.offsetIntoFileBlock;
                if(!seen.insert(at).second)continue;
                size_t end=at;
                forge::stbbake::FramedBlock frame;frame.frameOffset=at;
                frame.data=forge::lzo::decompressFramed(chunk,end);
                if(end>size_t(lod.fileBlockPos)+lod.fileBlockSize)
                    throw std::runtime_error("saved background compressed frame exceeds its owned file block");
                frame.compLen=uint32_t(end-at-8);frames.push_back(std::move(frame));
            }
            for(const auto& child:node.children)self(self,child);
        };visit(visit,tree);
        std::sort(frames.begin(),frames.end(),[](const auto& a,const auto& b){return a.frameOffset<b.frameOffset;});
        return backgroundAlbedo(frames,worldX,worldY,mapWidth,mapHeight);
    } catch(const std::exception& e){out.note=e.what();return out;}
}

BackgroundAlbedo backgroundAlbedo(const fs::path& gameRoot, const std::string& mapName, int mapWidth, int mapHeight) {
    FORGE_ZONE("STB background read");
    BackgroundAlbedo out;
    std::vector<uint8_t> chunk;
    int worldX = 0, worldY = 0;
    uint32_t root=0;
    if (!findChunk(gameRoot, mapName, chunk, worldX, worldY, out.note,&root) || mapWidth <= 0 || mapHeight <= 0 || !root) return out;
    return backgroundAlbedoFromTree(chunk,root,worldX,worldY,mapWidth,mapHeight);
}

ForegroundLayers loadLayers(const fs::path& gameRoot, const std::string& mapName, int mapWidth, int mapHeight, BackgroundAlbedo* background) {
    FORGE_ZONE("STB terrain layers read");
    if (background) *background = {};
    ForegroundLayers out;
    std::vector<uint8_t> chunk;
    int worldX = 0, worldY = 0;
    if (!findChunk(gameRoot, mapName, chunk, worldX, worldY, out.note)) return out;
    const auto frames = forge::stbbake::walkFramedBlocks(chunk);
    int frameIndex = 0;
    forEachFrame(frames, [&](const std::vector<uint8_t>& b) {
        ++frameIndex;
        // Same grammar as parseForeground, keeping everything.
        size_t p = 0;
        auto need = [&](size_t n) { return p + n <= b.size(); };
        auto r8 = [&]() { return b[p++]; };
        auto r16 = [&]() { const uint16_t v = uint16_t(b[p] | (b[p + 1] << 8)); p += 2; return v; };
        auto r32 = [&]() { uint32_t v; std::memcpy(&v, b.data() + p, 4); p += 4; return v; };
        auto rf = [&]() { float v; std::memcpy(&v, b.data() + p, 4); p += 4; return v; };
        if (!need(2)) return;
        const uint16_t layers = r16();
        if (layers == 0 || layers > 64) return;
        std::vector<ForegroundLayer> parsed;
        for (uint16_t li = 0; li < layers; ++li) {
            if (!need(2 + 2 + 1 + 12 + 1 + 4 + 4 + 4)) return;
            ForegroundLayer L;
            L.patchIndex = frameIndex - 1; L.layerIndex = li;
            const uint16_t vc = r16();
            const uint16_t pc = r16();
            L.direction = r8();
            if (vc == 0 || vc > 4096 || L.direction > 4) return;
            L.texture = r32(); L.backgroundTexture = r32(); L.bumpTexture = r32();
            if (L.texture > 100000 || L.backgroundTexture > 100000 || L.bumpTexture > 100000) return;
            L.sharedIndexBuffer = r8() != 0;
            r32(); r32();
            L.selfIllumination = rf();
            if (!need(size_t(vc) * 15)) return;
            L.vertices.reserve(vc);
            for (uint16_t vi = 0; vi < vc; ++vi) {
                LayerVertex v;
                v.x = int(r16()) - worldX; v.y = int(r16()) - worldY;
                v.height = rf(); r32();
                v.blend = r8(); v.b1 = r8(); v.b2 = r8();
                if (v.x < 0 || v.y < 0 || v.x > mapWidth || v.y > mapHeight) return;
                L.vertices.push_back(v);
            }
            if (!L.sharedIndexBuffer) {
                if (pc > 20000 || !need((size_t(pc) + 2) * 2)) return;
                L.strip.reserve(size_t(pc) + 2);
                for (size_t i = 0; i < size_t(pc) + 2; ++i) L.strip.push_back(r16());
                for (uint16_t idx : L.strip) if (idx >= vc) return;
            }
            parsed.push_back(std::move(L));
        }
        if (!need(1)) return;
        ++out.frames;
        for (auto& L : parsed) out.layers.push_back(std::move(L));
    });
    out.found = out.frames > 0;
    if (!out.found) out.note = "no foreground frames matched";
    if (background && out.found && !out.layers.empty()) *background = backgroundAlbedo(gameRoot,mapName,mapWidth,mapHeight);
    return out;
}

CellMask load(const fs::path& gameRoot, const std::string& mapName, int mapWidth, int mapHeight) {
    CellMask m;
    m.width = mapWidth; m.height = mapHeight;
    m.present.assign(size_t(std::max(mapWidth, 0)) * size_t(std::max(mapHeight, 0)), 1);
    m.presentCells = int(m.present.size());
    if (mapWidth <= 0 || mapHeight <= 0) { m.note = "empty map"; return m; }
    std::vector<uint8_t> chunk;
    int worldX = 0, worldY = 0;
    if (!findChunk(gameRoot, mapName, chunk, worldX, worldY, m.note)) return m;

    std::vector<uint8_t> touched(m.present.size(), 0);
    int frames = 0;
    const bool debug = std::getenv("ALBION_DEBUG") != nullptr;
    int total = 0, fgParsed = 0, rejected = 0;
    forEachFrame(chunk, [&](const std::vector<uint8_t>& body) {
        ++total;
        std::vector<Layer> layers;
        if (!parseForeground(body, layers)) return;
        ++fgParsed;
        if (debug) {
            int minx = 1 << 30, maxx = -(1 << 30), miny = 1 << 30, maxy = -(1 << 30); size_t verts = 0, strips = 0;
            for (const auto& L : layers) { for (auto [x, y] : L.xy) { minx = std::min<int>(minx, x); maxx = std::max<int>(maxx, x); miny = std::min<int>(miny, y); maxy = std::max<int>(maxy, y); ++verts; } strips += L.strip.size(); }
            std::fprintf(stderr, "fg frame %zu bytes: %zu layers, %zu verts, %zu strip idx, x %d..%d y %d..%d (local %d..%d, %d..%d)\n",
                         body.size(), layers.size(), verts, strips, minx, maxx, miny, maxy, minx - worldX, maxx - worldX, miny - worldY, maxy - worldY);
        }
        // Vertices are stored in WORLD grid units; sanity-check against the map box.
        bool any = false;
        for (const auto& L : layers) {
            if (L.shared) {
                // sharedIndexBuffer = the engine's canonical full-grid index buffer for
                // this vertex grid (a 17x17 layer is a complete 16x16 patch): every
                // cell inside the layer's vertex box is drawn.
                int minx = 1 << 30, maxx = -(1 << 30), miny = 1 << 30, maxy = -(1 << 30);
                for (auto [vx, vy] : L.xy) { minx = std::min<int>(minx, int(vx) - worldX); maxx = std::max<int>(maxx, int(vx) - worldX); miny = std::min<int>(miny, int(vy) - worldY); maxy = std::max<int>(maxy, int(vy) - worldY); }
                if (minx < 0 || miny < 0 || maxx > mapWidth || maxy > mapHeight || maxx - minx > 64 || maxy - miny > 64) { ++rejected; continue; }
                for (int y = miny; y < maxy && y < mapHeight; ++y)
                    for (int x = minx; x < maxx && x < mapWidth; ++x) { touched[size_t(y) * mapWidth + x] = 1; any = true; }
                continue;
            }
            const std::vector<uint16_t>* strip = &L.strip;
            if (strip->size() < 3) continue;
            for (size_t i = 2; i < strip->size(); ++i) {
                const uint16_t a = (*strip)[i - 2], b = (*strip)[i - 1], c = (*strip)[i];
                if (a == b || b == c || a == c) continue;   // degenerate bridge
                int xs[3] = {int(L.xy[a].first) - worldX, int(L.xy[b].first) - worldX, int(L.xy[c].first) - worldX};
                int ys[3] = {int(L.xy[a].second) - worldY, int(L.xy[b].second) - worldY, int(L.xy[c].second) - worldY};
                const int x0 = std::min({xs[0], xs[1], xs[2]}), x1 = std::max({xs[0], xs[1], xs[2]});
                const int y0 = std::min({ys[0], ys[1], ys[2]}), y1 = std::max({ys[0], ys[1], ys[2]});
                if (x0 < 0 || y0 < 0 || x1 > mapWidth || y1 > mapHeight || x1 - x0 > 32 || y1 - y0 > 32) { ++rejected; if (debug) std::fprintf(stderr, "  rejected tri local (%d..%d, %d..%d)\n", x0, x1, y0, y1); continue; }  // out of this map box: skip the triangle
                // Decimated patches span several cells per triangle: mark the box.
                for (int y = y0; y < std::max(y1, y0 + 1) && y < mapHeight; ++y)
                    for (int x = x0; x < std::max(x1, x0 + 1) && x < mapWidth; ++x) { touched[size_t(y) * mapWidth + x] = 1; any = true; }
            }
        }
        if (any) ++frames;
    });
    m.frames = frames;
    if (debug) std::fprintf(stderr, "frames total %d, foreground-parsed %d, used %d, rejected %d\n", total, fgParsed, frames, rejected);
    if (frames == 0) { m.note = "no foreground frames matched"; return m; }
    m.found = true;
    m.present = std::move(touched);
    m.presentCells = int(std::count(m.present.begin(), m.present.end(), uint8_t(1)));
    return m;
}

} // namespace albion::stbterrain
