#include "meshimport.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <stdexcept>

#include "nlohmann/json.hpp"

#include "../vendor/embedded_schema.hpp"
#include "backups.hpp"
#include "pendingbanks.hpp"
#include "forge/big.hpp"
#include "forge/bin.hpp"
#include "forge/defedit.hpp"
#include "forge/defschema.hpp"
#include "forge/meshpreview.hpp"
#include "forge/terraintex.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;
using forge::meshcompose::Primitive;
using forge::meshcompose::Vec2;
using forge::meshcompose::Vec3;

namespace albion::meshimport {

namespace {

std::vector<uint8_t> readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + p.string());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// glTF Y-up metres -> Fable Z-up centimetres: the inverse of the exporter's (x, z, -y) and its
// 0.01 mesh scale (retail's barrel is z 0..132 in its mesh; 1 world unit = 1 m = 100 mesh units)
constexpr float kMetresToMesh = 100.0f;
Vec3 toFable(float x, float y, float z) {
    const Vec3 result{x * kMetresToMesh, -z * kMetresToMesh, y * kMetresToMesh};
    if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z))
        throw std::runtime_error("non-finite mesh position after transform/metre conversion");
    return result;
}
Vec3 toFableDir(float x, float y, float z) { return {x, -z, y}; }

// ---------------------------------------------------------------- glTF 2.0

struct GltfBuffers { json doc; std::vector<std::vector<uint8_t>> buffers; };

size_t gltfInteger(const json& value, const char* key) {
    // glTF integer properties may use decimal/exponent JSON notation. Check
    // the value before conversion; double(SIZE_MAX) rounds up on 64-bit hosts.
    if(value.is_number_float()) {
        const double number=value.get<double>();
        if(!std::isfinite(number) || number<0 || std::floor(number)!=number ||
           number>=std::ldexp(1.0,std::numeric_limits<size_t>::digits))
            throw std::runtime_error(std::string("invalid nonnegative integer: ")+key);
        return size_t(number);
    }
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<int64_t>() < 0))
        throw std::runtime_error(std::string("invalid nonnegative integer: ") + key);
    const auto number = value.get<uint64_t>();
    if (number > std::numeric_limits<size_t>::max()) throw std::runtime_error(std::string("size overflow: ") + key);
    return size_t(number);
}
size_t gltfSize(const json& object, const char* key, size_t fallback = 0) {
    return object.contains(key) ? gltfInteger(object.at(key), key) : fallback;
}

std::array<uint32_t,2> gltfVersion(const json& asset,const char* key) {
    if(!asset.contains(key) || !asset.at(key).is_string())
        throw std::runtime_error(std::string("invalid glTF asset version field: ")+key);
    const auto& version=asset.at(key).get_ref<const std::string&>();
    const size_t dot=version.find('.');
    if(dot==std::string::npos || dot==0 || dot+1==version.size())
        throw std::runtime_error(std::string("invalid glTF asset version field: ")+key);
    std::array<uint32_t,2> result{};
    const char* begin=version.data();const char* end=begin+version.size();
    const auto major=std::from_chars(begin,begin+dot,result[0]);
    const auto minor=std::from_chars(begin+dot+1,end,result[1]);
    if(major.ec!=std::errc{} || major.ptr!=begin+dot || minor.ec!=std::errc{} || minor.ptr!=end)
        throw std::runtime_error(std::string("invalid glTF asset version field: ")+key);
    return result;
}

std::string decodeUriEscapes(std::string_view text) {
    auto hex=[](char c) {
        if(c>='0' && c<='9')return c-'0';
        if(c>='a' && c<='f')return c-'a'+10;
        if(c>='A' && c<='F')return c-'A'+10;
        return -1;
    };
    std::string decoded;decoded.reserve(text.size());
    for(size_t i=0;i<text.size();++i) {
        if(text[i]!='%') {decoded.push_back(text[i]);continue;}
        if(text.size()-i<3 || hex(text[i+1])<0 || hex(text[i+2])<0)
            throw std::runtime_error("bad glTF buffer URI escape");
        decoded.push_back(char((hex(text[i+1])<<4)|hex(text[i+2])));i+=2;
    }
    return decoded;
}

std::vector<uint8_t> decodeBufferUri(const std::string& uri) {
    const size_t comma=uri.find(',');
    if(comma==std::string::npos) throw std::runtime_error("bad glTF buffer data URI: missing comma");
    std::string header=uri.substr(5,comma-5);
    std::transform(header.begin(),header.end(),header.begin(),[](unsigned char c){return char(std::tolower(c));});
    const auto media=header.substr(0,header.find(';'));
    if((media!="application/octet-stream" && media!="application/gltf-buffer") || !header.ends_with(";base64"))
        throw std::runtime_error("unsupported glTF buffer data URI type or encoding");
    const auto encoded=decodeUriEscapes(std::string_view(uri).substr(comma+1));
    if(encoded.size()%4) throw std::runtime_error("bad glTF buffer base64 length");
    size_t padding=0;
    if(!encoded.empty() && encoded.back()=='=') {
        padding=1;
        if(encoded[encoded.size()-2]=='=')padding=2;
    }
    static const std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> out;out.reserve(encoded.size()/4*3);
    uint32_t value=0;int bits=-8;
    for(size_t i=0;i<encoded.size()-padding;++i) {
        const size_t digit=alphabet.find(encoded[i]);
        if(digit==std::string::npos) throw std::runtime_error("bad glTF buffer base64 character or padding");
        value=(value<<6)|uint32_t(digit);bits+=6;
        if(bits>=0) {out.push_back(uint8_t((value>>bits)&0xff));bits-=8;}
    }
    if((padding==1 && (value&3)) || (padding==2 && (value&15)))
        throw std::runtime_error("bad glTF buffer base64 padding bits");
    return out;
}

GltfBuffers openGltf(const fs::path& path) {
    GltfBuffers g;
    const auto bytes = readFile(path);
    std::vector<uint8_t> glbBin;
    const bool glb = bytes.size() >= 4 && std::memcmp(bytes.data(), "glTF", 4) == 0;
    if (glb) {
        // Khronos GLB 2.0: 12-byte header, JSON first, optional BIN second,
        // then unknown extension chunks. Validate ranges before reading them.
        if (bytes.size() < 20) throw std::runtime_error("malformed .glb (incomplete header or JSON chunk header)");
        auto u32 = [&](size_t o) {
            return uint32_t(bytes[o]) | (uint32_t(bytes[o + 1]) << 8) |
                   (uint32_t(bytes[o + 2]) << 16) | (uint32_t(bytes[o + 3]) << 24);
        };
        if (u32(4) != 2) throw std::runtime_error("unsupported .glb container version (expected 2)");
        if (u32(8) != bytes.size()) throw std::runtime_error("malformed .glb (declared length differs from file size)");
        size_t offset = 12, chunkIndex = 0;
        while (offset < bytes.size()) {
            if (bytes.size() - offset < 8) throw std::runtime_error("malformed .glb (incomplete chunk header)");
            const size_t length = u32(offset);
            const uint32_t type = u32(offset + 4);
            offset += 8;
            if (length % 4 || length > bytes.size() - offset)
                throw std::runtime_error("malformed .glb (unaligned or truncated chunk)");
            if (chunkIndex == 0) {
                if (type != 0x4E4F534A) throw std::runtime_error("malformed .glb (first chunk is not JSON)");
                g.doc = json::parse(bytes.begin() + offset, bytes.begin() + offset + length);
            } else if (type == 0x4E4F534A) {
                throw std::runtime_error("malformed .glb (duplicate JSON chunk)");
            } else if (type == 0x004E4942) {
                if (chunkIndex != 1) throw std::runtime_error("malformed .glb (BIN must be the second chunk)");
                glbBin.assign(bytes.begin() + offset, bytes.begin() + offset + length);
            }
            offset += length;
            ++chunkIndex;
        }
    } else {
        g.doc = json::parse(bytes.begin(), bytes.end());
    }
    if(!g.doc.is_object() || !g.doc.contains("asset") || !g.doc["asset"].is_object())
        throw std::runtime_error("missing glTF asset version object");
    const auto& asset=g.doc["asset"];
    if(gltfVersion(asset,"version")[0]!=2)
        throw std::runtime_error("unsupported glTF asset version (expected major version 2)");
    if(asset.contains("minVersion") && gltfVersion(asset,"minVersion")!=std::array<uint32_t,2>{2,0})
        throw std::runtime_error("unsupported glTF asset minimum version (importer supports 2.0)");
    // This static importer implements core glTF only. Optional extensions can
    // use their core fallback; required extensions must never be silently lost.
    auto extensionNames = [&](const char* key) {
        std::vector<std::string> names;
        if (!g.doc.contains(key)) return names;
        const auto& list = g.doc.at(key);
        if (!list.is_array() || list.empty())
            throw std::runtime_error(std::string("invalid glTF extension list: ") + key);
        for (const auto& item : list) {
            if (!item.is_string() || item.get_ref<const std::string&>().empty())
                throw std::runtime_error(std::string("invalid glTF extension name in ") + key);
            const auto name = item.get<std::string>();
            if (std::find(names.begin(), names.end(), name) != names.end())
                throw std::runtime_error(std::string("duplicate glTF extension in ") + key + ": " + name);
            names.push_back(name);
        }
        return names;
    };
    const auto used = extensionNames("extensionsUsed");
    const auto required = extensionNames("extensionsRequired");
    for (const auto& name : required) {
        if (std::find(used.begin(), used.end(), name) == used.end())
            throw std::runtime_error("required glTF extension missing from extensionsUsed: " + name);
        throw std::runtime_error("unsupported required glTF extension: " + name);
    }
    for (const auto& b : g.doc.value("buffers", json::array())) {
        if (b.contains("uri")) {
            const std::string uri = b["uri"].get<std::string>();
            std::string scheme=uri.substr(0,5);
            std::transform(scheme.begin(),scheme.end(),scheme.begin(),[](unsigned char c){return char(std::tolower(c));});
            if (scheme=="data:") {
                g.buffers.push_back(decodeBufferUri(uri));
            } else {
                const auto decoded=decodeUriEscapes(uri);
                if(decoded.find('\0')!=std::string::npos) throw std::runtime_error("NUL in glTF buffer URI path");
                g.buffers.push_back(readFile(path.parent_path() / fs::u8path(decoded)));
            }
        } else {
            if(!glb || !g.buffers.empty() || glbBin.empty())
                throw std::runtime_error("glTF buffer without URI has no supported data source (only buffer 0 may use GLB BIN)");
            g.buffers.push_back(glbBin);
        }
        const size_t length = gltfSize(b, "byteLength");
        if (!length || length > g.buffers.back().size()) throw std::runtime_error("glTF buffer has an invalid declared length");
        // Padding and extra external-file bytes are not part of the logical buffer.
        g.buffers.back().resize(length);
    }
    return g;
}

