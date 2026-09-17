#include "lodbake.hpp"

#include <algorithm>
#include <cmath>

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

forge::stbbake::BackgroundTextureProvider lodTextureProvider(const LodAlbedo& albedo) {
    return [albedo](int x, int y, int w, int h, int texW, int texH) {
        const int tw = texW > 0 ? texW : 64, th = texH > 0 ? texH : 64;
        if (tw % 4 || th % 4) return forge::stbbake::InlineTexture{};
        const auto tile = lodTile(albedo, x, y, w, h, false, tw, th);
        forge::stbbake::InlineTexture tex;
        tex.width = uint16_t(tw); tex.height = uint16_t(th);
        tex.levels = 1;
        tex.pixelFormat0 = 3;   // the DXT1 fourcc path (see singleLevelBackgroundTexture)
        tex.mipData = dxt1::encode(tile.rgba.data(), tw, th);
        return tex;
    };
}

} // namespace albion::editor
