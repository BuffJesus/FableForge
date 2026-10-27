#include "meshimport.hpp"
#include "forge/meshpreview.hpp"
#include "nlohmann/json.hpp"

#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

static void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
static void put32(Bytes& bytes, uint32_t value) {
    for (unsigned shift = 0; shift != 32; shift += 8) bytes.push_back(uint8_t(value >> shift));
}
static void set32(Bytes& bytes, size_t offset, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i) bytes.at(offset + i) = uint8_t(value >> (i * 8));
}
static void chunk(Bytes& bytes, uint32_t type, const Bytes& content) {
    put32(bytes, uint32_t(content.size())); put32(bytes, type);
    bytes.insert(bytes.end(), content.begin(), content.end());
    set32(bytes, 8, uint32_t(bytes.size()));
}

int main() {
    const auto dir = fs::temp_directory_path() / ("FableForgeMeshInput-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directory(dir);
        const auto path = dir / "triangle.glb";
        auto load = [&](const Bytes& bytes) {
            { std::ofstream f(path, std::ios::binary); f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())); }
            return albion::meshimport::loadModel(path);
        };
        size_t checks = 0;
        auto rejects = [&](const Bytes& bytes, const std::string& label) {
            bool rejected = false;
            try { load(bytes); } catch (const std::exception&) { rejected = true; }
            require(rejected, "accepted " + label);
            ++checks;
        };
        auto triangle = [&](const Bytes& bytes, const std::string& label) {
            const auto model = load(bytes);
            require(model.prims.size() == 1 && model.prims[0].verts.size() == 3 && model.prims[0].faces.size() == 1,
                    "lost triangle in " + label);
            require(model.prims[0].verts[1].x == 100 && model.prims[0].verts[2].z == 100,
                    "changed vertex conversion in " + label);
            ++checks;
        };

        std::string json = R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":36}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]})";
        while (json.size() % 4) json += ' ';
        Bytes good;
        put32(good, 0x46546c67); put32(good, 2); put32(good, 0);
        chunk(good, 0x4e4f534a, Bytes(json.begin(), json.end()));
        const size_t binaryHeader = good.size();
        Bytes positions;
        for (float f : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) put32(positions, std::bit_cast<uint32_t>(f));
        chunk(good, 0x004e4942, positions);
        triangle(good, "ordinary GLB");

        const auto document = nlohmann::json::parse(json);
        auto withDocument = [&](const nlohmann::json& doc, const Bytes& binary) {
            std::string encoded = doc.dump();
            while (encoded.size() % 4) encoded += ' ';
            Bytes result; put32(result, 0x46546c67); put32(result, 2); put32(result, 0);
            chunk(result, 0x4e4f534a, Bytes(encoded.begin(), encoded.end()));
            chunk(result, 0x004e4942, binary);
            return result;
        };
        for (int component : {5120,5121,5122,5123,5125}) {
            auto invalid=document;invalid["accessors"][0]["componentType"]=component;
            rejects(withDocument(invalid,positions),"non-core POSITION component encoding");
        }
        auto invalidPositionEncoding=document;invalidPositionEncoding["accessors"][0]["normalized"]=true;
        rejects(withDocument(invalidPositionEncoding,positions),"normalized FLOAT POSITION");
        invalidPositionEncoding=document;invalidPositionEncoding["accessors"][0].erase("componentType");
        rejects(withDocument(invalidPositionEncoding,positions),"POSITION accessor missing componentType");
        auto requiredExtension=document;
        requiredExtension["extensionsUsed"]={"KHR_draco_mesh_compression"};
        requiredExtension["extensionsRequired"]={"KHR_draco_mesh_compression"};
        rejects(withDocument(requiredExtension,positions),"unsupported required glTF extension");
        auto optionalExtension=document;
        optionalExtension["extensionsUsed"]={"VENDOR_optional_geometry"};
        triangle(withDocument(optionalExtension,positions),"optional extension core fallback");
        const auto optionalModel=load(withDocument(optionalExtension,positions));
        require(optionalModel.notes.size()==1 && optionalModel.notes[0].find("VENDOR_optional_geometry")!=std::string::npos,
                "optional extension omission was not reported");++checks;
        for (const char* field : {"extensionsUsed","extensionsRequired"}) {
            for (const auto& invalid : std::vector<nlohmann::json>{nullptr,42,"VENDOR_extension",
                    nlohmann::json::array(),nlohmann::json::array({42}),nlohmann::json::array({""}),
                    nlohmann::json::array({"VENDOR_extension","VENDOR_extension"})}) {
                auto malformed=document;malformed[field]=invalid;
                rejects(withDocument(malformed,positions),std::string("malformed ")+field);
            }
        }
        requiredExtension.erase("extensionsUsed");
        rejects(withDocument(requiredExtension,positions),"required extension absent from used list");
        auto missingPart=document;
        missingPart["meshes"][0]["primitives"].push_back({{"attributes",nlohmann::json::object()}});
        rejects(withDocument(missingPart,positions),"model with a triangle part missing POSITION");
        missingPart["meshes"].push_back(missingPart["meshes"][0]);
        missingPart["meshes"][0]["primitives"].erase(1);
        bool partLocated=false;
        try { load(withDocument(missingPart,positions)); }
        catch (const std::runtime_error& error) {
            const std::string message=error.what();
            partLocated=message.find("glTF mesh 1 primitive 1:")!=std::string::npos && message.find("POSITION")!=std::string::npos;
        }
        require(partLocated,"missing model part error lacks mesh/primitive location");++checks;
        for (const auto& attributes : std::vector<nlohmann::json>{nullptr,42,"POSITION",nlohmann::json::array()}) {
            auto malformed=document;malformed["meshes"][0]["primitives"][0]["attributes"]=attributes;
            rejects(withDocument(malformed,positions),"malformed primitive attributes");
        }
        for (const char* field : {"bufferViews","accessors","buffers"}) {
            auto malformed=document;malformed.erase(field);
            rejects(withDocument(malformed,positions),std::string("missing required resource array ")+field);
        }
        auto noAttributes=document;noAttributes["meshes"][0]["primitives"][0].erase("attributes");
        rejects(withDocument(noAttributes,positions),"missing attributes object");
        for (const auto& primitives : std::vector<nlohmann::json>{nullptr,nlohmann::json::object(),nlohmann::json::array()}) {
            auto malformed=document;malformed["meshes"][0]["primitives"]=primitives;
            rejects(withDocument(malformed,positions),"malformed primitives array");
        }
        auto noPrimitives=document;noPrimitives["meshes"][0].erase("primitives");
        rejects(withDocument(noPrimitives,positions),"missing primitives array");

        auto secondBuffer=document;
        secondBuffer["buffers"].push_back({{"byteLength",36}});
        secondBuffer["bufferViews"][0]["buffer"]=1;
        rejects(withDocument(secondBuffer,positions),"second URI-less buffer aliases GLB BIN");
        secondBuffer["buffers"][1]["uri"]="data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA";
        triangle(withDocument(secondBuffer,Bytes(36,0)),"second explicit buffer remains independent of GLB BIN");
        auto sparseDoc=document;auto sparseBytes=positions;
        set32(sparseBytes,12,std::bit_cast<uint32_t>(2.f));set32(sparseBytes,28,std::bit_cast<uint32_t>(3.f));
        sparseBytes.insert(sparseBytes.end(),{1,2,0,0});
        sparseBytes.insert(sparseBytes.end(),positions.begin()+12,positions.end());
        sparseDoc["buffers"][0]["byteLength"]=sparseBytes.size();
        sparseDoc["bufferViews"].push_back({{"buffer",0},{"byteOffset",36},{"byteLength",2}});
        sparseDoc["bufferViews"].push_back({{"buffer",0},{"byteOffset",40},{"byteLength",24}});
        sparseDoc["accessors"][0]["sparse"]={{"count",2},{"indices",{{"bufferView",1},{"componentType",5121}}},{"values",{{"bufferView",2}}}};
        triangle(withDocument(sparseDoc,sparseBytes),"sparse positions replace base values");
        auto sparseZero=sparseDoc;sparseZero["accessors"][0].erase("bufferView");
        triangle(withDocument(sparseZero,sparseBytes),"sparse positions over implicit zeros");
        auto sparseInterleaved=sparseDoc;Bytes sparseInterleavedBytes;
        for(size_t i=0;i<3;++i) {
            sparseInterleavedBytes.insert(sparseInterleavedBytes.end(),sparseBytes.begin()+12*i,sparseBytes.begin()+12*i+12);
            put32(sparseInterleavedBytes,0);
        }
        sparseInterleavedBytes.insert(sparseInterleavedBytes.end(),sparseBytes.begin()+36,sparseBytes.end());
        sparseInterleaved["buffers"][0]["byteLength"]=sparseInterleavedBytes.size();
        sparseInterleaved["bufferViews"][0]["byteLength"]=48;sparseInterleaved["bufferViews"][0]["byteStride"]=16;
        sparseInterleaved["bufferViews"][1]["byteOffset"]=48;sparseInterleaved["bufferViews"][2]["byteOffset"]=52;
        triangle(withDocument(sparseInterleaved,sparseInterleavedBytes),"sparse replacements over interleaved base");
        auto largePrimitive=document;largePrimitive["accessors"][0].erase("bufferView");
        largePrimitive["accessors"][0]["count"]=65535;
        const auto boundaryModel=load(withDocument(largePrimitive,positions));
        require(boundaryModel.prims[0].verts.size()==65535 && boundaryModel.prims[0].faces.size()==21845,
                "lost maximum supported primitive vertex count");++checks;
        largePrimitive["accessors"][0]["count"]=65538;
        rejects(withDocument(largePrimitive,positions),"vertex count beyond static mesh primitive capacity");
        for (const char* attribute : {"POSITION", "NORMAL", "TEXCOORD_0"}) {
            auto oversized = document;
            const size_t accessorIndex = std::string(attribute) == "POSITION" ? 0 : 1;
            oversized["accessors"][accessorIndex] = {{"componentType",5126},{"count",uint64_t(1)<<40},
                {"type",std::string(attribute)=="TEXCOORD_0" ? "VEC2" : "VEC3"}};
            oversized["meshes"][0]["primitives"][0]["attributes"][attribute] = accessorIndex;
            bool refusedBeforeAllocation = false;
            try { load(withDocument(oversized,positions)); }
            catch (const std::runtime_error& error) {
                const std::string message = error.what();
                refusedBeforeAllocation = message.find(accessorIndex==0 ? "vertex overflow" : "same count as POSITION") != std::string::npos;
            }
            require(refusedBeforeAllocation,"oversized accessor did not fail metadata preflight");++checks;
        }
        for(const auto& indices:std::vector<Bytes>{{1,1},{2,1},{1,3},{1,255}}) {
            auto invalid=sparseBytes;invalid[36]=indices[0];invalid[37]=indices[1];
            rejects(withDocument(sparseDoc,invalid),"duplicate/descending/out-of-range sparse index");
        }
        for(int count:{0,4}) {
            auto invalid=sparseDoc;invalid["accessors"][0]["sparse"]["count"]=count;
            rejects(withDocument(invalid,sparseBytes),"invalid sparse count");
        }
        for(const char* field:{"indices","values"}) {
            auto invalid=sparseDoc;invalid["accessors"][0]["sparse"].erase(field);
            rejects(withDocument(invalid,sparseBytes),std::string("missing sparse ")+field);
        }
        for(size_t view:{1,2}) for(const char* field:{"byteLength","byteStride","target"}) {
            auto invalid=sparseDoc;invalid["bufferViews"][view][field]=std::string(field)=="target" ? 34962 : 1;
            rejects(withDocument(invalid,sparseBytes),"truncated/strided/targeted sparse buffer view");
        }
        for(int component:{5120,5122,5126}) {
            auto invalid=sparseDoc;invalid["accessors"][0]["sparse"]["indices"]["componentType"]=component;
            rejects(withDocument(invalid,sparseBytes),"invalid sparse index component type");
        }
        auto sparseNan=sparseBytes;set32(sparseNan,40,0x7fc00000u);
        rejects(withDocument(sparseDoc,sparseNan),"non-finite sparse replacement value");
        for(int indexType:{5123,5125}) {
            auto wideDoc=sparseDoc;Bytes wideBytes(sparseBytes.begin(),sparseBytes.begin()+36);
            if(indexType==5123)wideBytes.insert(wideBytes.end(),{1,0,2,0});
            else {put32(wideBytes,1);put32(wideBytes,2);}
            wideDoc["bufferViews"][1]["byteLength"]=wideBytes.size()-36;
            wideDoc["bufferViews"][2]["byteOffset"]=wideBytes.size();
            wideBytes.insert(wideBytes.end(),positions.begin()+12,positions.end());
            wideDoc["buffers"][0]["byteLength"]=wideBytes.size();
            wideDoc["accessors"][0]["sparse"]["indices"]["componentType"]=indexType;
            triangle(withDocument(wideDoc,wideBytes),"wide unsigned sparse indices");
            auto misaligned=wideDoc;misaligned["accessors"][0]["sparse"]["indices"]["byteOffset"]=1;
            rejects(withDocument(misaligned,wideBytes),"unaligned sparse index offset");
        }
        for(uint64_t offset:{uint64_t(2),uint64_t(-4)}) {
            auto invalid=sparseDoc;invalid["accessors"][0]["sparse"]["values"]["byteOffset"]=offset;
            rejects(withDocument(invalid,sparseBytes),"unaligned/overflowing sparse value offset");
        }
        auto sparseUv=document;auto sparseUvBytes=positions;
        sparseUvBytes.insert(sparseUvBytes.end(),{1,2,0,0,255,0,0,255});
        sparseUv["buffers"][0]["byteLength"]=sparseUvBytes.size();
        sparseUv["bufferViews"].push_back({{"buffer",0},{"byteOffset",36},{"byteLength",2}});
        sparseUv["bufferViews"].push_back({{"buffer",0},{"byteOffset",40},{"byteLength",4}});
        sparseUv["accessors"].push_back({{"componentType",5121},{"count",3},{"type","VEC2"},{"normalized",true},
                                       {"sparse",sparseDoc["accessors"][0]["sparse"]}});
        sparseUv["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_0"]=1;
        const auto sparseUvModel=load(withDocument(sparseUv,sparseUvBytes));
        require(sparseUvModel.prims[0].uvs[0].u==0 && sparseUvModel.prims[0].uvs[1].u==1 &&
                sparseUvModel.prims[0].uvs[2].v==1,"sparse normalized UVs lost values");++checks;
        auto sparseTriangles=sparseUv;auto sparseTriangleBytes=sparseUvBytes;
        sparseTriangleBytes[40]=1;sparseTriangleBytes[41]=0;sparseTriangleBytes[42]=2;sparseTriangleBytes[43]=0;
        sparseTriangles["accessors"][1]["componentType"]=5123;
        sparseTriangles["accessors"][1]["type"]="SCALAR";sparseTriangles["accessors"][1].erase("normalized");
        sparseTriangles["meshes"][0]["primitives"][0]["attributes"].erase("TEXCOORD_0");
        sparseTriangles["meshes"][0]["primitives"][0]["indices"]=1;
        triangle(withDocument(sparseTriangles,sparseTriangleBytes),"sparse triangle indices over zeros");
        sparseTriangleBytes.insert(sparseTriangleBytes.end(),{0,0,255,255,255,255,0,0});
        sparseTriangles["buffers"][0]["byteLength"]=sparseTriangleBytes.size();
        sparseTriangles["bufferViews"].push_back({{"buffer",0},{"byteOffset",44},{"byteLength",6}});
        sparseTriangles["accessors"][1]["bufferView"]=3;
        triangle(withDocument(sparseTriangles,sparseTriangleBytes),"triangle index constraints apply after sparse replacement");
        sparseTriangleBytes[40]=sparseTriangleBytes[41]=255;
        rejects(withDocument(sparseTriangles,sparseTriangleBytes),"reserved triangle index after sparse replacement");
        auto sparseLast=document;Bytes sparseLastBytes{255,0,0,0};
        for(float value:{1.f,0.f,0.f})put32(sparseLastBytes,std::bit_cast<uint32_t>(value));
        sparseLast["buffers"][0]["byteLength"]=sparseLastBytes.size();
        sparseLast["bufferViews"]={{{"buffer",0},{"byteLength",1}},{{"buffer",0},{"byteOffset",4},{"byteLength",12}}};
        sparseLast["accessors"][0].erase("bufferView");sparseLast["accessors"][0]["count"]=258;
        sparseLast["accessors"][0]["sparse"]={{"count",1},{"indices",{{"bufferView",0},{"componentType",5121}}},{"values",{{"bufferView",1}}}};
        const auto sparseLastModel=load(withDocument(sparseLast,sparseLastBytes));
        require(sparseLastModel.prims[0].verts.size()==258 && sparseLastModel.prims[0].verts[255].x==100 &&
                sparseLastModel.prims[0].verts[0].x==0,"sparse slot 255 confused with triangle restart sentinel");++checks;
        for(const auto& asset:std::vector<nlohmann::json>{
                nlohmann::json::object(),{{"version","1.0"}},{{"version","3.0"}},{{"version",2}},
                {{"version","2"}},{{"version","2.0junk"}},{{"version","2.-1"}},
                {{"version","2.0"},{"minVersion","2.1"}},{{"version","2.1"},{"minVersion","2.1"}},
                {{"version","2.0"},{"minVersion","1.0"}},{{"version","2.0"},{"minVersion",2}},
                {{"version","2.0"},{"minVersion","2.0junk"}}}) {
            auto versionDoc=document;versionDoc["asset"]=asset;
            rejects(withDocument(versionDoc,positions),"unsupported or malformed glTF asset version");
        }
        auto missingAsset=document;missingAsset.erase("asset");
        rejects(withDocument(missingAsset,positions),"missing glTF asset version");
        for(const auto& asset:std::vector<nlohmann::json>{
                {{"version","2.0"},{"minVersion","2.0"}},{{"version","2.1"}},
                {{"version","2.1"},{"minVersion","2.0"}}}) {
            auto versionDoc=document;versionDoc["asset"]=asset;
            triangle(withDocument(versionDoc,positions),"compatible glTF asset version");
        }
        auto decimalDoc=document;
        auto decimalIntegers=[](auto&& self,nlohmann::json& value)->void {
            if(value.is_number_integer()) value=double(value.get<uint64_t>());
            else if(value.is_structured()) for(auto& child:value)self(self,child);
        };
        decimalIntegers(decimalIntegers,decimalDoc);
        triangle(withDocument(decimalDoc,positions),"whole-valued decimal integer fields");
        for(const auto& index:std::vector<nlohmann::json>{-1,0.5,1,uint64_t(0x100000000)}) {
            auto materialDoc=document;materialDoc["materials"]={{{"name","stone"}}};
            materialDoc["meshes"][0]["primitives"][0]["material"]=index;
            rejects(withDocument(materialDoc,positions),"invalid material reference");
        }
        auto missingMaterial=document;missingMaterial["meshes"][0]["primitives"][0]["material"]=0;
        rejects(withDocument(missingMaterial,positions),"reference to missing material array");
        auto namedMaterial=document;namedMaterial["materials"]={{{"name","stone"}}};
        namedMaterial["meshes"][0]["primitives"][0]["material"]=0.0;
        const auto materialModel=load(withDocument(namedMaterial,positions));
        require(materialModel.materialNames==std::vector<std::string>{"stone"},"lost valid material reference");++checks;
        for(const auto& names:std::vector<std::vector<std::string>>{{"stone","stone"},{"default","default"},{"",""}}) {
            auto duplicateNames=document;
            duplicateNames["materials"]={{{"name",names[0]}},{{"name",names[1]}}};
            auto primitive=document["meshes"][0]["primitives"][0];
            duplicateNames["meshes"][0]["primitives"]=nlohmann::json::array();
            for(int index:{0,1,0,-1}) {
                auto part=primitive;if(index>=0) part["material"]=index;
                duplicateNames["meshes"][0]["primitives"].push_back(part);
            }
            const auto distinct=load(withDocument(duplicateNames,positions));
            require(distinct.materialNames.size()==3 && distinct.prims.size()==4 &&
                    distinct.prims[0].material==0 && distinct.prims[1].material==1 &&
                    distinct.prims[2].material==0 && distinct.prims[3].material==2,
                    "glTF merged distinct material identities by display name");++checks;
        }
        for(const auto& mode:std::vector<nlohmann::json>{-1,4.5,7,uint64_t(0x100000004)}) {
            auto modeDoc=document;modeDoc["meshes"][0]["primitives"][0]["mode"]=mode;
            rejects(withDocument(modeDoc,positions),"invalid primitive topology mode");
        }
        for (int mode : {5,6}) {
            auto topologyDoc=document;Bytes topologyBytes;
            for (float f : {0.f,0.f,0.f, 1.f,0.f,0.f, 0.f,1.f,0.f, 1.f,1.f,0.f, 0.f,2.f,0.f})
                put32(topologyBytes,std::bit_cast<uint32_t>(f));
            topologyDoc["buffers"][0]["byteLength"]=60;
            topologyDoc["bufferViews"][0]["byteLength"]=60;
            topologyDoc["accessors"][0]["count"]=5;
            topologyDoc["meshes"][0]["primitives"][0]["mode"]=mode;
            const std::vector<std::array<uint32_t,3>> expected = mode==5
                ? std::vector<std::array<uint32_t,3>>{{0,1,2},{2,1,3},{2,3,4}}
                : std::vector<std::array<uint32_t,3>>{{0,1,2},{0,2,3},{0,3,4}};
            const auto topology=load(withDocument(topologyDoc,topologyBytes));
            require(topology.prims.size()==1 && topology.prims[0].faces==expected,"lost triangle strip/fan winding");++checks;
            const auto compiled=forge::meshcompose::composeStatic("MESH_TOPOLOGY",topology.prims,{{}},false);
            const auto decoded=forge::meshpreview::decodeLod0(compiled.payload,1);
            require(decoded.vertices.size()==5 && decoded.triangles.size()==expected.size(),"lost compiled strip/fan geometry");
            for (size_t i=0;i<expected.size();++i) {
                const auto& face=decoded.triangles[i];
                require(std::array<uint32_t,3>{face.a,face.b,face.c}==expected[i],"compiled strip/fan changed winding");
            }
            ++checks;
            for (size_t count : {1,2}) {
                auto shortTopology=topologyDoc;shortTopology["accessors"][0]["count"]=count;
                rejects(withDocument(shortTopology,topologyBytes),"short non-indexed triangle strip/fan");
            }
            topologyDoc["scenes"]={{{"nodes",{0}}}};
            topologyDoc["nodes"]={{{"mesh",0},{"scale",{-1,1,1}}}};
            auto mirroredExpected=expected;
            for (auto& face : mirroredExpected) std::swap(face[1],face[2]);
            require(load(withDocument(topologyDoc,topologyBytes)).prims[0].faces==mirroredExpected,
                    "lost mirrored strip/fan winding");++checks;
            topologyDoc.erase("scenes");topologyDoc.erase("nodes");
            topologyBytes.insert(topologyBytes.end(),{4,3,2,1,0,0,0,0});
            topologyDoc["buffers"][0]["byteLength"]=68;
            topologyDoc["bufferViews"].push_back({{"buffer",0},{"byteOffset",60},{"byteLength",5}});
            topologyDoc["accessors"].push_back({{"bufferView",1},{"componentType",5121},{"count",5},{"type","SCALAR"}});
            topologyDoc["meshes"][0]["primitives"][0]["indices"]=1;
            auto reversedExpected=expected;
            for (auto& face : reversedExpected) for (auto& index : face) index=4-index;
            require(load(withDocument(topologyDoc,topologyBytes)).prims[0].faces==reversedExpected,
                    "lost indexed strip/fan winding");++checks;
            for (size_t count : {1,2}) {
                auto shortTopology=topologyDoc;shortTopology["accessors"][1]["count"]=count;
                rejects(withDocument(shortTopology,topologyBytes),"short indexed triangle strip/fan");
            }
            for (uint8_t invalid : {uint8_t(5),uint8_t(255)}) {
                auto invalidBytes=topologyBytes;invalidBytes[62]=invalid;
                rejects(withDocument(topologyDoc,invalidBytes),"missing/restart index in triangle strip/fan");
            }
        }
        // No backing bytes: these counts must refuse before implicit-zero allocation.
        for (int mode : {4,5,6}) for (uint64_t count : {uint64_t(30000003),uint64_t(1)<<40}) {
            auto oversizedIndices=document;
            oversizedIndices["accessors"].push_back({{"componentType",5125},{"count",count},{"type","SCALAR"}});
            oversizedIndices["meshes"][0]["primitives"][0]["indices"]=1;
            oversizedIndices["meshes"][0]["primitives"][0]["mode"]=mode;
            bool refusedBeforeAllocation=false;
            try { load(withDocument(oversizedIndices,positions)); }
            catch (const std::runtime_error& error) {
                refusedBeforeAllocation=std::string(error.what()).find("preview triangle limit")!=std::string::npos;
            }
            require(refusedBeforeAllocation,"oversized indices did not fail metadata preflight");++checks;
        }
        auto linesDoc=document;linesDoc["meshes"][0]["primitives"][0]["mode"]=1;
        linesDoc["accessors"][0]["count"]=2;
        const auto linesModel=load(withDocument(linesDoc,positions));
        require(linesModel.prims.empty() && linesModel.notes.size()==1,"valid unsupported topology loses skip note");++checks;
        for(double count:{-1.0,3.5,18446744073709551616.0,1e100}) {
            auto invalidNumber=document;invalidNumber["accessors"][0]["count"]=count;
            rejects(withDocument(invalidNumber,positions),"negative/fractional/overflowing decimal count");
        }
        for(uint32_t bits : {0x7fc00000u,0x7f800000u,0xff800000u}) {
            auto invalidPosition=positions;set32(invalidPosition,0,bits);
            rejects(withDocument(document,invalidPosition),"non-finite position");
        }
        auto overflowPosition=positions;set32(overflowPosition,0,std::bit_cast<uint32_t>(3e37f));
        rejects(withDocument(document,overflowPosition),"position overflow after metre conversion");
        auto uvDoc=document;auto uvBytes=positions;
        for(size_t i=0;i<6;++i)put32(uvBytes,0);
        uvDoc["buffers"][0]["byteLength"]=uvBytes.size();
        uvDoc["bufferViews"].push_back({{"buffer",0},{"byteOffset",36},{"byteLength",24}});
        uvDoc["accessors"].push_back({{"bufferView",1},{"componentType",5126},{"count",3},{"type","VEC2"}});
        uvDoc["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_0"]=1;
        auto clippedUv=uvBytes;
        set32(clippedUv,36,std::bit_cast<uint32_t>(8.f));
        set32(clippedUv,44,std::bit_cast<uint32_t>(-25.f));
        set32(clippedUv,48,std::bit_cast<uint32_t>(-7.f));
        const auto clippedModel=load(withDocument(uvDoc,clippedUv));
        require(clippedModel.notes.size()==1 && clippedModel.notes[0].find("2 vertices")!=std::string::npos &&
                clippedModel.notes[0].find("texture")!=std::string::npos,"UV clipping was not reported per affected vertex");++checks;
        auto edgeUv=uvBytes;
        set32(edgeUv,36,std::bit_cast<uint32_t>(-24.f));
        set32(edgeUv,40,std::bit_cast<uint32_t>(-24.f));   // V is stored unflipped: same range as U
        set32(edgeUv,44,std::bit_cast<uint32_t>(8.f-1.f/2048));
        set32(edgeUv,48,std::bit_cast<uint32_t>(8.f-1.f/2048));
        set32(edgeUv,52,std::bit_cast<uint32_t>(8.f-1.f/2048+1.f/8192));
        require(load(withDocument(uvDoc,edgeUv)).notes.empty(),"ordinary UV quantization incorrectly reported clipping");++checks;
        for(const char* attribute:{"NORMAL","TEXCOORD_0"}) {
            auto shapeDoc=document;auto shapeBytes=positions;
            for(size_t i=0;i<3;++i) for(float v:{0.f,0.f,1.f}) put32(shapeBytes,std::bit_cast<uint32_t>(v));
            shapeDoc["buffers"][0]["byteLength"]=shapeBytes.size();
            shapeDoc["bufferViews"].push_back({{"buffer",0},{"byteOffset",36},{"byteLength",36}});
            shapeDoc["accessors"].push_back({{"bufferView",1},{"componentType",5126},{"count",3},
                                           {"type",std::string(attribute)=="NORMAL" ? "VEC3" : "VEC2"}});
            shapeDoc["meshes"][0]["primitives"][0]["attributes"][attribute]=1;
            auto wrongCount=shapeDoc;wrongCount["accessors"][1]["count"]=2;
            rejects(withDocument(wrongCount,shapeBytes),std::string(attribute)+" count differs from positions");
            auto wrongShape=shapeDoc;wrongShape["accessors"][1]["type"]="SCALAR";
            rejects(withDocument(wrongShape,shapeBytes),std::string(attribute)+" has wrong vector shape");
            for (int component : {5120,5121,5122,5123,5125,5126}) for (bool normalized : {false,true}) {
                const bool allowed=component==5126 ? !normalized :
                    std::string(attribute)=="TEXCOORD_0" && normalized && (component==5121 || component==5123);
                if(allowed) continue;
                auto invalid=shapeDoc;invalid["accessors"][1]["componentType"]=component;
                invalid["accessors"][1]["normalized"]=normalized;
                auto nonzero=shapeBytes;for(size_t i=36;i<nonzero.size();++i)nonzero[i]=1;
                rejects(withDocument(invalid,nonzero),std::string(attribute)+" unsupported component/normalization pair");
            }
        }
        for(int component : {5121,5123}) {
            auto compact=uvDoc;auto bytes=positions;
            for(const auto& row : {std::array<int,2>{0,1},std::array<int,2>{1,0},std::array<int,2>{1,1}}) {
                for(int value : row) {
                    bytes.push_back(value ? 255 : 0);
                    if(component==5123) bytes.push_back(value ? 255 : 0);
                }
                if(component==5121) {bytes.push_back(0);bytes.push_back(0);}
            }
            compact["buffers"][0]["byteLength"]=bytes.size();
            compact["bufferViews"][1]["byteLength"]=12;
            compact["bufferViews"][1]["byteStride"]=4;
            compact["accessors"][1]["componentType"]=component;
            compact["accessors"][1]["normalized"]=true;
            const auto model=load(withDocument(compact,bytes));
            const auto& uv=model.prims[0].uvs;
            require(uv.size()==3 && uv[0].u==0 && uv[0].v==1 && uv[1].u==1 && uv[1].v==0 &&
                    uv[2].u==1 && uv[2].v==1,"dense normalized UVs lost values");++checks;
            auto unaligned=compact;auto offsetBytes=bytes;
            offsetBytes.insert(offsetBytes.begin()+36,2,0);
            unaligned["buffers"][0]["byteLength"]=offsetBytes.size();
            unaligned["bufferViews"][1]["byteLength"]=14;
            unaligned["accessors"][1]["byteOffset"]=2;
            while(offsetBytes.size()%4)offsetBytes.push_back(0);
            rejects(withDocument(unaligned,offsetBytes),"vertex attribute offset is not four-byte aligned");
            if(component==5121) {
                auto tightlyPacked=uvDoc;auto packedBytes=positions;
                packedBytes.insert(packedBytes.end(),{0,255,255,0,255,255});
                tightlyPacked["buffers"][0]["byteLength"]=packedBytes.size();
                tightlyPacked["bufferViews"][1]["byteLength"]=6;
                tightlyPacked["accessors"][1]["componentType"]=5121;
                tightlyPacked["accessors"][1]["normalized"]=true;
                while(packedBytes.size()%4)packedBytes.push_back(0);
                rejects(withDocument(tightlyPacked,packedBytes),"vertex attribute elements are not four-byte aligned");
            }
        }
        for(uint32_t bits : {0x7fc00000u,0x7f800000u,0xff800000u}) {
            auto invalidUv=uvBytes;set32(invalidUv,36,bits);
            rejects(withDocument(uvDoc,invalidUv),"non-finite texture coordinate");
        }
        // The expected positions are independent of the URI decoder; include
        // enough nonzero bits to exercise repeated accumulator shifts.
        for(const auto* mime : {"application/octet-stream","application/gltf-buffer"}) {
            auto embedded=document;
            embedded["buffers"][0]["uri"]=std::string("data:")+mime+
                ";base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA";
            const auto text=embedded.dump();
            triangle(Bytes(text.begin(),text.end()),"embedded base64 buffer");
        }
        const std::string base64Positions="AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA";
        for(const auto& uri : std::vector<std::string>{
                "data:application/octet-stream,"+base64Positions,
                "data:image/png;base64,"+base64Positions,
                "data:application/octet-stream;base64,"+base64Positions+"!",
                "data:application/octet-stream;base64,"+base64Positions+"=garbage",
                "data:application/octet-stream;base64,"+base64Positions+"A",
                "data:application/octet-stream;base64,"+base64Positions+"AA=A",
                "data:application/octet-stream;base64,"+base64Positions+"%GG"}) {
            auto embedded=document;embedded["buffers"][0]["uri"]=uri;
            const auto text=embedded.dump();rejects(Bytes(text.begin(),text.end()),"malformed buffer data URI");
        }
        for(const auto& tail : {std::string("AA=="),std::string("AAA="),std::string("AA%3D%3D")}) {
            auto embedded=document;
            embedded["buffers"][0]["byteLength"]=tail=="AAA=" ? 38 : 37;
            embedded["buffers"][0]["uri"]="data:application/octet-stream;x=fixture;base64,"+base64Positions+tail;
            const auto text=embedded.dump();triangle(Bytes(text.begin(),text.end()),"padded base64 buffer");
        }
        auto escaped=document;
        escaped["buffers"][0]["uri"]="DATA:APPLICATION/OCTET-STREAM;base64,%2FwCAPwAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA";
        const auto escapedText=escaped.dump();
        const auto escapedModel=load(Bytes(escapedText.begin(),escapedText.end()));
        require(escapedModel.prims[0].verts[0].x==std::bit_cast<float>(uint32_t(0x3f8000ff))*100,
                "percent-escaped base64 changed decoded coordinate");++checks;
        for(const auto& tail : {"AB==","AAB=","====","%","%2","%20"}) {
            auto embedded=document;
            embedded["buffers"][0]["uri"]="data:application/octet-stream;base64,"+base64Positions+tail;
            const auto text=embedded.dump();rejects(Bytes(text.begin(),text.end()),"invalid base64 padding or escape");
        }
        const auto externalPath=dir/fs::u8path(u8"mesh \u00e9 #.bin");
        { std::ofstream output(externalPath,std::ios::binary);
          output.write(reinterpret_cast<const char*>(positions.data()),std::streamsize(positions.size())); }
        auto external=document;external["buffers"][0]["uri"]="mesh%20%C3%A9%20%23.bin";
        const auto externalText=external.dump();
        triangle(Bytes(externalText.begin(),externalText.end()),"escaped Unicode external buffer filename");
        for(const auto* suffix : {"%00ignored","%","%2","%ZZ"}) {
            external["buffers"][0]["uri"]=std::string("mesh%20%C3%A9%20%23.bin")+suffix;
            const auto text=external.dump();rejects(Bytes(text.begin(),text.end()),"invalid external buffer URI");
        }
        auto changed = document;
        auto indexedDoc = document;
        auto indexedBytes = positions;
        for (uint32_t value : {0u, 1u, 2u}) put32(indexedBytes, value);
        indexedDoc["buffers"][0]["byteLength"] = indexedBytes.size();
        indexedDoc["bufferViews"].push_back({{"buffer", 0}, {"byteOffset", 36}, {"byteLength", 12}});
        indexedDoc["accessors"].push_back({{"bufferView", 1}, {"componentType", 5125}, {"count", 3}, {"type", "SCALAR"}});
        indexedDoc["meshes"][0]["primitives"][0]["indices"] = 1;
        triangle(withDocument(indexedDoc, indexedBytes), "uint32 indexed triangle");
        for (const auto component : {5121, 5123}) {
            changed = indexedDoc; changed["accessors"][1]["componentType"] = component;
            auto packedIndices = positions;
            for (uint8_t value : {0, 1, 2}) {
                packedIndices.push_back(value);
                if (component == 5123) packedIndices.push_back(0);
            }
            changed["bufferViews"][1]["byteLength"] = packedIndices.size() - 36;
            changed["buffers"][0]["byteLength"] = packedIndices.size();
            while (packedIndices.size() % 4) packedIndices.push_back(0);
            triangle(withDocument(changed, packedIndices), "packed unsigned indexed triangle");
            packedIndices[36] = 255;
            if (component == 5123) packedIndices[37] = 255;
            rejects(withDocument(changed, packedIndices), "reserved maximum index");
        }
        changed = indexedDoc; changed["accessors"][1]["componentType"] = 5126;
        rejects(withDocument(changed, indexedBytes), "float triangle indices");
        changed = indexedDoc; changed["accessors"][1]["normalized"] = true;
        rejects(withDocument(changed, indexedBytes), "normalized triangle indices");
        changed=indexedDoc;changed["accessors"][1].erase("type");
        rejects(withDocument(changed,indexedBytes),"triangle accessor missing type");
        changed = indexedDoc; changed["accessors"][1]["componentType"] = 5122;
        rejects(withDocument(changed, indexedBytes), "signed triangle indices");
        changed = indexedDoc; changed["bufferViews"][1]["byteStride"] = 4;
        rejects(withDocument(changed, indexedBytes), "triangle index buffer view defines byteStride");
        changed = indexedDoc; changed["accessors"][1]["count"] = 1; changed["accessors"][1]["type"] = "VEC3";
        rejects(withDocument(changed, indexedBytes), "vector triangle indices");
        changed = indexedDoc; changed["accessors"][1]["count"] = 2;
        rejects(withDocument(changed, indexedBytes), "incomplete indexed triangle");
        auto badIndices = indexedBytes; set32(badIndices, 44, 3);
        rejects(withDocument(indexedDoc, badIndices), "index beyond vertex array");
        badIndices = indexedBytes; set32(badIndices, 44, 16777217);
        try { load(withDocument(indexedDoc, badIndices)); require(false, "accepted large index"); }
        catch (const std::exception& e) {
            require(std::string(e.what()).find("16777217") != std::string::npos, "index lost integer precision"); ++checks;
        }
        changed = document; changed["meshes"][0]["primitives"][0]["attributes"]["POSITION"] = 0.5;
        rejects(withDocument(changed, positions), "fractional attribute accessor reference");
        changed = document;
        changed["bufferViews"][0]["byteLength"] = 12;
        rejects(withDocument(changed, positions), "accessor outside its declared buffer view");
        auto invalidField = [&](const char* collection, const char* key, nlohmann::json value, const char* label) {
            auto doc = document; doc[collection][0][key] = std::move(value);
            rejects(withDocument(doc, positions), label);
        };
        invalidField("buffers", "byteLength", 40, "truncated logical buffer");
        invalidField("buffers", "byteLength", 32, "view outside logical buffer");
        invalidField("bufferViews", "byteOffset", uint64_t(-4), "overflowing view offset");
        invalidField("bufferViews", "byteLength", uint64_t(-1), "overflowing view length");
        invalidField("bufferViews", "byteStride", 4, "overlapping accessor elements");
        invalidField("bufferViews", "byteStride", 13, "unaligned stride");
        invalidField("bufferViews", "byteStride", 0, "explicit zero stride");
        invalidField("bufferViews", "byteStride", uint64_t(-4), "overflowing stride");
        invalidField("accessors", "byteOffset", uint64_t(-4), "overflowing accessor offset");
        invalidField("accessors", "byteOffset", 1, "unaligned accessor offset");
        invalidField("accessors", "count", uint64_t(-1), "overflowing accessor count");
        invalidField("accessors", "count", uint64_t(0x100000003), "count narrowed to int");
        invalidField("accessors", "count", -1, "negative count");
        invalidField("accessors", "count", 3.5, "fractional count");
        invalidField("accessors", "count", 0, "zero count");
        invalidField("accessors", "componentType", 5124, "unknown component type");
        invalidField("accessors", "sparse", {{"count", 1}}, "incomplete sparse data");
        Bytes interleaved(4, 0);
        for (size_t i = 0; i < 3; ++i) {
            interleaved.insert(interleaved.end(), positions.begin() + i * 12, positions.begin() + (i + 1) * 12);
            interleaved.insert(interleaved.end(), 4, 0);
        }
        changed = document;
        changed["buffers"][0]["byteLength"] = interleaved.size();
        changed["bufferViews"][0] = {{"buffer", 0}, {"byteOffset", 4}, {"byteLength", 48}, {"byteStride", 16}};
        triangle(withDocument(changed, interleaved), "interleaved positions with view offset");
        changed["bufferViews"][0]["byteOffset"] = 0;
        changed["bufferViews"][0]["byteLength"] = 52;
        changed["accessors"][0]["byteOffset"] = 4;
        triangle(withDocument(changed, interleaved), "interleaved positions with accessor offset");
        auto padded = positions; padded.insert(padded.end(), 4, 0);
        triangle(withDocument(document, padded), "extra binary bytes outside the logical buffer");

        auto sceneDoc = document;
        sceneDoc["scenes"] = {{{"nodes", {0}}}};
        sceneDoc["nodes"] = {{{"mesh", 0}}};
        triangle(withDocument(sceneDoc, positions), "single-node scene");
        auto skinDoc=sceneDoc;Bytes skinBytes=positions;
        skinBytes.insert(skinBytes.end(),12,0);
        for(size_t vertex=0;vertex<3;++vertex)
            for(float weight:{1.f,0.f,0.f,0.f}) put32(skinBytes,std::bit_cast<uint32_t>(weight));
        skinDoc["buffers"][0]["byteLength"]=skinBytes.size();
        skinDoc["bufferViews"].push_back({{"buffer",0},{"byteOffset",36},{"byteLength",12}});
        skinDoc["bufferViews"].push_back({{"buffer",0},{"byteOffset",48},{"byteLength",48}});
        skinDoc["accessors"].push_back({{"bufferView",1},{"componentType",5121},{"count",3},{"type","VEC4"}});
        skinDoc["accessors"].push_back({{"bufferView",2},{"componentType",5126},{"count",3},{"type","VEC4"}});
        skinDoc["meshes"][0]["primitives"][0]["attributes"]["JOINTS_0"]=1;
        skinDoc["meshes"][0]["primitives"][0]["attributes"]["WEIGHTS_0"]=2;
        skinDoc["nodes"][0]["skin"]=0;
        skinDoc["nodes"].push_back({{"translation",{5,0,0}}});
        skinDoc["skins"]={{{"joints",{1}}}};skinDoc["scenes"][0]["nodes"]={0,1};
        rejects(withDocument(skinDoc,skinBytes),"active skin imported as unposed static geometry");
        skinDoc["nodes"][0].erase("skin");
        triangle(withDocument(skinDoc,skinBytes),"uninstantiated skin declaration");
        skinDoc["nodes"].push_back({{"mesh",0},{"skin",0}});
        triangle(withDocument(skinDoc,skinBytes),"skin node outside selected scene");
        auto morphDoc=sceneDoc;
        morphDoc["meshes"][0]["primitives"][0]["targets"]={{{"POSITION",0}}};
        triangle(withDocument(morphDoc,positions),"zero-default morph base mesh");
        morphDoc["meshes"][0]["weights"]={0.5};
        rejects(withDocument(morphDoc,positions),"active default morph shape");
        morphDoc["nodes"][0]["weights"]={0};
        triangle(withDocument(morphDoc,positions),"node zero weights override active mesh weights");
        morphDoc["meshes"][0]["weights"]={0};morphDoc["nodes"][0]["weights"]={-0.5};
        rejects(withDocument(morphDoc,positions),"active node morph shape");
        morphDoc["nodes"][0]["weights"]={0,0};
        rejects(withDocument(morphDoc,positions),"morph weight count mismatch");
        morphDoc["nodes"][0]["weights"]={"0"};
        rejects(withDocument(morphDoc,positions),"nonnumeric morph weight");
        morphDoc["nodes"][0]["weights"]={1e-100};
        rejects(withDocument(morphDoc,positions),"tiny active morph weight");
        morphDoc.erase("scenes");morphDoc.erase("nodes");
        morphDoc["meshes"][0]["weights"]={0.5};
        rejects(withDocument(morphDoc,positions),"active morph mesh library");
        morphDoc["meshes"][0]["weights"]={-0.0};
        triangle(withDocument(morphDoc,positions),"zero-weight morph mesh library");
        auto decimalScene=sceneDoc;decimalIntegers(decimalIntegers,decimalScene);
        triangle(withDocument(decimalScene,positions),"whole-valued decimal scene/accessor references");
        changed = sceneDoc; changed["nodes"][0]["children"] = {0};
        rejects(withDocument(changed, positions), "self-cycle");
        changed = sceneDoc; changed["nodes"][0]["children"] = {1};
        changed["nodes"].push_back({{"children", {0}}});
        rejects(withDocument(changed, positions), "two-node cycle");
        changed = sceneDoc; changed["scenes"][0]["nodes"] = {0, 0};
        rejects(withDocument(changed, positions), "repeated scene root");
        changed = sceneDoc; changed["nodes"][0]["children"] = {1, 1};
        changed["nodes"].push_back({{"mesh", 0}});
        rejects(withDocument(changed, positions), "repeated child");
        changed = sceneDoc; changed["nodes"][0]["children"] = {-1};
        rejects(withDocument(changed, positions), "negative child index");
        changed = sceneDoc; changed["nodes"][0]["children"] = {0.5};
        rejects(withDocument(changed, positions), "fractional child index");
        changed = sceneDoc; changed["nodes"][0]["children"] = {7};
        rejects(withDocument(changed, positions), "missing child");
        for (const auto* key : {"matrix", "translation", "rotation", "scale"}) {
            changed = sceneDoc; changed["nodes"][0][key] = {1, 2};
            rejects(withDocument(changed, positions), std::string("short node ") + key);
        }
        changed = sceneDoc; changed["nodes"][0]["translation"] = {1e300, 0, 0};
        rejects(withDocument(changed, positions), "non-finite converted transform");
        changed = sceneDoc;
        changed["nodes"][0]["matrix"] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        triangle(withDocument(changed, positions), "matrix node");
        for (size_t component : {3,7,11,15}) {
            auto invalidMatrix=changed;invalidMatrix["nodes"][0]["matrix"][component]=component==15 ? 2 : 1;
            rejects(withDocument(invalidMatrix,positions),"non-affine node matrix");
        }
        for (const auto& rotation : std::vector<nlohmann::json>{{0,0,0,0},{0,0,0,2},{0,0,0.5,0.5}}) {
            auto invalidRotation=sceneDoc;invalidRotation["nodes"][0]["rotation"]=rotation;
            rejects(withDocument(invalidRotation,positions),"non-unit node quaternion");
        }
        for (double tiny : {1e-100,-1e-100}) {
            auto invalidMatrix=changed;invalidMatrix["nodes"][0]["matrix"][3]=tiny;
            rejects(withDocument(invalidMatrix,positions),"non-affine row lost during float conversion");
        }
        for (double sign : {-1.0,1.0}) {
            auto roundedRotation=sceneDoc;roundedRotation["nodes"][0]["rotation"]={0,0,sign*0.707,sign*0.707};
            const auto rotated=load(withDocument(roundedRotation,positions));
            require(std::abs(rotated.prims[0].verts[1].x)<1e-4 && std::abs(rotated.prims[0].verts[1].z-100)<1e-4 &&
                    std::abs(rotated.prims[0].verts[2].x+100)<1e-4,
                    "rounded quaternion introduced scale/shear or changed sign-equivalent rotation");++checks;
        }
        changed["nodes"][0]["translation"] = {0, 0, 0};
        rejects(withDocument(changed, positions), "mixed matrix and TRS node");
        changed = sceneDoc;
        changed["nodes"] = {{{"scale", {1e30, 1e30, 1e30}}, {"children", {1}}}, {{"scale", {1e30, 1e30, 1e30}}, {"mesh", 0}}};
        rejects(withDocument(changed, positions), "overflowing inherited transform");
        changed = sceneDoc;
        changed["nodes"] = {{{"translation", {1, 0, 0}}, {"children", {1}}}, {{"scale", {2, 2, 2}}, {"mesh", 0}}};
        auto transformed = load(withDocument(changed, positions));
        require(transformed.prims.size() == 1 && transformed.prims[0].verts[0].x == 100 &&
                transformed.prims[0].verts[1].x == 300 && transformed.prims[0].verts[2].z == 200,
                "lost inherited scene transform"); ++checks;
        changed = sceneDoc;
        changed["nodes"] = {{{"children", {1, 2}}}, {{"mesh", 0}, {"translation", {1, 0, 0}}}, {{"mesh", 0}, {"translation", {2, 0, 0}}}};
        transformed = load(withDocument(changed, positions));
        require(transformed.prims.size() == 2 && transformed.prims[0].verts[0].x == 100 &&
                transformed.prims[1].verts[0].x == 200, "lost instance order or duplicate mesh instance"); ++checks;
        changed = sceneDoc; changed["nodes"][0]["scale"] = {-1, 1, 1};
        transformed = load(withDocument(changed, positions));
        require(transformed.prims.size() == 1 && transformed.prims[0].verts[1].x == -100 &&
                transformed.prims[0].faces[0][1] == 2 && transformed.prims[0].faces[0][2] == 1,
                "lost mirrored-node winding"); ++checks;
        // A slanted triangle makes a wrong normal transform observable: the
        // result must remain perpendicular to both transformed edges.
        Bytes slanted;
        for (float value : {0.f,0.f,0.f, 0.f,0.f,1.f, 1.f,-1.f,0.f})
            put32(slanted, std::bit_cast<uint32_t>(value));
        for (size_t i=0;i<3;++i) for (float value : {float(std::sqrt(0.5)),float(std::sqrt(0.5)),0.f})
            put32(slanted, std::bit_cast<uint32_t>(value));
        auto normalDoc=sceneDoc;
        normalDoc["buffers"][0]["byteLength"]=slanted.size();
        normalDoc["bufferViews"].push_back({{"buffer",0},{"byteOffset",36},{"byteLength",36}});
        normalDoc["accessors"].push_back({{"bufferView",1},{"componentType",5126},{"count",3},{"type","VEC3"}});
        normalDoc["meshes"][0]["primitives"][0]["attributes"]["NORMAL"]=1;
        for (int transform=0;transform<6;++transform) {
            changed=normalDoc;
            if(transform==1) changed["nodes"][0]["scale"]={2,3,4};
            if(transform==2) changed["nodes"][0]["scale"]={-2,3,4};
            if(transform==3) changed["nodes"][0]["rotation"]={0,0,std::sqrt(0.5),std::sqrt(0.5)};
            if(transform==4) changed["nodes"]={{{"scale",{2,3,4}},{"children",{1}}},
                {{"mesh",0},{"rotation",{0,0,std::sin(0.3),std::cos(0.3)}}}};
            if(transform==5) changed["nodes"][0]["scale"]={-1e-20,2e-20,3e-20};
            const auto normalsModel=load(withDocument(changed,slanted));
            const auto& primitive=normalsModel.prims.at(0);
            using Vec3=forge::meshcompose::Vec3;
            auto subtract=[](Vec3 a,Vec3 b) {return Vec3{a.x-b.x,a.y-b.y,a.z-b.z};};
            auto dot=[](Vec3 a,Vec3 b) {return a.x*b.x+a.y*b.y+a.z*b.z;};
            const auto face=primitive.faces.at(0);
            const auto a=subtract(primitive.verts[face[1]],primitive.verts[face[0]]);
            const auto b=subtract(primitive.verts[face[2]],primitive.verts[face[0]]);
            const Vec3 outward{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
            auto checkNormal=[&](Vec3 n,float tolerance) {
                require(std::abs(dot(n,a))/std::sqrt(dot(a,a))<tolerance &&
                        std::abs(dot(n,b))/std::sqrt(dot(b,b))<tolerance,
                        "scaled normal is not perpendicular to its triangle");
                require(dot(n,outward)>0,"mirrored normal points into its triangle");
            };
            for(const auto& n:primitive.normals) checkNormal(n,1e-5f);
            const auto composed=forge::meshcompose::composeStatic("MESH_NORMAL_TEST",normalsModel.prims,{{}},false);
            const auto decoded=forge::meshpreview::decodeLod0(composed.payload,1);
            for(const auto& vertex:decoded.vertices) checkNormal({vertex.nx,vertex.ny,vertex.nz},0.003f);
            ++checks;
        }
        changed=normalDoc;changed["nodes"][0]["scale"]={2,0,4};
        const auto flatModel=load(withDocument(changed,slanted));
        require(flatModel.prims[0].normals.empty() && !flatModel.notes.empty(),
                "singular transform must request normal reconstruction");
        const auto flatMesh=forge::meshcompose::composeStatic("MESH_FLAT_TEST",flatModel.prims,{{}},false);
        for(const auto& vertex:forge::meshpreview::decodeLod0(flatMesh.payload,1).vertices)
            require(std::abs(vertex.nx)<0.003f && std::abs(vertex.ny)<0.003f && vertex.nz>0.99f,
                    "flattened surface lost geometric normal");
        ++checks;
        // Authored shading directions need not equal the flat face normal.
        auto smooth=slanted;
        for(size_t vertex=0;vertex<3;++vertex) {
            set32(smooth,36+vertex*12,0);set32(smooth,40+vertex*12,0);
            set32(smooth,44+vertex*12,std::bit_cast<uint32_t>(1.f));
        }
        changed=normalDoc;changed["nodes"][0]["scale"]={2,3,4};
        const auto smoothModel=load(withDocument(changed,smooth));
        require(smoothModel.prims[0].normals[0].y==-1 && smoothModel.prims[0].normals[0].x==0 &&
                smoothModel.prims[0].normals[0].z==0,"discarded authored smooth normals");++checks;
        set32(smooth,44,0);rejects(withDocument(changed,smooth),"zero authored normal");
        set32(smooth,44,0x7fc00000);rejects(withDocument(changed,smooth),"NaN authored normal");
        changed = sceneDoc; changed["nodes"] = nlohmann::json::array();
        for (size_t i = 0; i < 12000; ++i) changed["nodes"].push_back({{"children", {i + 1}}});
        changed["nodes"].push_back({{"mesh", 0}});
        triangle(withDocument(changed, positions), "12001-node hierarchy");
        changed = sceneDoc; changed["nodes"][0].erase("mesh");
        require(load(withDocument(changed, positions)).prims.empty(), "meshless scene imported unused library mesh"); ++checks;
        changed = sceneDoc; changed["scenes"][0].erase("nodes");
        require(load(withDocument(changed, positions)).prims.empty(), "empty scene imported unused library mesh"); ++checks;

        auto bad = good; set32(bad, 4, 1); rejects(bad, "GLB version 1");
        bad = good; set32(bad, 8, uint32_t(good.size() - 4)); rejects(bad, "short declared length");
        bad = good; set32(bad, 8, uint32_t(good.size() + 4)); rejects(bad, "long declared length");
        bad = good; set32(bad, 12, 0xfffffff0); rejects(bad, "overflowing JSON chunk length");
        bad = good; set32(bad, binaryHeader, uint32_t(positions.size() + 4)); rejects(bad, "truncated binary chunk");
        bad = good; set32(bad, 16, 0x004e4942); rejects(bad, "BIN before JSON");
        bad = good; chunk(bad, 0x004e4942, positions); rejects(bad, "duplicate binary chunk");
        bad = good; chunk(bad, 0x4e4f534a, Bytes(json.begin(), json.end())); rejects(bad, "duplicate JSON chunk");
        bad = good; bad.push_back(0); set32(bad, 8, uint32_t(bad.size())); rejects(bad, "incomplete trailing chunk header");
        bad = good; chunk(bad, 0x12345678, {1, 2, 3, 4}); triangle(bad, "unknown extension chunk");
        bad = good; chunk(bad, 0x12345678, {1, 2, 3}); rejects(bad, "unaligned extension chunk");
        bad = good; set32(bad, 12, uint32_t(json.size() - 1)); rejects(bad, "unaligned JSON chunk");
        for (size_t length = 0; length < good.size(); ++length) {
            bad.assign(good.begin(), good.begin() + length);
            if (length >= 12) set32(bad, 8, uint32_t(length));
            rejects(bad, "prefix of length " + std::to_string(length));
        }
        const auto objPath = dir / "triangle.obj";
        auto loadObj = [&](const std::string& text) {
            { std::ofstream f(objPath); f << text; }
            return albion::meshimport::loadModel(objPath);
        };
        const std::string objVertices = "v 0 0 0\nv 1 0 0\nv 0 1 0\n";
        auto rejectsObj = [&](const std::string& text, const std::string& label) {
            bool rejected = false;
            try { loadObj(text); } catch (const std::exception&) { rejected = true; }
            require(rejected, "accepted OBJ " + label); ++checks;
        };
        rejectsObj("v 0 0\n" + objVertices + "f 1 2 3\n", "short position");
        rejectsObj("vn 0 0\n" + objVertices + "f 1//1 2//1 3//1\n", "short normal");
        rejectsObj("vt\n" + objVertices + "f 1/1 2/1 3/1\n", "empty UV");
        for (const auto* suffix : {"junk", ".5", "/2"}) {
            rejectsObj("v 0 0 1.0"+std::string(suffix)+"\n"+objVertices+"f 1 2 3\n", "partial position scalar");
            rejectsObj("vn 0 0 1.0"+std::string(suffix)+"\n"+objVertices+"f 1//1 2//1 3//1\n", "partial normal scalar");
            rejectsObj("vt 0.25 1.0"+std::string(suffix)+"\n"+objVertices+"f 1/1 2/1 3/1\n", "partial UV scalar");
        }
        rejectsObj("v 1e38 0 0\n" + objVertices + "f 1 2 3\n", "overflowing converted position");
        for (const auto* face : {"1x 2 3", "0 2 3", "-4 2 3", "4 2 3", "1/1 2/1 3/1",
                                 "1//1 2//1 3//1", "1/0 2 3", "1//0 2 3", "1/1/1/1 2 3", "1 2"})
            rejectsObj(objVertices + "f " + face + "\n", std::string("invalid face ") + face);
        const auto obj = loadObj(objVertices + "vt 0.25\nvn 0 0 1\nf -3/1/1 -2/1/1 -1/1/1 # triangle\n");
        require(obj.prims.size() == 1 && obj.prims[0].faces.size() == 1 && obj.prims[0].verts[1].x == 100 &&
                obj.prims[0].uvs[0].u == 0.25f && obj.prims[0].uvs[0].v == 1 &&
                obj.prims[0].normals[0].y == -1, "lost OBJ relative indices, optional UV or comment"); ++checks;
        const auto signedObj=loadObj("v +0 -0 0e0 1\nv +1.0 0E+2 -.0\nv .0 +1 0\n"
                                     "vt +2.5e-1 +.5 0\nvn +0.0 -0 +1.0\nf 1/1/1 2/1/1 3/1/1");
        require(signedObj.prims[0].verts[1].x==100 && signedObj.prims[0].uvs[0].u==0.25f &&
                signedObj.prims[0].uvs[0].v==0.5f && signedObj.prims[0].normals[0].y==-1,
                "lost OBJ signed/exponent scalars or optional components");++checks;
        const std::array<std::array<double,2>,8> ring{{{0,0},{3,0},{3,3},{2,3},{2,1},{1,1},{1,3},{0,3}}};
        for(int plane=0;plane<4;++plane) {
            std::ostringstream source;
            for(const auto& p:ring) {
                if(plane==0) source<<"v "<<p[0]<<' '<<p[1]<<" 0\n";
                else if(plane==1) source<<"v 0 "<<p[0]<<' '<<p[1]<<"\n";
                else if(plane==2) source<<"v "<<p[0]<<" 0 "<<p[1]<<"\n";
                else source<<"v "<<p[0]<<' '<<p[1]<<' '<<p[0]+p[1]<<"\n";
            }
            for(size_t i=0;i<8;++i) source<<"vt "<<double(i+1)/8<<" 0\n";
            source<<"vn 0 0 1\n";
            for(bool reversed:{false,true}) for(size_t start=0;start<8;++start) {
                std::ostringstream face;face<<"f";
                std::array<size_t,8> order{};
                for(size_t i=0;i<8;++i) {
                    order[i]=(start+(reversed?8-i:i))%8;
                    face<<' '<<order[i]+1<<'/'<<order[i]+1<<"/1";
                }
                face<<'\n';
                const auto concave=loadObj(source.str()+face.str());
                const auto& primitive=concave.prims.at(0);
                double area=0;
                const std::array<double,3> direction=plane==0?std::array<double,3>{0,-1,0}:
                    plane==1?std::array<double,3>{1,0,0}:plane==2?std::array<double,3>{0,0,-1}:
                    std::array<double,3>{-1,-1,-1};
                for(const auto& triangle:primitive.faces) {
                    const auto& a=primitive.verts[triangle[0]];const auto& b=primitive.verts[triangle[1]];const auto& c=primitive.verts[triangle[2]];
                    const double ux=double(b.x)-a.x,uy=double(b.y)-a.y,uz=double(b.z)-a.z;
                    const double vx=double(c.x)-a.x,vy=double(c.y)-a.y,vz=double(c.z)-a.z;
                    const double x=uy*vz-uz*vy,y=uz*vx-ux*vz,z=ux*vy-uy*vx;
                    area+=std::sqrt(x*x+y*y+z*z)/20000.0;
                    require((reversed?-1:1)*(x*direction[0]+y*direction[1]+z*direction[2])>0,"concave OBJ winding changed or triangle collapsed");
                }
                require(primitive.faces.size()==6 && std::abs(area-7.0*(plane==3?std::sqrt(3.0):1.0))<1e-6,"concave OBJ triangles overlap or cover outside polygon");
                require(primitive.uvs.size()==8 && primitive.normals.size()==8,"concave OBJ lost corner attributes");
                for(size_t i=0;i<8;++i) require(primitive.uvs[i].u==double(order[i]+1)/8 && primitive.uvs[i].v==1 && primitive.normals[i].y==-1,"concave OBJ corner attributes changed");
                ++checks;
            }
        }
        const auto convexObj=loadObj("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3 4\n");
        require(convexObj.prims[0].faces==std::vector<std::array<uint32_t,3>>{{0,1,2},{0,2,3}},"convex OBJ fan output changed");++checks;
        // A corner along a straight edge can carry distinct UVs: retain it without
        // producing the collapsed triangle that a fan from that edge creates.
        const std::vector<std::vector<std::array<int,2>>> straightEdges{
            {{0,0},{1,0},{2,0},{2,2},{0,2}},
            {{0,0},{1,0},{3,0},{3,3},{2,3},{2,1},{1,1},{1,3},{0,3}}};
        for(size_t shape=0;shape<straightEdges.size();++shape) {
        const auto& straightEdge=straightEdges[shape];const auto corners=straightEdge.size();
        for(bool reversed:{false,true}) for(size_t start=0;start<corners;++start) {
            std::ostringstream source;
            for(const auto& p:straightEdge) source<<"v "<<p[0]<<' '<<p[1]<<" 0\n";
            for(size_t i=0;i<corners;++i) source<<"vt "<<double(i)/16<<" 0\n";
            source<<"f";
            for(size_t i=0;i<corners;++i) {
                const auto corner=(start+(reversed?corners-i:i))%corners;
                source<<' '<<corner+1<<'/'<<corner+1;
            }
            source<<'\n';
            const auto model=loadObj(source.str());const auto& primitive=model.prims.at(0);
            double area=0;std::vector<bool> used(corners);
            for(const auto& triangle:primitive.faces) {
                const auto& a=primitive.verts[triangle[0]];const auto& b=primitive.verts[triangle[1]];const auto& c=primitive.verts[triangle[2]];
                const double signedArea=((double(b.x)-a.x)*(double(c.z)-a.z)-(double(b.z)-a.z)*(double(c.x)-a.x))/20000;
                require((reversed?-1:1)*signedArea>0,"OBJ straight edge produced collapsed or reversed triangle");
                area+=std::abs(signedArea);
                for(const auto index:triangle) used[index]=true;
            }
            require(primitive.faces.size()==corners-2 && area==(shape==0?4:7) && primitive.uvs.size()==corners &&
                    std::all_of(used.begin(),used.end(),[](bool value){return value;}),"OBJ straight edge lost area or attributed corner");++checks;
        }
        }
        rejectsObj("v 0 0 0\nv 1 1 0\nv 0 1 0\nv 1 0 0\nf 1 2 3 4\n","self-intersecting polygon");
        const auto regularPolygon=[](size_t corners) {
            std::ostringstream source;
            for(size_t i=0;i<corners;++i) {
                const double angle=2*std::acos(-1.0)*double(i)/double(corners);
                source<<"v "<<std::cos(angle)<<' '<<std::sin(angle)<<" 0\n";
            }
            source<<"f";
            for(size_t i=0;i<corners;++i) source<<' '<<i+1;
            source<<'\n';
            return source.str();
        };
        require(loadObj(regularPolygon(1024)).prims[0].faces.size()==1022,"OBJ face corner boundary refused");++checks;
        rejectsObj(regularPolygon(1025),"unbounded face size");
        rejectsObj("v 0 0 1\nv 3 0 0\nv 3 3 0\nv 2 3 0\nv 2 1 0\nv 1 1 0\nv 1 3 0\nv 0 3 0\nf 1 2 3 4 5 6 7 8\n","non-planar concave polygon");
        const auto clippedObj=loadObj(objVertices+"vt 8 0\nf 1/1 2/1 3/1\n");
        require(clippedObj.notes.size()==1 && clippedObj.notes[0].find("3 vertices")!=std::string::npos,
                "OBJ UV clipping was not reported");++checks;
        require(signedObj.notes.empty(),"ordinary OBJ texture coordinates reported clipping");++checks;
        std::string largeObj;
        for (size_t i=0;i<65536;++i) largeObj += "v 0 0 0\n";
        for (size_t i=1;i<=65535;i+=3)
            largeObj += "f " + std::to_string(i) + " " + std::to_string(i+1) + " " + std::to_string(i+2) + "\n";
        require(loadObj(largeObj).prims[0].verts.size()==65535,"lost OBJ primitive boundary");++checks;
        rejectsObj(largeObj+"f 1 2 65536\n","primitive vertex overflow");
        fs::remove(objPath); fs::remove(path); fs::remove(externalPath); fs::remove(dir);
        std::cout << "mesh input: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << " (fixture retained in " << dir.string() << ")\n";
        return 1;
    }
}