// Decode a validated source window. Sparse indices/values are tightly packed;
// ordinary accessor bases may be interleaved. Never allocate before range checks.
template<typename Value>
std::vector<Value> readElements(const GltfBuffers& g,const json& acc,size_t count,
                              size_t ct,int comps,bool norm,bool sparse) {
    if(!acc.contains("bufferView")) throw std::runtime_error("missing glTF accessor bufferView");
    const auto& bv = g.doc.at("bufferViews").at(gltfSize(acc, "bufferView"));
    if(sparse && (bv.contains("byteStride") || bv.contains("target")))
        throw std::runtime_error("sparse buffer view must not define byteStride or target");
    // glTF 2.0 bufferView.byteStride is only for vertex attributes. Index
    // accessors stay tightly packed even if an explicit stride equals their size.
    if constexpr(std::is_same_v<Value,uint32_t>) {
        if (!sparse && bv.contains("byteStride"))
            throw std::runtime_error("triangle index buffer view must not define byteStride");
    }
    const auto& buf = g.buffers.at(gltfSize(bv, "buffer"));
    const size_t csize = ct == 5126 || ct == 5125 ? 4 : ct == 5123 || ct == 5122 ? 2 : 1;
    const size_t element = csize * size_t(comps);
    const size_t stride = gltfSize(bv, "byteStride", element);
    const size_t viewOffset = gltfSize(bv, "byteOffset"), viewLength = gltfSize(bv, "byteLength");
    const size_t accessorOffset = gltfSize(acc, "byteOffset");
    if (!viewLength || viewOffset > buf.size() || viewLength > buf.size() - viewOffset)
        throw std::runtime_error("buffer view reads past its buffer");
    if (stride < element || stride % csize || (bv.contains("byteStride") && (stride < 4 || stride > 252 || stride % 4)))
        throw std::runtime_error("invalid accessor stride");
    if (accessorOffset % csize || viewOffset % csize)
        throw std::runtime_error("unaligned accessor offset");
    if constexpr(!std::is_same_v<Value,uint32_t>) {
        // Consumed dense vertex attributes must align each element inside
        // their view to four bytes (glTF 2.0 section 3.6.2.4).
        if (!sparse && (accessorOffset % 4 || (count > 1 && stride % 4)))
            throw std::runtime_error("vertex attribute elements must be four-byte aligned inside their buffer view");
    }
    if (accessorOffset > viewLength || element > viewLength - accessorOffset ||
        count - 1 > (viewLength - accessorOffset - element) / stride)
        throw std::runtime_error("accessor reads past its buffer view");
    const size_t base = viewOffset + accessorOffset;
    std::vector<Value> out(count * size_t(comps), Value{});
    for (size_t i = 0; i < count; ++i) {
        for (int c = 0; c < comps; ++c) {
            const size_t o = base + i * stride + size_t(c) * csize;
            if (o + csize > buf.size()) throw std::runtime_error("accessor reads past its buffer");
            double v = 0; // All uint32 values are exact here; never route indices through float.
            switch (ct) {
                case 5126: { float f; std::memcpy(&f, buf.data() + o, 4); v = f; break; }
                case 5125: { uint32_t u; std::memcpy(&u, buf.data() + o, 4); v = u; break; }
                case 5123: { uint16_t u; std::memcpy(&u, buf.data() + o, 2); v = norm ? u / 65535.0f : float(u); break; }
                case 5122: { int16_t s; std::memcpy(&s, buf.data() + o, 2); v = norm ? std::max(s / 32767.0f, -1.0f) : float(s); break; }
                case 5121: { v = norm ? buf[o] / 255.0f : float(buf[o]); break; }
                case 5120: { const int8_t s = int8_t(buf[o]); v = norm ? std::max(s / 127.0f, -1.0f) : float(s); break; }
                default: throw std::runtime_error("unsupported componentType " + std::to_string(ct));
            }
            if (!std::isfinite(v)) throw std::runtime_error("non-finite glTF accessor value");
            out[i * size_t(comps) + size_t(c)] = Value(v);
        }
    }
    return out;
}

// Attributes use floats; triangle indices retain their unsigned integer values.
template<typename Value = float>
std::vector<Value> readAccessor(const GltfBuffers& g, size_t index, int& comps) {
    const auto& acc=g.doc.at("accessors").at(index);
    const std::string type=acc.at("type").get<std::string>();
    comps=type=="SCALAR" ? 1 : type=="VEC2" ? 2 : type=="VEC3" ? 3 : type=="VEC4" ? 4 : type=="MAT4" ? 16 : 0;
    if(!comps) throw std::runtime_error("unsupported accessor type "+type);
    const size_t count=gltfSize(acc,"count"),ct=gltfInteger(acc.at("componentType"),"componentType");
    const bool norm=acc.value("normalized",false);
    if constexpr(std::is_same_v<Value,uint32_t>) {
        if(comps!=1 || norm || (ct!=5121 && ct!=5123 && ct!=5125))
            throw std::runtime_error("triangle indices must be unnormalized unsigned SCALAR values");
    }
    if(!count || count>std::vector<Value>().max_size()/size_t(comps))
        throw std::runtime_error("invalid accessor count");
    if(ct!=5120 && ct!=5121 && ct!=5122 && ct!=5123 && ct!=5125 && ct!=5126)
        throw std::runtime_error("unsupported componentType "+std::to_string(ct));
    if(!acc.contains("bufferView") && acc.contains("byteOffset"))
        throw std::runtime_error("accessor byteOffset requires a bufferView");
    std::vector<Value> out=acc.contains("bufferView") ? readElements<Value>(g,acc,count,ct,comps,norm,false)
                                                    : std::vector<Value>(count*size_t(comps),Value{});
    if(acc.contains("sparse")) {
        const auto& sparse=acc.at("sparse");
        const size_t replacements=gltfSize(sparse,"count");
        if(!replacements || replacements>count) throw std::runtime_error("invalid sparse accessor count");
        const auto& indices=sparse.at("indices");const auto& values=sparse.at("values");
        const size_t indexType=gltfSize(indices,"componentType");
        if(indexType!=5121 && indexType!=5123 && indexType!=5125)
            throw std::runtime_error("sparse indices must use an unsigned integer component type");
        const auto slots=readElements<uint32_t>(g,indices,replacements,indexType,1,false,true);
        for(size_t i=0;i<slots.size();++i)
            if(slots[i]>=count || (i && slots[i]<=slots[i-1]))
                throw std::runtime_error("sparse indices must be increasing and inside the accessor count");
        const auto changes=readElements<Value>(g,values,replacements,ct,comps,norm,true);
        for(size_t i=0;i<slots.size();++i)
            std::copy_n(changes.begin()+i*size_t(comps),comps,out.begin()+size_t(slots[i])*size_t(comps));
    }
    if constexpr(std::is_same_v<Value,uint32_t>) {
        const uint32_t maximum=ct==5121 ? 255u : ct==5123 ? 65535u : 0xffffffffu;
        if(std::find(out.begin(),out.end(),maximum)!=out.end())
            throw std::runtime_error("triangle index uses the reserved maximum value");
    }
    return out;
}

