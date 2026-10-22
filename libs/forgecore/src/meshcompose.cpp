#include "forge/meshcompose.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>

#include "forge/lzo.hpp"
#include "forge/texturewrite.hpp"

namespace forge::meshcompose {

namespace {

void putU8(std::vector<uint8_t>& o, uint8_t v) { o.push_back(v); }
void putU16(std::vector<uint8_t>& o, uint16_t v) { o.push_back(uint8_t(v)); o.push_back(uint8_t(v >> 8)); }
void putI16(std::vector<uint8_t>& o, int16_t v) { putU16(o, uint16_t(v)); }
void putU32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i))); }
void putI32(std::vector<uint8_t>& o, int32_t v) { putU32(o, uint32_t(v)); }
void putF32(std::vector<uint8_t>& o, float v) { uint32_t u; std::memcpy(&u, &v, 4); putU32(o, u); }
void putStr(std::vector<uint8_t>& o, const std::string& s) {
    if(s.find('\0')!=std::string::npos) throw std::invalid_argument("embedded NUL in mesh name or material name");
    o.insert(o.end(), s.begin(), s.end()); o.push_back(0);
}

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

void writeMaterial(std::vector<uint8_t>& out, int32_t id, const std::string& name, int32_t decal, uint32_t diff, uint32_t bump,
                   uint32_t refl, uint32_t illum, int32_t mapFlags, int32_t selfIllum, bool twoSided, bool transparent,
                   bool booleanAlpha, bool degenerate,
                   const std::optional<std::array<uint8_t,5>>& nativeFlags={},
                   const std::array<std::string,4>& filenames={}) {
    putI32(out, id);
    putStr(out, name);
    putI32(out, decal); putU32(out, diff); putU32(out, bump); putU32(out, refl); putU32(out, illum); putI32(out, mapFlags); putI32(out, selfIllum);
    if(nativeFlags) {
        out.insert(out.end(),nativeFlags->begin(),nativeFlags->end());
        if((*nativeFlags)[4]) for(const auto& filename:filenames) putStr(out,filename);
    } else {
        putU8(out, twoSided ? 1 : 0); putU8(out, transparent ? 1 : 0); putU8(out, booleanAlpha ? 1 : 0); putU8(out, degenerate ? 1 : 0);
        putU8(out, 0);   // UseFilenames
    }
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
    const float limit=std::numeric_limits<float>::max();
    mn = {limit,limit,limit}; mx = {-limit,-limit,-limit};
    for (const auto* p : prims)
        for (const auto& v : p->verts) {
            if(!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
                throw std::invalid_argument("non-finite mesh position");
            mn.x = std::min(mn.x, v.x); mn.y = std::min(mn.y, v.y); mn.z = std::min(mn.z, v.z);
            mx.x = std::max(mx.x, v.x); mx.y = std::max(mx.y, v.y); mx.z = std::max(mx.z, v.z);
        }
    c = {float((double(mn.x)+mx.x)*0.5),float((double(mn.y)+mx.y)*0.5),float((double(mn.z)+mx.z)*0.5)};
    double r2 = 0;
    for (const auto* p : prims)
        for (const auto& v : p->verts) {
            const double dx=double(v.x)-c.x,dy=double(v.y)-c.y,dz=double(v.z)-c.z;
            r2 = std::max(r2, dx*dx+dy*dy+dz*dz);
        }
    const double radius=std::sqrt(r2);
    if(radius>limit) throw std::invalid_argument("mesh bounding sphere exceeds float range");
    r = float(radius);
}

std::vector<Vec3> faceNormals(const std::vector<Vec3>& verts, const std::vector<std::array<uint32_t, 3>>& faces) {
    std::vector<std::array<double,3>> acc(verts.size());
    for (const auto& f : faces) {
        const Vec3 &a = verts[f[0]], &b = verts[f[1]], &c = verts[f[2]];
        const double e1[3] = {double(b.x)-a.x,double(b.y)-a.y,double(b.z)-a.z};
        const double e2[3] = {double(c.x)-a.x,double(c.y)-a.y,double(c.z)-a.z};
        const double n[3] = {e1[1]*e2[2]-e1[2]*e2[1],e1[2]*e2[0]-e1[0]*e2[2],e1[0]*e2[1]-e1[1]*e2[0]};
        for (uint32_t i : f) for(size_t axis=0;axis<3;++axis) acc[i][axis]+=n[axis];
    }
    std::vector<Vec3> normals;normals.reserve(verts.size());
    for (const auto& v : acc) {
        const double length=std::hypot(v[0],v[1],v[2]);
        normals.push_back(length>1e-12 ? Vec3{float(v[0]/length),float(v[1]/length),float(v[2]/length)} : Vec3{0,0,1});
    }
    return normals;
}

} // namespace

