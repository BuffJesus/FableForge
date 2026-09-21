#include "forge/meshcompose.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include "forge/texturewrite.hpp"

namespace forge::meshcompose {

namespace {

void putU8(std::vector<uint8_t>& o, uint8_t v) { o.push_back(v); }
void putU16(std::vector<uint8_t>& o, uint16_t v) { o.push_back(uint8_t(v)); o.push_back(uint8_t(v >> 8)); }
void putI16(std::vector<uint8_t>& o, int16_t v) { putU16(o, uint16_t(v)); }
void putU32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i))); }
void putI32(std::vector<uint8_t>& o, int32_t v) { putU32(o, uint32_t(v)); }
void putF32(std::vector<uint8_t>& o, float v) { uint32_t u; std::memcpy(&u, &v, 4); putU32(o, u); }
void putStr(std::vector<uint8_t>& o, const std::string& s) { o.insert(o.end(), s.begin(), s.end()); o.push_back(0); }

// one Fable chunk-framed block (EgoCore WriteLZOBlock): LZO, or a stored chunk (u16 0 + raw);
// an empty block is a lone u16 0 (the reader's clen==0 skip)
void lzoBlock(std::vector<uint8_t>& out, const std::vector<uint8_t>& block, bool compress) {
    if (block.empty()) { putU16(out, 0); return; }
    if (compress && block.size() > 3) {
        const auto framed = texturewrite::compressFableBlock(block);
        out.insert(out.end(), framed.begin(), framed.end());
    } else {
        putU16(out, 0);
        out.insert(out.end(), block.begin(), block.end());
    }
}

void writeMaterial(std::vector<uint8_t>& out, int32_t id, const std::string& name, int32_t decal, int32_t diff, int32_t bump,
                   int32_t refl, int32_t illum, int32_t mapFlags, int32_t selfIllum, bool twoSided, bool transparent,
                   bool booleanAlpha, bool degenerate) {
    putI32(out, id);
    putStr(out, name);
    putI32(out, decal); putI32(out, diff); putI32(out, bump); putI32(out, refl); putI32(out, illum); putI32(out, mapFlags); putI32(out, selfIllum);
    putU8(out, twoSided ? 1 : 0); putU8(out, transparent ? 1 : 0); putU8(out, booleanAlpha ? 1 : 0); putU8(out, degenerate ? 1 : 0);
    putU8(out, 0);   // UseFilenames
}

void meshHeader(std::vector<uint8_t>& out, const std::string& name, const Vec3& bsc, float bsr, const Vec3& bbMin, const Vec3& bbMax) {
    putStr(out, name);
    putU8(out, 0);   // AnimatedFlag
    putF32(out, bsc.x); putF32(out, bsc.y); putF32(out, bsc.z);
    putF32(out, bsr);
    putF32(out, bbMin.x); putF32(out, bbMin.y); putF32(out, bbMin.z);
    putF32(out, bbMax.x); putF32(out, bbMax.y); putF32(out, bbMax.z);
    for (int i = 0; i < 5; ++i) putU16(out, 0);   // helper points, dummies, helper name size, volumes, generators
}