// node transforms: apply the scene's world matrix to each mesh instance (column-major 4x4)
using Mat4 = std::array<float, 16>;
Mat4 identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }
Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c) for (int rr = 0; rr < 4; ++rr) { float s = 0; for (int k = 0; k < 4; ++k) s += a[size_t(k * 4 + rr)] * b[size_t(c * 4 + k)]; r[size_t(c * 4 + rr)] = s; }
    return r;
}
Mat4 nodeMatrix(const json& n) {
    for (const auto& [key, count] : {std::pair{"matrix", 16u}, {"translation", 3u}, {"scale", 3u}, {"rotation", 4u}}) {
        if (!n.contains(key)) continue;
        const auto& values = n.at(key);
        if (!values.is_array() || values.size() != count) throw std::runtime_error(std::string("invalid node ") + key);
        for (const auto& value : values)
            if (!value.is_number() || !std::isfinite(value.get<float>())) throw std::runtime_error(std::string("invalid node ") + key);
    }
    if (n.contains("matrix") && (n.contains("translation") || n.contains("scale") || n.contains("rotation")))
        throw std::runtime_error("node combines matrix and TRS transforms");
    if (n.contains("matrix")) {
        const auto& values=n["matrix"];
        if (values[3].get<double>()!=0 || values[7].get<double>()!=0 ||
            values[11].get<double>()!=0 || values[15].get<double>()!=1)
            throw std::runtime_error("node matrix must be affine (last row 0, 0, 0, 1)");
        Mat4 m; for (size_t i=0;i<16;++i) m[i]=values[i].get<float>(); return m;
    }
    Mat4 t = identity(), r = identity(), s = identity();
    if (n.contains("translation")) { t[12] = n["translation"][0]; t[13] = n["translation"][1]; t[14] = n["translation"][2]; }
    if (n.contains("scale")) { s[0] = n["scale"][0]; s[5] = n["scale"][1]; s[10] = n["scale"][2]; }
    if (n.contains("rotation")) {
        std::array<double,4> q;
        for (size_t i=0;i<4;++i) {
            q[i]=n["rotation"][i].get<double>();
            if (std::abs(q[i])>1) throw std::runtime_error("node rotation quaternion component outside [-1, 1]");
        }
        const double length=std::hypot(std::hypot(q[0],q[1]),std::hypot(q[2],q[3]));
        // Match Khronos glTF-Validator's unitLengthThresholdVec4, allowing
        // rounded/quantized unit rotations, then remove their scale error.
        if (std::abs(length-1)>0.00769) throw std::runtime_error("node rotation quaternion is not unit length");
        const float x=float(q[0]/length), y=float(q[1]/length), z=float(q[2]/length), w=float(q[3]/length);
        r = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0,
             2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0,
             2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0,
             0, 0, 0, 1};
    }
    return mul(mul(t, r), s);
}
void transformPoint(const Mat4& m, float& x, float& y, float& z, bool direction) {
    const float nx = m[0] * x + m[4] * y + m[8] * z + (direction ? 0 : m[12]);
    const float ny = m[1] * x + m[5] * y + m[9] * z + (direction ? 0 : m[13]);
    const float nz = m[2] * x + m[6] * y + m[10] * z + (direction ? 0 : m[14]);
    x = nx; y = ny; z = nz;
}

struct NormalTransform {
    // Cofactor columns are cross-products of the position matrix's columns.
    // After normalization, only the determinant's sign is needed for A^-T.
    std::array<double,9> cofactor;
    double determinant;
    explicit NormalTransform(const Mat4& m) {
        const double a=m[0],b=m[4],c=m[8],d=m[1],e=m[5],f=m[9],g=m[2],h=m[6],i=m[10];
        cofactor={e*i-f*h, c*h-b*i, b*f-c*e,
                  f*g-d*i, a*i-c*g, c*d-a*f,
                  d*h-e*g, b*g-a*h, a*e-b*d};
        determinant=a*cofactor[0]+d*cofactor[1]+g*cofactor[2];
    }
    Vec3 apply(float x,float y,float z) const {
        const double nx=cofactor[0]*x+cofactor[3]*y+cofactor[6]*z;
        const double ny=cofactor[1]*x+cofactor[4]*y+cofactor[7]*z;
        const double nz=cofactor[2]*x+cofactor[5]*y+cofactor[8]*z;
        const double length=std::hypot(nx,ny,nz);
        if (!std::isfinite(length) || length==0) throw std::runtime_error("invalid transformed glTF normal");
        const double sign=determinant<0 ? -1.0 : 1.0;
        return toFableDir(float(sign*nx/length),float(sign*ny/length),float(sign*nz/length));
    }
};

SkinBinding readSkin(const GltfBuffers& g,size_t skinIndex,size_t meshNode,
                     const std::vector<std::optional<Mat4>>& worlds,
                     const std::vector<std::optional<size_t>>& parents) {
    const auto& skin=g.doc.at("skins").at(skinIndex);const auto& nodes=g.doc.at("nodes");
    const auto& joints=skin.at("joints");
    if(!joints.is_array() || joints.empty() || joints.size()>65536) throw std::runtime_error("invalid glTF skin joint count");
    SkinBinding binding;binding.sourceSkin=skinIndex;binding.meshNode=meshNode;
    std::vector<bool> used(worlds.size(),false);std::optional<size_t> commonRoot;
    if(skin.contains("skeleton")) binding.skeletonRoot=gltfSize(skin,"skeleton");
    for(const auto& joint:joints) {
        const size_t ni=gltfInteger(joint,"joint node");
        if(ni>=worlds.size() || !worlds[ni] || used[ni])
            throw std::runtime_error("skin joint is missing, repeated or outside the selected scene");
        used[ni]=true;size_t root=ni;bool foundSkeleton=!binding.skeletonRoot;
        for(std::optional<size_t> ancestor=ni;ancestor;ancestor=parents[*ancestor]) {
            root=*ancestor;if(binding.skeletonRoot==ancestor) foundSkeleton=true;
        }
        if(!foundSkeleton || (commonRoot && *commonRoot!=root)) throw std::runtime_error("skin joints lack the declared/common scene root");
        commonRoot=root;
        binding.joints.push_back({ni,parents[ni],nodes.at(ni).value("name",std::string()),*worlds[ni],identity()});
    }
    if(skin.contains("inverseBindMatrices")) {
        const size_t index=gltfSize(skin,"inverseBindMatrices");const auto& acc=g.doc.at("accessors").at(index);
        if(acc.at("type")!="MAT4" || gltfSize(acc,"componentType")!=5126 || acc.value("normalized",false) ||
           gltfSize(acc,"count")<joints.size() || gltfSize(acc,"count")>1048576)
            throw std::runtime_error("invalid glTF inverse-bind accessor");
        if(acc.contains("bufferView") && g.doc.at("bufferViews").at(gltfSize(acc,"bufferView")).contains("byteStride"))
            throw std::runtime_error("inverse-bind buffer view must not define byteStride");
        int comps=0;const auto values=readAccessor(g,index,comps);
        for(size_t i=0;i<binding.joints.size();++i) {
            auto& matrix=binding.joints[i].inverseBind;std::copy_n(values.begin()+16*i,16,matrix.begin());
            if(matrix[3]!=0 || matrix[7]!=0 || matrix[11]!=0 || matrix[15]!=1)
                throw std::runtime_error("inverse-bind matrix must be affine");
        }
    }
    return binding;
}
void readSkinAttributes(const GltfBuffers& g,const json& attributes,size_t count,SkinBinding& binding) {
    struct Set {std::vector<float> joints,weights;size_t weightType;};
    std::map<size_t,Set> sets;
    for(const auto& [name,index]:attributes.items()) if(name.starts_with("JOINTS_") || name.starts_with("WEIGHTS_")) {
        const size_t prefix=name.starts_with("JOINTS_")?7:8;const std::string_view suffix(name.data()+prefix,name.size()-prefix);
        size_t set=0;const auto parsed=std::from_chars(suffix.data(),suffix.data()+suffix.size(),set);
        if(suffix.empty() || parsed.ec!=std::errc{} || parsed.ptr!=suffix.data()+suffix.size() || std::to_string(set)!=suffix)
            throw std::runtime_error("invalid glTF skin attribute set index");
        sets.try_emplace(set);
    }
    if(sets.empty() || !sets.contains(0)) throw std::runtime_error("skinned primitive requires JOINTS_0 and WEIGHTS_0");
    size_t next=0;
    for(auto& [set,data]:sets) {
        if(set!=next++) throw std::runtime_error("glTF skin attribute sets must be consecutive");
        for(bool joints:{true,false}) {
            const auto name=std::string(joints?"JOINTS_":"WEIGHTS_")+std::to_string(set);
            if(!attributes.contains(name)) throw std::runtime_error("unpaired glTF skin attribute set");
            const size_t index=gltfSize(attributes,name.c_str());const auto& acc=g.doc.at("accessors").at(index);
            const size_t component=gltfSize(acc,"componentType");const bool norm=acc.value("normalized",false);
            if(acc.at("type")!="VEC4" || gltfSize(acc,"count")!=count ||
               (joints? (norm || (component!=5121 && component!=5123)):
                !((component==5126 && !norm) || ((component==5121 || component==5123) && norm))))
                throw std::runtime_error("invalid glTF joint/weight accessor shape or encoding");
            int comps=0;auto values=readAccessor(g,index,comps);
            if(joints) data.joints=std::move(values);else {data.weights=std::move(values);data.weightType=component;}
        }
    }
    binding.influences.resize(count);
    for(size_t v=0;v<count;++v) {
        double total=0;uint64_t exactTotal=0;bool integerOnly=true;
        for(const auto& [set,data]:sets) for(size_t k=0;k<4;++k) {
            const size_t ordinal=size_t(data.joints[4*v+k]);const float weight=data.weights[4*v+k];
            if(ordinal>=binding.joints.size() || weight<0 || weight>1) throw std::runtime_error("glTF joint index or weight is out of range");
            total+=weight;
            if(data.weightType==5126) integerOnly=false;
            else exactTotal+=uint64_t(std::lround(double(weight)*(data.weightType==5121?255:65535)))*(data.weightType==5121?257:1);
            if(weight==0) continue;
            auto& influences=binding.influences[v];
            if(std::any_of(influences.begin(),influences.end(),[&](const auto& i){return i.bone==ordinal;}))
                throw std::runtime_error("joint has repeated positive weights for one vertex");
            influences.push_back({uint16_t(ordinal),weight});
        }
        if(integerOnly?exactTotal!=65535:std::abs(total-1)>1e-5) throw std::runtime_error("glTF vertex weights must sum to one");
    }
}