uint32_t packNormal(Vec3 n) {
    const double length=std::hypot(double(n.x),double(n.y),double(n.z));
    if(!std::isfinite(length)) throw std::invalid_argument("non-finite mesh normal");
    if(length>1e-5f) {n.x=float(n.x/length);n.y=float(n.y/length);n.z=float(n.z/length);}
    const int ix = std::clamp(int(std::lround(n.x * 1023.0f)), -1023, 1023);
    const int iy = std::clamp(int(std::lround(n.y * 1023.0f)), -1023, 1023);
    const int iz = std::clamp(int(std::lround(n.z * 511.0f)), -511, 511);
    return (uint32_t(ix) & 0x7FF) | ((uint32_t(iy) & 0x7FF) << 11) | ((uint32_t(iz) & 0x3FF) << 22);
}

int16_t compressUv(float v) {
    if (!std::isfinite(v)) throw std::invalid_argument("non-finite mesh texture coordinate");
    const double scaled=(double(v)+8.0)*2048.0;
    return int16_t(std::lround(std::clamp(scaled,-32768.0,32767.0)));
}

bool uvNeedsClamping(float v) {
    if (!std::isfinite(v)) throw std::invalid_argument("non-finite mesh texture coordinate");
    const double rounded=std::round((double(v)+8.0)*2048.0);
    return rounded < -32768.0 || rounded > 32767.0;
}

