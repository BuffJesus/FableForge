#include "lodbake.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "dxt1.hpp"

namespace albion::editor {

LodAlbedo bakeLodAlbedo(const std::filesystem::path& gameRoot, const forge::lev::File& lev, float gain) {
    LodAlbedo out;
    terrainexport::Context ctx;
    std::string err;
    out.textured = ctx.load(gameRoot, gameRoot / "data" / "graphics" / "pc" / "textures.big", err);
    terrainexport::Options o;
    o.textures = out.textured; o.texelsPerCell = out.texelsPerCell; o.gain = gain; o.engineLayers = false; o.gameRoot = gameRoot;
    o.texturesBig = gameRoot / "data" / "graphics" / "pc" / "textures.big";
    const auto scene = terrainexport::buildScene(lev, o, out.textured ? &ctx : nullptr);
    if (scene.hasAlbedo && scene.albedo.width && scene.albedo.height) {
        out.image = scene.albedo;
    } else {
        // flat: one mid tone per cell (no theme textures in this install)
        out.image.width = uint32_t(lev.width() * out.texelsPerCell);
        out.image.height = uint32_t(lev.height() * out.texelsPerCell);
        out.image.rgba.assign(size_t(out.image.width) * out.image.height * 4, 255);
        for (size_t i = 0; i < out.image.rgba.size(); i += 4) { out.image.rgba[i] = 120; out.image.rgba[i + 1] = 130; out.image.rgba[i + 2] = 80; }
    }
    return out;
}

terrainexport::Image lodTile(const LodAlbedo& albedo, int x, int y, int w, int h, bool flipY, int tw, int th) {
    terrainexport::Image tile;
    tile.width = uint32_t(tw); tile.height = uint32_t(th);
    tile.rgba.assign(size_t(tw) * size_t(th) * 4, 255);
    const auto& src = albedo.image;
    const float sx = float(w) / float(tw), sy = float(h) / float(th);   // cells per tile texel
    const float tpc = float(albedo.texelsPerCell);
    for (int ty = 0; ty < th; ++ty)
        for (int tx = 0; tx < tw; ++tx) {
            // the cell rect this texel covers, box-filtered over the albedo
            const float cx0 = float(x) + float(tx) * sx, cx1 = cx0 + sx;
            const int row = flipY ? th - 1 - ty : ty;
            const float cy0 = float(y) + float(row) * sy, cy1 = cy0 + sy;
            const int ax0 = std::clamp(int(std::floor(cx0 * tpc)), 0, int(src.width) - 1), ax1 = std::clamp(int(std::ceil(cx1 * tpc)), ax0 + 1, int(src.width));
            const int ay0 = std::clamp(int(std::floor(cy0 * tpc)), 0, int(src.height) - 1), ay1 = std::clamp(int(std::ceil(cy1 * tpc)), ay0 + 1, int(src.height));
            float acc[3] = {0, 0, 0}; int n = 0;
            for (int ay = ay0; ay < ay1; ++ay)
                for (int ax = ax0; ax < ax1; ++ax) {
                    const uint8_t* p = &src.rgba[(size_t(ay) * src.width + size_t(ax)) * 4];
                    acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; ++n;
                }
            uint8_t* q = &tile.rgba[(size_t(ty) * size_t(tw) + size_t(tx)) * 4];
            for (int k = 0; k < 3; ++k) q[k] = uint8_t(n ? std::clamp(acc[k] / float(n), 0.0f, 255.0f) : 0);
            q[3] = 255;
        }
    return tile;
}

terrainexport::Image nativeLodTile(const LodAlbedo& albedo,int x,int y,int w,int h,
                                  const forge::stbbake::BackgroundBandLockInfo& locks) {
    const int tw=locks.textureExtentX,th=locks.textureExtentY,tpc=albedo.texelsPerCell;
    const auto& src=albedo.image;
    if(x<0 || y<0 || w<1 || h<1 || w>128 || h>128 || tw<4 || th<4 || tw>128 || th>128 ||
        tw%4 || th%4 || tpc<1 || (int64_t(x)+w)*tpc>src.width || (int64_t(y)+h)*tpc>src.height ||
        !src.width || !src.height || src.rgba.size()!=size_t(src.width)*src.height*4 ||
        locks.u.size()!=size_t(w+1) || locks.v.size()!=size_t(h+1))
        throw std::invalid_argument("native albedo projection dimensions are invalid");
    auto boundaries=[](const std::vector<float>& uv,int pixels,int origin,int texelsPerCell) {
        if(uv.front()!=0 || uv.back()!=1 || !std::is_sorted(uv.begin(),uv.end()))
            throw std::invalid_argument("native albedo UV table is not monotone with normalized endpoints");
        std::vector<float> quantized;quantized.reserve(uv.size());
        for(float value:uv)quantized.push_back(float(forge::stbbake::packBackgroundUV(value))/255.f);
        std::vector<float> result;result.reserve(size_t(pixels+1));
        for(int pixel=0;pixel<=pixels;++pixel) {
            const float coordinate=float(pixel)/pixels;
            const auto upper=std::upper_bound(quantized.begin(),quantized.end(),coordinate);
            float world=0;
            if(upper==quantized.end())world=float(quantized.size()-1);
            else {
                const auto right=size_t(upper-quantized.begin()),left=right-1;
                // upper_bound skips quantized plateaus (zero-area native cells).
                world=float(left)+(coordinate-quantized[left])/(quantized[right]-quantized[left]);
            }
            result.push_back((float(origin)+world)*texelsPerCell);
        }
        return result;
    };
    const auto xs=boundaries(locks.u,tw,x,tpc),ys=boundaries(locks.v,th,y,tpc);
    terrainexport::Image tile;tile.width=tw;tile.height=th;tile.rgba.resize(size_t(tw)*th*4,255);
    for(int ty=0;ty<th;++ty)for(int tx=0;tx<tw;++tx) {
        const int ax0=std::clamp(int(std::floor(xs[tx])),0,int(src.width)-1);
        const int ax1=std::clamp(int(std::ceil(xs[tx+1])),ax0+1,int(src.width));
        const int ay0=std::clamp(int(std::floor(ys[ty])),0,int(src.height)-1);
        const int ay1=std::clamp(int(std::ceil(ys[ty+1])),ay0+1,int(src.height));
        float sum[3]={0,0,0};int count=0;
        for(int sy=ay0;sy<ay1;++sy)for(int sx=ax0;sx<ax1;++sx) {
            const auto* p=&src.rgba[(size_t(sy)*src.width+sx)*4];
            for(int k=0;k<3;++k)sum[k]+=p[k];++count;
        }
        auto* out=&tile.rgba[(size_t(ty)*tw+tx)*4];
        for(int k=0;k<3;++k)out[k]=uint8_t(std::clamp(sum[k]/count,0.f,255.f));
    }
    return tile;
}

forge::stbbake::NativeBackgroundTextureProvider nativeLodTextureProvider(const LodAlbedo& albedo) {
    return [albedo](int x,int y,int w,int h,const forge::stbbake::BackgroundBandLockInfo& locks) {
        const auto tile=nativeLodTile(albedo,x,y,w,h,locks);
        forge::stbbake::InlineTexture tex;
        tex.width=uint16_t(tile.width);tex.height=uint16_t(tile.height);
        tex.levels=1;tex.pixelFormat0=1027;tex.surfacePool=1;
        tex.mipData=dxt1::encode(tile.rgba.data(),tile.width,tile.height);
        return tex;
    };
}

forge::stbbake::BackgroundTextureProvider lodTextureProvider(const LodAlbedo& albedo) {
    return [albedo](int x, int y, int w, int h, int texW, int texH) {
        const int tw = texW > 0 ? texW : 64, th = texH > 0 ? texH : 64;
        if (tw % 4 || th % 4) return forge::stbbake::InlineTexture{};
        const auto tile = lodTile(albedo, x, y, w, h, false, tw, th);
        forge::stbbake::InlineTexture tex;
        tex.width = uint16_t(tw); tex.height = uint16_t(th);
        tex.levels = 1;
        tex.pixelFormat0 = 1027; // CPixelFormatInit: DXT1 type3, ColourDepth4
        tex.mipData = dxt1::encode(tile.rgba.data(), tw, th);
        return tex;
    };
}

} // namespace albion::editor