Model loadGltf(const fs::path& path,bool skinned=false) {
    Model model;
    const GltfBuffers g = openGltf(path);
    if(skinned && g.doc.contains("animations") && !g.doc.at("animations").empty())
        model.notes.push_back("glTF animation tracks are not imported; skin metadata retains the default joint transforms and inverse binds");
    for (const auto& extension : g.doc.value("extensionsUsed", json::array()))
        model.notes.push_back("ignored optional glTF extension: " + extension.get<std::string>());
    if (!g.doc.contains("meshes") || !g.doc.at("meshes").is_array() || g.doc.at("meshes").empty())
        throw std::runtime_error("glTF requires a nonempty meshes array");
    // glTF names are display labels, not identities. Missing material uses its
    // own default slot; repeated references to one array index share a slot.
    std::map<std::optional<size_t>, int> slotOf;
    auto slotFor = [&](const json& primitive) {
        std::string name="default";
        std::optional<size_t> identity;
        if(primitive.contains("material")) {
            const size_t matIndex=gltfSize(primitive,"material");
            if(!g.doc.contains("materials") || !g.doc["materials"].is_array() || matIndex>=g.doc["materials"].size())
                throw std::runtime_error("primitive references a missing glTF material");
            name=g.doc["materials"][matIndex].value("name","material"+std::to_string(matIndex));
            identity=matIndex;
        }
        auto it = slotOf.find(identity);
        if (it != slotOf.end()) return it->second;
        const int slot = int(model.materialNames.size());
        model.materialNames.push_back(name); slotOf[identity] = slot;
        return slot;
    };
    // Traverse the selected scene iteratively: malformed cycles and deep valid
    // hierarchies must not exhaust the process stack. Preserve depth-first order.
    struct MeshInstance {size_t mesh;Mat4 world;json weights;std::optional<size_t> skin;size_t node;};
    std::vector<MeshInstance> instances;
    const auto nodes=g.doc.value("nodes",json::array());
    if(!nodes.is_array()) throw std::runtime_error("nodes is not an array");
    std::vector<std::optional<Mat4>> worlds(nodes.size());
    std::vector<std::optional<size_t>> parents(nodes.size());
    if (g.doc.contains("scenes") && !g.doc["scenes"].empty()) {
        const size_t sceneIndex = gltfSize(g.doc, "scene");
        if (!g.doc["scenes"].is_array() || sceneIndex >= g.doc["scenes"].size())
            throw std::runtime_error("invalid default scene index");
        const auto& scene = g.doc["scenes"][sceneIndex];
        std::vector<bool> visited(nodes.size(), false);
        struct Pending {size_t node;Mat4 world;std::optional<size_t> parent;};
        std::vector<Pending> pending;
        auto enqueue = [&](const json& children, const Mat4& parent,std::optional<size_t> parentNode) {
            if (!children.is_array()) throw std::runtime_error("node list is not an array");
            for (auto it = children.rbegin(); it != children.rend(); ++it)
                pending.push_back({gltfInteger(*it, "node index"), parent,parentNode});
        };
        enqueue(scene.value("nodes", json::array()), identity(),std::nullopt);
        while (!pending.empty()) {
            const auto [ni, parent,parentNode] = pending.back(); pending.pop_back();
            if (ni >= nodes.size()) throw std::runtime_error("scene references a missing node");
            const auto& n = nodes[ni];
            if (visited[ni]) throw std::runtime_error("scene contains a cycle or repeated node");
            visited[ni] = true;
            if(!skinned && n.contains("mesh") && n.contains("skin"))
                throw std::runtime_error("active glTF skin is unsupported; bake the posed shape into a static mesh before export");
            const Mat4 world = mul(parent, nodeMatrix(n));
            if (!std::all_of(world.begin(), world.end(), [](float v) { return std::isfinite(v); }))
                throw std::runtime_error("node world transform is not finite");
            worlds[ni]=world;parents[ni]=parentNode;
            if (n.contains("mesh")) {
                if(skinned && !n.contains("skin")) throw std::runtime_error("skinned import requires skin on every selected mesh instance");
                instances.push_back({gltfSize(n, "mesh"),skinned?identity():world,n.value("weights",json{}),
                    n.contains("skin")?std::optional<size_t>(gltfSize(n,"skin")):std::nullopt,ni});
            }
            enqueue(n.value("children", json::array()), world,ni);
        }
    } else {
        if(skinned) throw std::runtime_error("skinned glTF requires a selected scene and joint hierarchy");
        // With no scenes, import the asset as a mesh library.
        for (size_t mi = 0; mi < g.doc["meshes"].size(); ++mi) instances.push_back({mi,identity(),json{},std::nullopt,0});
    }

    for (const auto& [mi, world, nodeWeights,skinIndex,meshNode] : instances) {
        const auto binding=skinned?std::optional<SkinBinding>(readSkin(g,*skinIndex,meshNode,worlds,parents)):std::nullopt;
        const NormalTransform normalTransform(world);
        const auto& mesh = g.doc["meshes"].at(size_t(mi));
        if (!mesh.is_object() || !mesh.contains("primitives") || !mesh.at("primitives").is_array() || mesh.at("primitives").empty())
            throw std::runtime_error("glTF mesh " + std::to_string(mi) + " requires a nonempty primitives array");
        const auto& primitives=mesh.at("primitives");
        for (size_t pi=0;pi<primitives.size();++pi) {
            try {
                const auto& prim=primitives[pi];
                const size_t mode=gltfSize(prim,"mode",4);
                if(mode>6) throw std::runtime_error("invalid glTF primitive mode");
                if(mode<4) {model.notes.push_back("skipped a non-triangle primitive (mode "+std::to_string(mode)+")");continue;}
                const auto& weights=nodeWeights.is_null()?mesh.value("weights",json{}):nodeWeights;
                const size_t targets=prim.contains("targets")?prim.at("targets").size():0;
                if(prim.contains("targets") && (!prim.at("targets").is_array() || !targets))
                    throw std::runtime_error("invalid glTF morph targets array");
                if(!weights.is_null()) {
                    if(!weights.is_array() || !targets || weights.size()!=targets)
                        throw std::runtime_error("glTF morph weight count must match the primitive targets");
                    for(const auto& weight:weights) {
                        if(!weight.is_number() || !std::isfinite(weight.get<double>()))
                            throw std::runtime_error("invalid glTF morph weight");
                        if(weight.get<double>()!=0)
                            throw std::runtime_error("active glTF morph shape is unsupported; bake the shape into a static mesh before export");
                    }
                }
                if (!prim.contains("attributes") || !prim.at("attributes").is_object())
                    throw std::runtime_error("triangle primitive requires an attributes object");
                const auto& attrs = prim.at("attributes");
                if (!attrs.contains("POSITION")) throw std::runtime_error("triangle primitive is missing POSITION");
                int comps = 0;
                const size_t positionIndex = gltfSize(attrs, "POSITION");
                const auto& position = g.doc.at("accessors").at(positionIndex);
                if (position.at("type") != "VEC3") throw std::runtime_error("POSITION is not VEC3");
                const size_t nv = gltfSize(position, "count");
                if (nv > forge::meshcompose::maxPrimitiveVertices)
                    throw std::runtime_error("vertex overflow: max 65535 per static mesh primitive");
                const bool indexed = prim.contains("indices");
                const size_t indexAccessor = indexed ? gltfSize(prim,"indices") : 0;
                const size_t topologyCount = indexed ? gltfSize(g.doc.at("accessors").at(indexAccessor),"count") : nv;
                const size_t maxCount = mode==4 ? forge::meshpreview::maxPrimitiveIndices
                                              : forge::meshpreview::maxPrimitiveIndices/3+2;
                if (topologyCount>maxCount)
                    throw std::runtime_error("glTF primitive exceeds preview triangle limit");
                if (topologyCount<3 || (mode==4 && topologyCount%3))
                    throw std::runtime_error(mode==4 ? "incomplete triangle list" : "triangle strip/fan requires at least three vertices or indices");
                auto checkAttribute = [&](const char* name, const char* type, const char* message) {
                    if (!attrs.contains(name)) return;
                    const auto& accessor = g.doc.at("accessors").at(gltfSize(attrs, name));
                    if (accessor.at("type") != type || gltfSize(accessor, "count") != nv)
                        throw std::runtime_error(message);
                    const size_t component=gltfInteger(accessor.at("componentType"),"componentType");
                    const bool normalized=accessor.value("normalized",false);
                    const bool floating=component==5126 && !normalized;
                    const bool compactUv=std::string_view(name)=="TEXCOORD_0" && normalized &&
                                         (component==5121 || component==5123);
                    if (!floating && !compactUv)
                        throw std::runtime_error(std::string(name)+" uses an unsupported component/normalization pair");
                };
                checkAttribute("POSITION", "VEC3", "POSITION must be VEC3");
                checkAttribute("NORMAL", "VEC3", "NORMAL must be VEC3 with the same count as POSITION");
                checkAttribute("TEXCOORD_0", "VEC2", "TEXCOORD_0 must be VEC2 with the same count as POSITION");
                const auto pos = readAccessor(g, positionIndex, comps);
                Primitive p;
                p.material = slotFor(prim);
                p.verts.reserve(nv);
                for (size_t i = 0; i < nv; ++i) {
                    float x = pos[i * 3], y = pos[i * 3 + 1], z = pos[i * 3 + 2];
                    transformPoint(world, x, y, z, false);
                    p.verts.push_back(toFable(x, y, z));
                }
                if (attrs.contains("NORMAL")) {
                    const auto nrm = readAccessor(g, gltfSize(attrs, "NORMAL"), comps);
                    if (normalTransform.determinant==0) {
                        model.notes.push_back("recomputed normals for a singular node transform");
                    } else {
                        p.normals.reserve(nv);
                        for (size_t i=0;i<nv;++i)
                            p.normals.push_back(normalTransform.apply(nrm[i*3],nrm[i*3+1],nrm[i*3+2]));
                    }
                }
                if (attrs.contains("TEXCOORD_0")) {
                    const auto uv = readAccessor(g, gltfSize(attrs, "TEXCOORD_0"), comps);
                    p.uvs.reserve(nv); for (size_t i = 0; i < nv; ++i) p.uvs.push_back({uv[i * 2], uv[i * 2 + 1]});
                }
                std::vector<uint32_t> indices;
                if (indexed) {
                    indices = readAccessor<uint32_t>(g, indexAccessor, comps);
                    for (const auto value : indices)
                        if (value >= nv) throw std::runtime_error("glTF index " + std::to_string(value) + " references a missing vertex");
                }
                const size_t count = indexed ? indices.size() : nv;
                auto vertex = [&](size_t i) { return indexed ? indices[i] : uint32_t(i); };
                if (mode==4) {
                    p.faces.reserve(count/3);
                    for (size_t i=0;i<count;i+=3) p.faces.push_back({vertex(i),vertex(i+1),vertex(i+2)});
                } else {
                    p.faces.reserve(count-2);
                    for (size_t i=0;i<count-2;++i) {
                        if (mode==6) p.faces.push_back({vertex(0),vertex(i+1),vertex(i+2)});
                        else if (i%2) p.faces.push_back({vertex(i+1),vertex(i),vertex(i+2)});
                        else p.faces.push_back({vertex(i),vertex(i+1),vertex(i+2)});
                    }
                }
                // a mirrored node transform flips the winding
                if (normalTransform.determinant < 0) for (auto& f : p.faces) std::swap(f[1], f[2]);
                if (!p.faces.empty()) {
                    if(binding) {auto skin=*binding;readSkinAttributes(g,attrs,nv,skin);model.skins.push_back(std::move(skin));}
                    model.prims.push_back(std::move(p));
                }
            } catch (const std::exception& error) {
                throw std::runtime_error("glTF mesh " + std::to_string(mi) + " primitive " + std::to_string(pi) + ": " + error.what());
            }
        }
    }
    if (model.materialNames.empty()) model.materialNames.push_back("default");
    return model;
}