Composed composeStatic(const std::string& name, const std::vector<Primitive>& prims,
                       const std::vector<Material>& materials, bool compress, uint32_t physicsIndex,bool appendDegenerateMaterial) {
    if (physicsIndex == std::numeric_limits<uint32_t>::max())
        throw std::invalid_argument("physics entry ID uses the reserved no-hull sentinel");
    if (name.rfind("MESH_", 0) != 0) throw std::invalid_argument("mesh name must start with MESH_ (the payload classifier)");
    if (prims.empty()) throw std::invalid_argument("at least one primitive required");
    if (prims.size()>std::numeric_limits<uint16_t>::max())
        throw std::invalid_argument("static mesh exceeds 65535 primitive blocks");
    if (materials.empty()) throw std::invalid_argument("at least one material required");
    if(!appendDegenerateMaterial && (!materials.back().nativeFlags || !(*materials.back().nativeFlags)[3]))
        throw std::invalid_argument("preserved static materials require a final degenerate sentinel");
    for (const auto& p : prims) {
        if (p.verts.empty() || p.faces.empty()) throw std::invalid_argument("a primitive has no vertices or faces");
        if (p.verts.size() > maxPrimitiveVertices) throw std::invalid_argument("vertex overflow: Fable indices are u16 (max 65535 per primitive)");
        for (const auto& f : p.faces) for (uint32_t i : f) if (i >= p.verts.size()) throw std::invalid_argument("face index out of range");
        if (p.material < 0 || size_t(p.material) >= materials.size()) throw std::invalid_argument("primitive material slot out of range");
    }

    std::vector<const Primitive*> all;
    for (const auto& p : prims) all.push_back(&p);
    Composed c;
    bounds(all, c.bbMin, c.bbMax, c.sphereCentre, c.sphereRadius);

    std::vector<uint8_t> lod;
    meshHeader(lod, name, c.sphereCentre, c.sphereRadius, c.bbMin, c.bbMax);
    putI32(lod, int32_t(materials.size()) + int32_t(appendDegenerateMaterial));
    putI32(lod, int32_t(prims.size()));
    putI32(lod, 0); putI32(lod, 0);                // BoneCount, BoneNameSize
    putU8(lod, 0);                                 // ClothFlag
    putU16(lod, uint16_t(prims.size()));           // TotalStaticBlocks
    putU16(lod, 0);                                // TotalAnimatedBlocks
    identityRoot(lod);

    for (size_t slot = 0; slot < materials.size(); ++slot) {
        const auto& m = materials[slot];
        const uint32_t mapFlags=m.originalMapFlags.value_or((m.diffuseId > 0 ? 1u : 0u) | (m.bumpId > 0 ? 2u : 0u) | (m.reflectId > 0 ? 4u : 0u) | (m.illumId > 0 ? 8u : 0u));
        writeMaterial(lod, m.originalId.value_or(int32_t(slot)), (!m.originalId && m.name.empty()) ? name + "_mat" + std::to_string(slot) : m.name, m.decalId, m.diffuseId, m.bumpId,
                      m.reflectId, m.illumId, int32_t(mapFlags), m.selfIllum, m.twoSided, m.transparent, m.booleanAlpha, false,m.nativeFlags,m.filenames);
    }
    if(appendDegenerateMaterial) writeMaterial(lod, 0, "DegenerateTriangles", 0, 0, 0, 0, 0, 0, 0, false, false, false, true);

    for (const auto& p : prims) {
        Vec3 pmn, pmx, psc; float psr;
        bounds({&p}, pmn, pmx, psc, psr);
        const auto norms = p.normals.empty() ? faceNormals(p.verts, p.faces) : p.normals;
        const uint32_t nv = uint32_t(p.verts.size()), nf = uint32_t(p.faces.size());
        const int32_t materialId=materials[size_t(p.material)].originalId.value_or(p.material);
        putI32(lod, materialId); putI32(lod, 0);            // MaterialIndex, Reps
        putF32(lod, psc.x); putF32(lod, psc.y); putF32(lod, psc.z);
        putF32(lod, psr); putF32(lod, 0.1f);                // radius, AverageStretch
        putU32(lod, 1); putU32(lod, 0); putU32(lod, nv); putU32(lod, nf); putU32(lod, 3 * nf); putU32(lod, 0x14);   // static blocks, animated blocks, verts, faces, indices, InitFlags
        putU32(lod, 1); putU32(lod, 0);                     // the counts repeated
        putU32(lod, nf); putU32(lod, 0); putU8(lod, 0); putU8(lod, 0); putU8(lod, 0); putI32(lod, p.material);   // block references the material array slot, unlike the primitive's material ID
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

    std::vector<uint32_t> texIds;
    for (const auto& m : materials)
        for (uint32_t t : {m.diffuseId, m.bumpId, m.reflectId, m.illumId, m.decalId>0 ? uint32_t(m.decalId) : 0u})
            if (t > 0 && std::find(texIds.begin(), texIds.end(), t) == texIds.end()) texIds.push_back(t);
    auto& info = c.info;
    putU32(info, physicsIndex);
    putF32(info, c.sphereCentre.x); putF32(info, c.sphereCentre.y); putF32(info, c.sphereCentre.z);
    putF32(info, c.sphereRadius);
    putF32(info, c.bbMin.x); putF32(info, c.bbMin.y); putF32(info, c.bbMin.z);
    putF32(info, c.bbMax.x); putF32(info, c.bbMax.y); putF32(info, c.bbMax.z);
    putU32(info, 1);                                        // LODCount
    putU32(info, uint32_t(lod.size()));                     // LODSizes[0] (the ghost is not counted)
    putF32(info, 0.0f);                                     // SafeBoundingRadius
    putU32(info, uint32_t(texIds.size()));
    for (uint32_t t : texIds) putU32(info, t);
    return c;
}

namespace {

// EgoCore's ChunkWriter: TAG, u32 size placeholder, body, size patched on pop
struct ChunkWriter {
    std::vector<uint8_t> buf;
    std::vector<size_t> stack;
    void raw(const void* d, size_t n) { const auto* b = static_cast<const uint8_t*>(d); buf.insert(buf.end(), b, b + n); }
    void tag(const char* t) { raw(t, 4); }
    void u32(uint32_t v) { putU32(buf, v); }
    void i32(int32_t v) { putI32(buf, v); }
    void str(const std::string& s) { putStr(buf, s); }
    void push(const char* t) { tag(t); stack.push_back(buf.size()); u32(0); }
    void pop() { const size_t at = stack.back(); stack.pop_back(); const uint32_t n = uint32_t(buf.size() - at - 4); std::memcpy(buf.data() + at, &n, 4); }
    void align() { while (buf.size() % 4) buf.push_back(0); }
};

} // namespace

std::vector<uint8_t> composePhysicsUncompressed(const std::vector<Primitive>& prims, const std::string& subMeshName) {
    // This composer joins all parts into one hull with uint16 triangle indices.
    constexpr size_t vertexLimit=size_t(std::numeric_limits<uint16_t>::max())+1;
    size_t totalVertices=0;
    for(const auto& primitive:prims) {
        if(primitive.verts.size()>vertexLimit-totalVertices)
            throw std::invalid_argument("collision hull exceeds 65536 vertices; simplify the model or disable collision");
        totalVertices+=primitive.verts.size();
        for(const auto& vertex:primitive.verts)
            if(!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z))
                throw std::invalid_argument("non-finite collision position");
        for(const auto& face:primitive.faces) for(uint32_t index:face)
            if(index>=primitive.verts.size()) throw std::invalid_argument("collision face index out of range");
    }
    // retail's hull byte for byte in structure (MESH_OBJECT_BARREL[PHYSICS] decoded with mesh-info --raw):
    // MTRL "Collision material" + an MTLE {1, 0} sub-chunk, the SUBM named after the mesh, no TRFM
    ChunkWriter w;
    w.tag(">>>>"); w.tag("3DMF"); w.u32(100);
    w.str("Copyright Big Blue Box Studios Ltd.");
    w.align();
    w.push("3DRT");
    w.push("MTLS");
    w.push("MTRL");
    w.str("Collision material");
    w.u32(0);
    w.buf.push_back(0);   // two-sided
    const uint32_t colours[6] = {0xFF96EFF7, 0xFF96EFF7, 0xFFE5E5E5, 0x3DCCCCCC, 0x00000000, 0x3E99999A};
    for (uint32_t c : colours) w.u32(c);
    w.push("MTLE"); w.u32(1); w.u32(0); w.pop();
    w.pop();
    w.pop();
    w.push("SUBM");
    w.str(subMeshName.empty() ? std::string("collision") : subMeshName);
    w.u32(0); w.i32(-1); w.i32(-1); w.i32(-1);   // sub-mesh index, parent, first child, next sibling
    w.push("PRIM");
    w.i32(0);   // physics material index
    // Every primitive's triangles share one vertex/index set; limits were checked above.
    std::vector<uint16_t> indices;
    std::vector<uint8_t> verts;
    uint32_t base = 0, count = 0;
    for (const auto& p : prims) {
        const auto norms = p.normals.empty() ? std::vector<Vec3>(p.verts.size(), Vec3{0, 0, 1}) : p.normals;
        // the engine's winding, like the render index buffer (retail hull triples read 2,1,0 / 5,4,3):
        // straight order made an inside-out hull that held the hero at the cube's centre in-game
        for (const auto& f : p.faces) for (uint32_t i : {f[0], f[2], f[1]}) indices.push_back(uint16_t(base + i));
        for (size_t i = 0; i < p.verts.size(); ++i) {
            putF32(verts, p.verts[i].x); putF32(verts, p.verts[i].y); putF32(verts, p.verts[i].z);
            const Vec3 n = i < norms.size() ? norms[i] : Vec3{0, 0, 1};
            putF32(verts, n.x); putF32(verts, n.y); putF32(verts, n.z);
            const Vec2 uv = i < p.uvs.size() ? p.uvs[i] : Vec2{};
            putF32(verts, uv.u); putF32(verts, uv.v);
        }
        base += uint32_t(p.verts.size()); count += uint32_t(p.verts.size());
    }
    // retail's chunk order inside PRIM: TRIS, SMTH (a smoothing group per triangle, all 0 in
    // retail hulls), VERT, UNIV (per vertex: a stable ID for each distinct position);
    // EgoCore writes TRIS + VERT only -- the two extra chunks are what retail carries
    if (!indices.empty()) {
        w.push("TRIS");
        w.u32(uint32_t(indices.size() / 3));
        for (uint16_t i : indices) putU16(w.buf, i);
        w.pop();
        w.push("SMTH");
        w.u32(uint32_t(indices.size() / 3));
        for (size_t i = 0; i < indices.size() / 3; ++i) w.u32(0);
        w.pop();
    }
    if (count) {
        w.push("VERT");
        w.u32(count);
        w.raw(verts.data(), verts.size());
        w.pop();
        w.push("UNIV");
        w.u32(count);
        std::vector<uint32_t> uniq;
        uniq.reserve(count);
        {
            // Preserve first-encounter numbering and exact coordinate equality
            // (including signed zero), without rescanning all previous vertices.
            std::map<std::array<float,3>,uint32_t> seen;
            for (const auto& p : prims)
                for (const auto& v : p.verts) {
                    const auto found=seen.try_emplace(std::array<float,3>{v.x,v.y,v.z},uint32_t(seen.size()));
                    uniq.push_back(found.first->second);
                }
        }
        for (uint32_t u : uniq) w.u32(u);
        w.pop();
    }
    w.pop();   // PRIM
    w.pop();   // SUBM
    w.pop();   // 3DRT
    w.u32(0);  // end marker
    return w.buf;
}

std::vector<uint8_t> composePhysics(const std::vector<Primitive>& prims, const std::string& subMeshName) {
    const auto plain = composePhysicsUncompressed(prims, subMeshName);
    std::vector<uint8_t> out;
    putU32(out, uint32_t(plain.size()));
    const auto lzo = forge::lzo::compress(plain);
    if (lzo.empty()) out.insert(out.end(), plain.begin(), plain.end());
    else out.insert(out.end(), lzo.begin(), lzo.end());
    return out;
}

} // namespace forge::meshcompose