void identityRoot(std::vector<uint8_t>& out) {
    const float m[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    for (float v : m) putF32(out, v);
}

void bounds(const std::vector<const Primitive*>& prims, Vec3& mn, Vec3& mx, Vec3& c, float& r) {
    mn = {1e9f, 1e9f, 1e9f}; mx = {-1e9f, -1e9f, -1e9f};
    for (const auto* p : prims)
        for (const auto& v : p->verts) {
            mn.x = std::min(mn.x, v.x); mn.y = std::min(mn.y, v.y); mn.z = std::min(mn.z, v.z);
            mx.x = std::max(mx.x, v.x); mx.y = std::max(mx.y, v.y); mx.z = std::max(mx.z, v.z);
        }
    c = {(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f};
    double r2 = 0;
    for (const auto* p : prims)
        for (const auto& v : p->verts) {
            const double d = double(v.x - c.x) * (v.x - c.x) + double(v.y - c.y) * (v.y - c.y) + double(v.z - c.z) * (v.z - c.z);
            r2 = std::max(r2, d);
        }
    r = float(std::sqrt(r2));
}

std::vector<Vec3> faceNormals(const std::vector<Vec3>& verts, const std::vector<std::array<uint32_t, 3>>& faces) {
    std::vector<Vec3> acc(verts.size());
    for (const auto& f : faces) {
        const Vec3 &a = verts[f[0]], &b = verts[f[1]], &c = verts[f[2]];
        const float e1[3] = {b.x - a.x, b.y - a.y, b.z - a.z}, e2[3] = {c.x - a.x, c.y - a.y, c.z - a.z};
        const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        for (uint32_t i : f) { acc[i].x += n[0]; acc[i].y += n[1]; acc[i].z += n[2]; }
    }
    for (auto& v : acc) {
        const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (l > 1e-12f) { v.x /= l; v.y /= l; v.z /= l; } else v = {0, 0, 1};
    }
    return acc;
}

} // namespace

uint32_t packNormal(Vec3 n) {
    const float l = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (l > 1e-5f) { n.x /= l; n.y /= l; n.z /= l; }
    const int ix = std::clamp(int(std::lround(n.x * 1023.0f)), -1023, 1023);
    const int iy = std::clamp(int(std::lround(n.y * 1023.0f)), -1023, 1023);
    const int iz = std::clamp(int(std::lround(n.z * 511.0f)), -511, 511);
    return (uint32_t(ix) & 0x7FF) | ((uint32_t(iy) & 0x7FF) << 11) | ((uint32_t(iz) & 0x3FF) << 22);
}

int16_t compressUv(float v) {
    return int16_t(std::clamp(long(std::lround((v + 8.0f) * 2048.0f)), -32768L, 32767L));
}

Composed composeStatic(const std::string& name, const std::vector<Primitive>& prims,
                       const std::vector<Material>& materials, bool compress, int physicsIndex) {
    if (name.rfind("MESH_", 0) != 0) throw std::invalid_argument("mesh name must start with MESH_ (the payload classifier)");
    if (prims.empty()) throw std::invalid_argument("at least one primitive required");
    if (materials.empty()) throw std::invalid_argument("at least one material required");
    for (const auto& p : prims) {
        if (p.verts.empty() || p.faces.empty()) throw std::invalid_argument("a primitive has no vertices or faces");
        if (p.verts.size() > 65535) throw std::invalid_argument("vertex overflow: Fable indices are u16 (max 65535 per primitive)");
        for (const auto& f : p.faces) for (uint32_t i : f) if (i >= p.verts.size()) throw std::invalid_argument("face index out of range");
        if (p.material < 0 || size_t(p.material) >= materials.size()) throw std::invalid_argument("primitive material slot out of range");
    }

    std::vector<const Primitive*> all;
    for (const auto& p : prims) all.push_back(&p);
    Composed c;
    bounds(all, c.bbMin, c.bbMax, c.sphereCentre, c.sphereRadius);

    std::vector<uint8_t> lod;
    meshHeader(lod, name, c.sphereCentre, c.sphereRadius, c.bbMin, c.bbMax);
    putI32(lod, int32_t(materials.size()) + 1);   // + the DegenerateTriangles sentinel
    putI32(lod, int32_t(prims.size()));
    putI32(lod, 0); putI32(lod, 0);                // BoneCount, BoneNameSize
    putU8(lod, 0);                                 // ClothFlag
    putU16(lod, uint16_t(prims.size()));           // TotalStaticBlocks
    putU16(lod, 0);                                // TotalAnimatedBlocks
    identityRoot(lod);

    for (size_t slot = 0; slot < materials.size(); ++slot) {
        const auto& m = materials[slot];
        const int32_t mapFlags = (m.diffuseId > 0 ? 1 : 0) | (m.bumpId > 0 ? 2 : 0) | (m.reflectId > 0 ? 4 : 0) | (m.illumId > 0 ? 8 : 0);
        writeMaterial(lod, int32_t(slot), m.name.empty() ? name + "_mat" + std::to_string(slot) : m.name, m.decalId, m.diffuseId, m.bumpId,
                      m.reflectId, m.illumId, mapFlags, m.selfIllum, m.twoSided, m.transparent, m.booleanAlpha, false);
    }
    writeMaterial(lod, 0, "DegenerateTriangles", 0, 0, 0, 0, 0, 0, 0, false, false, false, true);

    for (const auto& p : prims) {
        Vec3 pmn, pmx, psc; float psr;
        bounds({&p}, pmn, pmx, psc, psr);
        const auto norms = p.normals.empty() ? faceNormals(p.verts, p.faces) : p.normals;
        const uint32_t nv = uint32_t(p.verts.size()), nf = uint32_t(p.faces.size());
        putI32(lod, p.material); putI32(lod, 0);            // MaterialIndex, Reps
        putF32(lod, psc.x); putF32(lod, psc.y); putF32(lod, psc.z);
        putF32(lod, psr); putF32(lod, 0.1f);                // radius, AverageStretch
        putU32(lod, 1); putU32(lod, 0); putU32(lod, nv); putU32(lod, nf); putU32(lod, 3 * nf); putU32(lod, 0x14);   // static blocks, animated blocks, verts, faces, indices, InitFlags
        putU32(lod, 1); putU32(lod, 0);                     // the counts repeated
        putU32(lod, nf); putU32(lod, 0); putU8(lod, 0); putU8(lod, 0); putU8(lod, 0); putI32(lod, p.material);   // one CStaticBlock: the whole index buffer as a list
        const float scale[4] = {1, 1, 1, 1}, off[4] = {0, 0, 0, 0};
        for (float v : scale) putF32(lod, v);
        for (float v : off) putF32(lod, v);
        putU32(lod, 20); putU32(lod, 0);                    // stride, BufferType

        std::vector<uint8_t> vb;
        vb.reserve(size_t(nv) * 20);
        for (uint32_t i = 0; i < nv; ++i) {
            const Vec3& v = p.verts[i];
            const Vec2 uv = i < p.uvs.size() ? p.uvs[i] : Vec2{};
            putF32(vb, v.x); putF32(vb, v.y); putF32(vb, v.z);
            putU32(vb, packNormal(i < norms.size() ? norms[i] : Vec3{0, 0, 1}));
            putI16(vb, compressUv(uv.u));
            putI16(vb, compressUv(1.0f - uv.v));            // the decoder yields 1-v: store it back
        }
        lzoBlock(lod, vb, compress);
        std::vector<uint8_t> ib;
        ib.reserve(size_t(nf) * 6);
        for (const auto& f : p.faces) { putU16(ib, uint16_t(f[0])); putU16(ib, uint16_t(f[2])); putU16(ib, uint16_t(f[1])); }   // the decoder emits (k, k+2, k+1)
        lzoBlock(lod, ib, compress);
        putU32(lod, 0);                                     // ClothPrimitiveCount
        c.vertices += nv; c.triangles += nf;
    }

    // the retail ghost LOD: a header with nothing in it
    std::vector<uint8_t> ghost;
    meshHeader(ghost, name, c.sphereCentre, c.sphereRadius, c.bbMin, c.bbMax);
    putI32(ghost, 0); putI32(ghost, 0); putI32(ghost, 0); putI32(ghost, 0);
    putU8(ghost, 0); putU16(ghost, 0); putU16(ghost, 0);
    identityRoot(ghost);

    c.payload = lod;
    c.payload.insert(c.payload.end(), ghost.begin(), ghost.end());

    std::vector<int32_t> texIds;
    for (const auto& m : materials)
        for (int32_t t : {m.diffuseId, m.bumpId, m.reflectId, m.illumId, m.decalId})
            if (t > 0 && std::find(texIds.begin(), texIds.end(), t) == texIds.end()) texIds.push_back(t);
    auto& info = c.info;
    putI32(info, physicsIndex);
    putF32(info, c.sphereCentre.x); putF32(info, c.sphereCentre.y); putF32(info, c.sphereCentre.z);
    putF32(info, c.sphereRadius);
    putF32(info, c.bbMin.x); putF32(info, c.bbMin.y); putF32(info, c.bbMin.z);
    putF32(info, c.bbMax.x); putF32(info, c.bbMax.y); putF32(info, c.bbMax.z);
    putU32(info, 1);                                        // LODCount
    putU32(info, uint32_t(lod.size()));                     // LODSizes[0] (the ghost is not counted)
    putF32(info, 0.0f);                                     // SafeBoundingRadius
    putU32(info, uint32_t(texIds.size()));
    for (int32_t t : texIds) putI32(info, t);
    return c;
}

} // namespace forge::meshcompose