// ---------------------------------------------------------------- OBJ

// Keep existing convex fans; clip ears for simple planar concave OBJ faces.
// No additional vertices are created, so each corner retains its UV/normal key.
std::vector<std::array<uint32_t,3>> triangulateObjFace(const std::vector<Vec3>& verts,
                                                     const std::vector<uint32_t>& poly) {
    if(poly.size()==3) return {{poly[0],poly[1],poly[2]}};
    if(poly.size()>1024) throw std::runtime_error("OBJ face exceeds 1024 corners; triangulate faces before import");
    std::vector<std::array<double,3>> local;
    const auto& origin=verts[poly[0]];
    for(const auto index:poly) {
        const auto& v=verts[index];
        local.push_back({double(v.x)-origin.x,double(v.y)-origin.y,double(v.z)-origin.z});
    }
    std::array<double,3> normal{};
    double extent=0;
    for(size_t i=0;i<local.size();++i) {
        const auto& a=local[i];const auto& b=local[(i+1)%local.size()];
        normal[0]+=a[1]*b[2]-a[2]*b[1];
        normal[1]+=a[2]*b[0]-a[0]*b[2];
        normal[2]+=a[0]*b[1]-a[1]*b[0];
        for(const double c:a) extent=std::max(extent,std::abs(c));
    }
    size_t drop=0;
    for(size_t axis=1;axis<3;++axis) if(std::abs(normal[axis])>std::abs(normal[drop])) drop=axis;
    if(normal[drop]==0) throw std::runtime_error("OBJ polygon has zero projected area; triangulate faces before import");
    struct Point {double x,y;};
    std::vector<Point> points;
    for(const auto& v:local) {
        if(drop==0) points.push_back({v[1],v[2]});
        else if(drop==1) points.push_back({v[0],v[2]});
        else points.push_back({v[0],v[1]});
    }
    const auto cross=[](const Point& a,const Point& b,const Point& c) {
        return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
    };
    const auto onSegment=[&](const Point& a,const Point& b,const Point& p) {
        return cross(a,b,p)==0 && p.x>=std::min(a.x,b.x) && p.x<=std::max(a.x,b.x) &&
               p.y>=std::min(a.y,b.y) && p.y<=std::max(a.y,b.y);
    };
    for(size_t i=0;i<points.size();++i) {
        const auto& a=points[i];const auto& b=points[(i+1)%points.size()];
        if(a.x==b.x && a.y==b.y) throw std::runtime_error("OBJ polygon has a repeated adjacent corner");
        for(size_t j=i+1;j<points.size();++j) {
            if(j==i+1 || (i==0 && j+1==points.size())) continue;
            const auto& c=points[j];const auto& d=points[(j+1)%points.size()];
            const double abC=cross(a,b,c),abD=cross(a,b,d),cdA=cross(c,d,a),cdB=cross(c,d,b);
            const bool crossing=((abC<0 && abD>0)||(abC>0 && abD<0)) &&
                                ((cdA<0 && cdB>0)||(cdA>0 && cdB<0));
            if(crossing || onSegment(a,b,c) || onSegment(a,b,d) || onSegment(c,d,a) || onSegment(c,d,b))
                throw std::runtime_error("OBJ polygon is self-intersecting; triangulate faces before import");
        }
    }
    double area=0;
    for(size_t i=0;i<points.size();++i) {
        const auto& a=points[i];const auto& b=points[(i+1)%points.size()];
        area+=a.x*b.y-a.y*b.x;
    }
    const double winding=area>0?1:-1;
    bool convex=true;
    for(size_t i=0;i<points.size();++i)
        if(winding*cross(points[(i+points.size()-1)%points.size()],points[i],points[(i+1)%points.size()])<0) convex=false;
    bool validFan=convex;
    for(size_t i=1;validFan && i+1<points.size();++i)
        if(winding*cross(points[0],points[i],points[i+1])<=0) validFan=false;
    std::vector<std::array<uint32_t,3>> result;
    if(validFan) {
        for(size_t i=1;i+1<poly.size();++i) result.push_back({poly[0],poly[i],poly[i+1]});
        return result;
    }
    const double length=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
    for(const auto& v:local)
        if(std::abs(normal[0]*v[0]+normal[1]*v[1]+normal[2]*v[2])/length>extent*1e-5)
            throw std::runtime_error("OBJ concave polygon is not planar; triangulate faces before import");
    std::vector<size_t> remaining;
    for(size_t i=0;i<poly.size();++i) remaining.push_back(i);
    size_t budget=8*1024*1024;
    while(remaining.size()>3) {
        bool clipped=false;
        for(size_t at=0;at<remaining.size();++at) {
            const size_t a=remaining[(at+remaining.size()-1)%remaining.size()],b=remaining[at],c=remaining[(at+1)%remaining.size()];
            if(winding*cross(points[a],points[b],points[c])<=0) continue;
            bool contains=false;
            for(const size_t candidate:remaining) {
                if(candidate==a || candidate==b || candidate==c) continue;
                if(!budget--) throw std::runtime_error("OBJ polygon triangulation is too complex; triangulate faces before import");
                if(winding*cross(points[a],points[b],points[candidate])>=0 &&
                   winding*cross(points[b],points[c],points[candidate])>=0 &&
                   winding*cross(points[c],points[a],points[candidate])>=0) {contains=true;break;}
            }
            if(contains) continue;
            result.push_back({poly[a],poly[b],poly[c]});
            remaining.erase(remaining.begin()+static_cast<ptrdiff_t>(at));
            clipped=true;break;
        }
        if(!clipped) throw std::runtime_error("OBJ polygon cannot be triangulated; triangulate faces before import");
    }
    result.push_back({poly[remaining[0]],poly[remaining[1]],poly[remaining[2]]});
    return result;
}

Model loadObj(const fs::path& path) {
    Model model;
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    std::vector<Vec3> v, vn; std::vector<Vec2> vt;
    struct Key { size_t p, t, n; bool operator<(const Key& o) const { return std::tie(p, t, n) < std::tie(o.p, o.t, o.n); } };
    constexpr size_t absent = std::numeric_limits<size_t>::max();
    auto index = [](const std::string& text, size_t count) -> size_t {
        size_t used = 0;
        const int value = std::stoi(text, &used);
        if (used != text.size() || value == 0)
            throw std::runtime_error("invalid OBJ face index: " + text);
        const size_t magnitude = size_t(value < 0 ? -int64_t(value) : value);
        if (magnitude > count) throw std::runtime_error("OBJ face references a missing attribute: " + text);
        return value < 0 ? count - magnitude : magnitude - 1;
    };
    auto finite3 = [](const Vec3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    auto readScalar = [](std::istringstream& stream, float& value) {
        if (!(stream >> value)) return false;
        const int next = stream.peek();
        return next == std::char_traits<char>::eof() ||
               std::isspace(static_cast<unsigned char>(next));
    };
    std::map<std::string, int> slotOf;
    std::map<int, Primitive> prims;          // slot -> primitive
    std::map<int, std::map<Key, uint32_t>> dedupe;
    int slot = 0;
    auto ensureSlot = [&](const std::string& name) {
        auto it = slotOf.find(name);
        if (it != slotOf.end()) return it->second;
        const int s = int(model.materialNames.size());
        model.materialNames.push_back(name); slotOf[name] = s;
        return s;
    };
    slot = ensureSlot("default");
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (const auto comment = line.find('#'); comment != std::string::npos) line.resize(comment);
        std::istringstream ls(line);
        std::string tag; ls >> tag;
        if (tag == "v" || tag == "vn") {
            float x = 0, y = 0, z = 0;
            if (!readScalar(ls,x) || !readScalar(ls,y) || !readScalar(ls,z))
                throw std::runtime_error("incomplete or invalid OBJ " + tag);
            const auto converted = tag == "v" ? toFable(x, y, z) : toFableDir(x, y, z);
            if (!finite3(converted)) throw std::runtime_error("non-finite OBJ " + tag);
            (tag == "v" ? v : vn).push_back(converted);
        }
        else if (tag == "vt") {
            float u = 0, w = 0;
            if (!readScalar(ls,u)) throw std::runtime_error("incomplete or invalid OBJ vt");
            ls >> std::ws;
            if (!ls.eof() && !readScalar(ls,w)) throw std::runtime_error("invalid OBJ vt");
            if (!std::isfinite(u) || !std::isfinite(w)) throw std::runtime_error("non-finite OBJ vt");
            vt.push_back({u, 1.0f - w}); // OBJ v is bottom-up; glTF/Blender convention is top-down
        }
        else if (tag == "usemtl") { std::string n; ls >> n; slot = ensureSlot(n.empty() ? "default" : n); }
        else if (tag == "f") {
            std::vector<uint32_t> poly;
            std::string tok;
            while (ls >> tok) {
                Key k{absent, absent, absent};
                const size_t s1 = tok.find('/');
                k.p = index(tok.substr(0, s1), v.size());
                if (s1 != std::string::npos) {
                    const size_t s2 = tok.find('/', s1 + 1);
                    const std::string ts = tok.substr(s1 + 1, s2 == std::string::npos ? std::string::npos : s2 - s1 - 1);
                    if (!ts.empty()) k.t = index(ts, vt.size());
                    if (s2 != std::string::npos && s2 + 1 < tok.size()) k.n = index(tok.substr(s2 + 1), vn.size());
                }
                auto& prim = prims[slot];
                auto& dd = dedupe[slot];
                auto it = dd.find(k);
                if (it == dd.end()) {
                    if (prim.verts.size() >= forge::meshcompose::maxPrimitiveVertices)
                        throw std::runtime_error("vertex overflow: max 65535 per static mesh primitive");
                    it = dd.emplace(k, uint32_t(prim.verts.size())).first;
                    prim.verts.push_back(v[size_t(k.p)]);
                    prim.uvs.push_back(k.t != absent ? vt[k.t] : Vec2{});
                    if (k.n != absent) prim.normals.push_back(vn[k.n]);
                }
                poly.push_back(it->second);
            }
            if (poly.size() < 3) throw std::runtime_error("OBJ face has fewer than three vertices");
            auto& prim = prims[slot];
            if (poly.size()-2 > forge::meshpreview::maxPrimitiveIndices/3-prim.faces.size())
                throw std::runtime_error("OBJ primitive exceeds preview triangle limit");
            const auto triangles=triangulateObjFace(prim.verts,poly);
            prim.faces.insert(prim.faces.end(),triangles.begin(),triangles.end());
        }
    }
    for (auto& [s, prim] : prims) {
        if (prim.faces.empty()) continue;
        if (prim.normals.size() != prim.verts.size()) prim.normals.clear();   // partial normals: recompute
        prim.material = s;
        model.prims.push_back(std::move(prim));
    }
    return model;
}

bool validName(const std::string& n) {
    if (n.empty() || n.size() >= 80) return false;
    for (char c : n) if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

} // namespace

static void appendUvNotes(Model& model) {
    for (size_t part=0;part<model.prims.size();++part) {
        size_t clipped=0;
        for (const auto& uv:model.prims[part].uvs)
            if (forge::meshcompose::uvNeedsClamping(uv.u) || forge::meshcompose::uvNeedsClamping(1.0f-uv.v))
                ++clipped;
        if (clipped) model.notes.push_back("model part "+std::to_string(part+1)+": texture coordinates exceed the supported range on "+
            std::to_string(clipped)+" vertices; import clips these coordinates. Reduce texture tiling to preserve the mapping.");
    }
}

std::array<double,16> inverseBindInFable(const std::array<float,16>& source) {
    for(float value:source)
        if(!std::isfinite(value)) throw std::runtime_error("non-finite source inverse bind matrix");
    if(source[3]!=0 || source[7]!=0 || source[11]!=0 || source[15]!=1)
        throw std::runtime_error("non-affine source inverse bind matrix");
    // Signed permutation plus uniform unit scale. Linear terms lose the
    // uniform scale in conjugation; translation retains metres-to-centimetres.
    constexpr std::array<size_t,3> axes{0,2,1};
    constexpr std::array<double,3> signs{1,-1,1};
    std::array<double,16> result{};
    for(size_t row=0;row<3;++row) {
        for(size_t column=0;column<3;++column)
            result[row*4+column]=signs[row]*signs[column]*source[axes[column]*4+axes[row]];
        result[row*4+3]=100.0*signs[row]*source[12+axes[row]];
    }
    result[15]=1;
    return result;
}

Model loadModel(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    Model model;
    if (ext == ".glb" || ext == ".gltf") model=loadGltf(path);
    else if (ext == ".obj") model=loadObj(path);
    else throw std::runtime_error("unsupported model format " + ext + " (.glb, .gltf or .obj)");
    appendUvNotes(model);return model;
}

Model loadSkinnedModel(const fs::path& path) {
    auto ext=path.extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(ext!=".glb" && ext!=".gltf") throw std::runtime_error("skinned import requires glTF or GLB joint/weight data");
    auto model=loadGltf(path,true);
    if(model.prims.empty() || model.skins.size()!=model.prims.size()) throw std::runtime_error("no skinned triangle geometry in selected scene");
    appendUvNotes(model);
    for(size_t part=0;part<model.skins.size();++part) {
        size_t reduced=0;for(const auto& influences:model.skins[part].influences) if(influences.size()>3) ++reduced;
        if(reduced) model.notes.push_back("model part "+std::to_string(part+1)+": "+std::to_string(reduced)+" vertices have more than three influences; Fable composition keeps the strongest three");
    }
    return model;
}

bool importModel(const fs::path& gameRoot, const ImportRequest& req, ImportResult& out, std::string& error) {
    return importModel(gameRoot, gameRoot, req, out, error);
}

bool importModel(const fs::path& baseRoot, const fs::path& outRoot, const ImportRequest& req, ImportResult& out, std::string& error) {
    out = ImportResult{};
    std::error_code ec;
    const bool inPlace = fs::equivalent(baseRoot, outRoot, ec) || baseRoot == outRoot;
    if (!validName(req.name)) { error = "name must be A-Z, 0-9 and _ (got '" + req.name + "')"; return false; }
    if (inPlace && backups::gameRunningIn(baseRoot)) { error = "Fable is running from this install; quit to the desktop first"; return false; }
    // read: the output tree's copy when an earlier layer wrote one, else the base; write: the output tree
    auto readPath = [&](const fs::path& rel) { const fs::path o = outRoot / rel; return fs::exists(o, ec) ? o : baseRoot / rel; };
    if (!fs::exists(req.model, ec)) { error = "no such model: " + req.model.string(); return false; }
    if (!req.texturePng.empty() && !fs::exists(req.texturePng, ec)) { error = "no such image: " + req.texturePng.string(); return false; }
    try {
        const fs::path defsRel = fs::path("data") / "CompiledDefs";
        const fs::path namesIn = readPath(defsRel / "names.bin"), gameIn = readPath(defsRel / "game.bin");
        const fs::path texRel = fs::path("data") / "graphics" / "pc" / "textures.big";
        const fs::path texIn = readPath(texRel);
        fs::path gfxRel = fs::path("data") / "graphics" / "graphics.big";
        if (!fs::exists(readPath(gfxRel), ec)) gfxRel = fs::path("data") / "graphics" / "pc" / "graphics.big";
        const fs::path gfxIn = readPath(gfxRel);
        if (!fs::exists(gfxIn, ec)) { error = "no graphics.big under " + (baseRoot / "data" / "graphics").string(); return false; }
        out.meshName = "MESH_" + req.name;
        out.objectName = "OBJECT_" + req.name;

        auto defs = forge::bin::File::open(namesIn, gameIn);
        if (defs.find(out.objectName)) { error = "game.bin already has a def named " + out.objectName; return false; }
        if (std::count_if(defs.entries().begin(), defs.entries().end(),
                          [&](const auto& entry) { return entry.name == req.donor; }) > 1) {
            error = "ambiguous donor definition: " + req.donor; return false;
        }
        const auto* donor = defs.find(req.donor);
        if (!donor) { error = "no OBJECT named " + req.donor + " to copy from"; return false; }
        if (donor->definition != "OBJECT") { error = req.donor + " is a " + donor->definition + ", not an OBJECT"; return false; }

        // 1. the model, composed
        Model model = loadModel(req.model);
        if (model.prims.empty()) { error = "the model has no triangles"; return false; }
        out.modelNotes = model.notes;
        for (const auto& n : model.notes) out.notes.push_back(n);
        detail::PendingBanks pending(outRoot, ".forge-model-import-");

        // 2. the texture: an appended GBANK_MAIN_PC entry, or the id the caller names
        out.textureId = req.textureId;
        if (!req.texturePng.empty()) {
            const std::string symbol = req.name + "_DIFFUSE";
            {
                const auto tex = forge::big::File::open(texIn);
                const auto* bank = tex.findBank("GBANK_MAIN_PC");
                if (!bank) { error = "textures.big has no GBANK_MAIN_PC"; return false; }
                for (const auto& e : bank->entries) if (e.name == symbol) { error = "textures.big already has an entry named " + symbol; return false; }
            }
            forge::terraintex::ImportRequest ir;
            ir.png = req.texturePng; ir.srcBig = texIn; ir.outBig = pending.prepare(texRel);
            ir.entryName = symbol; ir.subBank = "GBANK_MAIN_PC"; ir.format = "dxt1"; ir.add = true;
            const auto r = forge::terraintex::importPng(ir);
            if (!r.ok) {
                error = "texture import failed for " + req.texturePng.string();
                for (const auto& issue : r.validation.errors) error += ": " + issue;
                if (!r.output.empty()) error += ": " + r.output;
                return false;
            }
            out.textureId = uint32_t(r.entryId);
            out.notes.push_back("textures.big: appended " + symbol + " (id " + std::to_string(out.textureId) + ", DXT1) from " + req.texturePng.string());
        } else if(out.textureId!=0) {
            const auto tex=forge::big::File::open(texIn);
            const auto* bank=tex.findBank("GBANK_MAIN_PC");
            if(!bank) {error="textures.big has no GBANK_MAIN_PC";return false;}
            const auto matches=std::count_if(bank->entries.begin(),bank->entries.end(),
                [&](const auto& entry){return entry.id==out.textureId;});
            if(matches!=1) {
                error=(matches ? "ambiguous texture ID " : "no texture ID ")+std::to_string(out.textureId)+" in GBANK_MAIN_PC";
                return false;
            }
        }
        std::vector<forge::meshcompose::Material> materials;
        for (const auto& n : model.materialNames) {
            forge::meshcompose::Material m;
            m.name = out.meshName + "_" + n;
            m.diffuseId = out.textureId;
            materials.push_back(m);
        }
        // 3a. the collision hull: the next id, so the render mesh's Info can name it
        uint32_t nextId = 0;
        {
            const auto gfx = forge::big::File::open(gfxIn);
            const auto* bank = gfx.findBank("MBANK_ALLMESHES");
            if (!bank || bank->entries.empty()) { error = "graphics.big has no MBANK_ALLMESHES"; return false; }
            for (const auto& e : bank->entries) {
                nextId = std::max(nextId, e.id);
                if (e.name == out.meshName || (req.collision && e.name == out.meshName + "[PHYSICS]")) {
                    error = "graphics.big already has a mesh named " + e.name; return false;
                }
            }
            const uint32_t needed = req.collision ? 2u : 1u;
            if (nextId > UINT32_MAX - needed) { error = "no free mesh entry IDs for this import"; return false; }
            ++nextId;
        }
        std::vector<uint8_t> physics;
        if (req.collision) { physics = forge::meshcompose::composePhysics(model.prims, out.meshName); out.physicsId = nextId++; }
        const auto composed = forge::meshcompose::composeStatic(out.meshName, model.prims, materials, req.compress, out.physicsId);
        out.vertices = composed.vertices; out.triangles = composed.triangles; out.primitives = model.prims.size();
        // the decoder must read back what we wrote (the same reader the preview and thumbnails use)
        const auto check = forge::meshpreview::decodeLod0(composed.payload, 1);
        if (check.vertices.size() != composed.vertices || check.triangles.size() != composed.triangles) {
            error = "composed mesh does not decode back (" + std::to_string(check.vertices.size()) + "/" + std::to_string(check.triangles.size()) + " of " +
                    std::to_string(composed.vertices) + "/" + std::to_string(composed.triangles) + ")";
            return false;
        }

        // 3. graphics.big: appended MBANK_ALLMESHES entry with the next id
        {
            auto gfx = forge::big::File::open(gfxIn);
            auto* bank = gfx.findBank("MBANK_ALLMESHES");
            if (!bank) { error = "graphics.big has no MBANK_ALLMESHES"; return false; }
            if (bank->entries.empty()) { error = "MBANK_ALLMESHES is empty"; return false; }
            const forge::big::Entry* modelEntry = nullptr;
            for (const auto& e : bank->entries) if (e.type == 1 && !modelEntry) modelEntry = &e;
            if (!modelEntry) modelEntry = &bank->entries.back();
            const uint32_t entryMagic = gfx.magic() == "BIGB" ? 42 : modelEntry->magic;
            const uint32_t entryDevFileType = modelEntry->devFileType;
            if (!physics.empty()) {   // the hull first (its id was reserved above)
                forge::big::Entry h;
                h.magic = entryMagic; h.devFileType = entryDevFileType; h.type = 3;
                h.id = out.physicsId;
                h.name = out.meshName + "[PHYSICS]";   // retail's name for a hull
                h.data = physics;
                h.length = uint32_t(physics.size());
                bank->entries.push_back(std::move(h));
            }
            forge::big::Entry e;
            e.magic = entryMagic; e.devFileType = entryDevFileType; e.type = 1;
            e.id = nextId;
            e.name = out.meshName;
            e.subHeader = composed.info;
            e.data = composed.payload;
            e.length = uint32_t(composed.payload.size());
            bank->entries.push_back(std::move(e));
            out.meshId = nextId;
            const auto bytes = gfx.serialize();
            const fs::path tmp = pending.prepare(gfxRel);
            { std::ofstream o(tmp, std::ios::binary | std::ios::trunc); o.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())); o.close(); if (!o) { error = "cannot write " + tmp.string(); return false; } }
            out.notes.push_back("graphics.big: appended " + out.meshName + " (id " + std::to_string(out.meshId) + ", " + std::to_string(out.vertices) + " vertices, " +
                                std::to_string(out.triangles) + " triangles, " + std::to_string(out.primitives) + " primitive(s), " + std::to_string(composed.payload.size()) + " bytes)" +
                                (physics.empty() ? std::string(", no collision hull") : " + " + out.meshName + "[PHYSICS] (id " + std::to_string(out.physicsId) + ", the model's own triangles as the hull)"));
        }

        // 4. the OBJECT def: the donor's bytes, Graphic.modelId repointed, the mesh size from the bounds
        const size_t index = defs.addEntry("OBJECT", out.objectName, donor->data);
        const auto schema = forge::defschema::Schema::loadText(kEmbeddedDefSchema, "embedded");
        auto graphic = forge::defedit::getFieldBytes(defs, schema, out.objectName, "Graphic");
        if (graphic.size() < 8) { error = "the donor's Graphic field is not a CEngineGraphic"; return false; }
        std::memcpy(graphic.data() + 4, &out.meshId, 4);
        forge::defedit::setFieldBytes(defs, schema, out.objectName, "Graphic", graphic);
        // Bounds are mesh centimetres. Subtract in double before converting to
        // world units: opposite finite float endpoints may have an overflowing
        // float difference even though the final definition size is finite.
        const float height = float((double(composed.bbMax.z) - composed.bbMin.z) / kMetresToMesh);
        const float radius = float(0.5 * std::max(double(composed.bbMax.x) - composed.bbMin.x,
                                                double(composed.bbMax.y) - composed.bbMin.y) / kMetresToMesh);
        auto setf = [&](const char* field, float value) {
            try { forge::defedit::setField(defs, schema, out.objectName, field, std::to_string(value)); }
            catch (const std::exception& e) { out.notes.push_back(std::string("game.bin: ") + field + " left as the donor's (" + e.what() + ")"); }
        };
        setf("MeshHeight", height); setf("ApproxMaxMeshHeight", height); setf("MeshRadius", radius);
        const fs::path namesOut = pending.prepare(defsRel / "names.bin"), gameOut = pending.prepare(defsRel / "game.bin");
        defs.save(namesOut, gameOut);
        {
            const auto verified = forge::bin::File::open(namesOut, gameOut);
            const auto* entry = verified.find(out.objectName);
            if (verified.entries().size() != defs.entries().size() || !entry || entry->data != defs.find(out.objectName)->data) {
                error = "prepared definitions did not read back correctly";
                return false;
            }
        }
        if (inPlace && backups::gameRunningIn(baseRoot)) { error = "Fable started during import; quit to the desktop first"; return false; }
        if (!pending.install(inPlace, error)) return false;
        out.defIndex = index;
        out.notes.push_back("game.bin: appended OBJECT " + out.objectName + " (def index " + std::to_string(index) + ", a copy of " + req.donor + " with Graphic.modelId " +
                            std::to_string(out.meshId) + ", height " + std::to_string(height) + ", radius " + std::to_string(radius) + ")");
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}

bool replaceModel(const fs::path& gameRoot,const ReplaceRequest& req,ReplaceResult& out,std::string& error) {
    return replaceModel(gameRoot,gameRoot,req,out,error);
}

bool replaceModel(const fs::path& baseRoot,const fs::path& outRoot,const ReplaceRequest& req,ReplaceResult& out,std::string& error) {
    out=ReplaceResult{};
    std::error_code ec;
    const bool inPlace=fs::equivalent(baseRoot,outRoot,ec) || baseRoot==outRoot;
    if(backups::gameRunningIn(outRoot)) {error="Fable is running from the destination install; quit to the desktop first";return false;}
    try {
        auto readPath=[&](const fs::path& relative) {
            const auto staged=outRoot/relative;
            return fs::exists(staged)?staged:baseRoot/relative;
        };
        fs::path relative=fs::path("data")/"graphics"/"graphics.big";
        if(!fs::exists(readPath(relative))) relative=fs::path("data")/"graphics"/"pc"/"graphics.big";
        auto archive=forge::big::File::open(readPath(relative));
        if(archive.magic()!="BIGB") throw std::runtime_error("mesh replacement requires a BIGB graphics bank");
        auto* bank=archive.findBank("MBANK_ALLMESHES");
        if(!bank) throw std::runtime_error("graphics.big has no MBANK_ALLMESHES");
        forge::big::Entry* target=nullptr;
        for(auto& entry:bank->entries) if(entry.name==req.target) {
            if(target) throw std::runtime_error("ambiguous mesh name: "+req.target);
            target=&entry;
        }
        if(!target) {
            uint32_t id=0;const auto parsed=std::from_chars(req.target.data(),req.target.data()+req.target.size(),id);
            if(parsed.ec!=std::errc{} || parsed.ptr!=req.target.data()+req.target.size())
                throw std::runtime_error("no mesh named "+req.target);
            for(auto& entry:bank->entries) if(entry.id==id) {
                if(target) throw std::runtime_error("ambiguous mesh ID: "+req.target);
                target=&entry;
            }
        }
        if(!target) throw std::runtime_error("no mesh entry: "+req.target);
        if(!req.expectedName.empty() && target->name!=req.expectedName)
            throw std::runtime_error("selected mesh identity changed; refresh the model list before replacing");
        if(std::count_if(bank->entries.begin(),bank->entries.end(),[&](const auto& entry){return entry.id==target->id;})!=1)
            throw std::runtime_error("ambiguous target mesh ID");
        if(target->type!=1) throw std::runtime_error("mesh replacement supports static type-1 entries only");
        if(target->name.rfind("MESH_",0)!=0 || target->name.find('\0')!=std::string::npos)
            throw std::runtime_error("target has no supported MESH_ symbol name");
        const auto& info=target->subHeader;
        if(info.size()<60) throw std::runtime_error("target mesh descriptor is truncated");
        auto word=[&](size_t offset) {uint32_t value;std::memcpy(&value,info.data()+offset,4);return value;};
        if(word(44)!=1) throw std::runtime_error("mesh replacement requires exactly one source LOD");
        const uint32_t textureCount=word(56);
        if(textureCount>(info.size()-60)/4 || info.size()!=60+size_t(textureCount)*4)
            throw std::runtime_error("target mesh descriptor has unsupported trailing or texture data");
        float safeRadius;std::memcpy(&safeRadius,info.data()+52,4);
        if(safeRadius!=0) throw std::runtime_error("mesh replacement cannot preserve a nonzero safe-radius override");
        const auto oldPayload=archive.entryData(*target);
        const auto nameEnd=std::find(oldPayload.begin(),oldPayload.end(),uint8_t(0));
        if(nameEnd==oldPayload.end()) throw std::runtime_error("target mesh has an unterminated payload name");
        const size_t helperOffset=size_t(nameEnd-oldPayload.begin())+1+1+40;
        if(helperOffset>oldPayload.size() || oldPayload.size()-helperOffset<10)
            throw std::runtime_error("target mesh header is truncated");
        if(*(nameEnd+1)!=0) throw std::runtime_error("target static mesh has an unsupported animated header");
        if(oldPayload.size()-helperOffset<26 ||
           std::any_of(oldPayload.begin()+helperOffset+18,oldPayload.begin()+helperOffset+26,[](uint8_t byte){return byte!=0;}))
            throw std::runtime_error("mesh replacement cannot discard bone-name header metadata");
        if(std::any_of(oldPayload.begin()+helperOffset,oldPayload.begin()+helperOffset+10,[](uint8_t byte){return byte!=0;}))
            throw std::runtime_error("mesh replacement cannot discard helper points, dummies or other header metadata");
        const size_t rootOffset=helperOffset+10+16+5;
        const std::array<float,12> identityRoot{1,0,0,0,1,0,0,0,1,0,0,0};
        if(rootOffset>oldPayload.size() || oldPayload.size()-rootOffset<sizeof(identityRoot) ||
           std::memcmp(oldPayload.data()+rootOffset,identityRoot.data(),sizeof(identityRoot))!=0)
            throw std::runtime_error("mesh replacement cannot discard a noncanonical root matrix");
        const auto decoded=forge::meshpreview::decodeLods(oldPayload,info,1);
        if(decoded.size()!=1 || decoded[0].boneCount || !decoded[0].helpers.empty() || decoded[0].hasCloth || decoded[0].animatedBlockCount)
            throw std::runtime_error("mesh replacement cannot discard bones, helpers, cloth or animated blocks");
        const auto& source=decoded[0];
        for(const auto& primitive:source.primitives) if(primitive.repeatCount!=1)
            throw std::runtime_error("mesh replacement cannot discard repeated primitive instances");
        if(source.materials.size()<2 || !source.materials.back().nativeFlags[3])
            throw std::runtime_error("target mesh has no final degenerate material sentinel");
        std::vector<forge::meshcompose::Material> materials;
        for(size_t slot=0;slot<source.materials.size();++slot) {
            const auto& old=source.materials[slot];
            if(slot+1<source.materials.size()) {
                if(old.nativeFlags[3]) throw std::runtime_error("target has an unsupported nonfinal degenerate material");
                if(old.id!=int32_t(slot)) throw std::runtime_error("target material IDs are not canonical array slots");
                for(size_t previous=0;previous<slot;++previous) if(source.materials[previous].id==old.id)
                    throw std::runtime_error("target has ambiguous ordinary material IDs");
            }
            forge::meshcompose::Material material;
            material.name=old.name;material.decalId=old.decalId;
            material.diffuseId=old.diffuseTexture;material.bumpId=old.bumpTexture;
            material.reflectId=old.reflectionTexture;material.illumId=old.alphaMapTexture;
            std::memcpy(&material.selfIllum,&old.glowStrength,4);
            material.originalId=old.id;material.originalMapFlags=old.textureFlags;
            material.nativeFlags=old.nativeFlags;material.filenames=old.filenames;
            materials.push_back(std::move(material));
        }
        const uint32_t physics=word(0);
        if(physics && physics!=UINT32_MAX) {
            const auto matches=std::count_if(bank->entries.begin(),bank->entries.end(),[&](const auto& entry){return entry.id==physics;});
            const auto hull=std::find_if(bank->entries.begin(),bank->entries.end(),[&](const auto& entry){return entry.id==physics;});
            if(matches!=1 || hull->type!=3) throw std::runtime_error("target collision link is missing, ambiguous or not a type-3 hull");
        }
        auto model=loadModel(req.model);
        if(model.prims.empty()) throw std::runtime_error("the replacement model has no triangles");
        if(model.materialNames.size()+1!=materials.size())
            throw std::runtime_error("replacement material slot count must match the target mesh; slots map by ordinal");
        for(const auto& primitive:model.prims)
            if(primitive.material<0 || size_t(primitive.material)+1>=materials.size())
                throw std::runtime_error("replacement primitive uses an invalid material slot");
        auto composed=forge::meshcompose::composeStatic(target->name,model.prims,materials,req.compress,physics==UINT32_MAX?0:physics,false);
        std::memcpy(composed.info.data(),&physics,4); // preserve either native no-hull spelling
        composed.info.resize(56);
        composed.info.insert(composed.info.end(),info.begin()+56,info.end()); // retain descriptor texture references and their order
        const auto check=forge::meshpreview::decodeLods(composed.payload,composed.info,1);
        if(check.size()!=1 || check[0].vertices.size()!=composed.vertices || check[0].triangles.size()!=composed.triangles || check[0].materials.size()!=source.materials.size())
            throw std::runtime_error("replacement geometry failed read-back verification");
        for(size_t slot=0;slot<source.materials.size();++slot) {
            const auto& a=source.materials[slot];const auto& b=check[0].materials[slot];
            if(a.id!=b.id || a.name!=b.name || a.decalId!=b.decalId || a.diffuseTexture!=b.diffuseTexture ||
               a.bumpTexture!=b.bumpTexture || a.reflectionTexture!=b.reflectionTexture || a.alphaMapTexture!=b.alphaMapTexture ||
               a.textureFlags!=b.textureFlags || a.glowStrength!=b.glowStrength || a.nativeFlags!=b.nativeFlags || a.filenames!=b.filenames)
                throw std::runtime_error("replacement did not preserve a source material record");
        }
        out.meshId=target->id;out.physicsId=physics;out.meshName=target->name;
        out.vertices=composed.vertices;out.triangles=composed.triangles;out.primitives=model.prims.size();
        out.notes=model.notes;
        target->data=std::move(composed.payload);target->subHeader=std::move(composed.info);target->length=uint32_t(target->data.size());
        const auto bytes=archive.serialize();
        detail::PendingBanks pending(outRoot,".forge-mesh-replace-");
        const auto prepared=pending.prepare(relative);
        {std::ofstream stream(prepared,std::ios::binary|std::ios::trunc);stream.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));stream.close();
         if(!stream) throw std::runtime_error("cannot write prepared graphics bank");}
        const auto verified=forge::big::File::open(prepared);
        const auto* verifiedBank=verified.findBank("MBANK_ALLMESHES");
        if(!verifiedBank || verifiedBank->entries.size()!=bank->entries.size()) throw std::runtime_error("prepared graphics bank failed entry-count verification");
        for(size_t index=0;index<bank->entries.size();++index) {
            const auto& a=bank->entries[index];const auto& b=verifiedBank->entries[index];
            if(a.name!=b.name || a.id!=b.id || a.type!=b.type || a.magic!=b.magic || a.devFileType!=b.devFileType ||
               a.devCrc!=b.devCrc || a.devSources!=b.devSources || a.subHeader!=b.subHeader || archive.entryData(a)!=verified.entryData(b))
                throw std::runtime_error("prepared graphics bank failed entry preservation verification");
        }
        if(backups::gameRunningIn(outRoot)) {error="Fable started from the destination during replacement; quit to the desktop first";return false;}
        if(!pending.install(inPlace,error)) return false;
        out.notes.push_back("Replaced "+out.meshName+" at ID "+std::to_string(out.meshId)+"; original materials and collision preserved. Definitions and textures unchanged.");
        return true;
    } catch(const std::exception& exception) {error=exception.what();return false;}
}

} // namespace albion::meshimport
