// Pure CPU checks: this executable never creates a window or D3D device.
#include "renderer.hpp"
#include "cutoutcache.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <cstddef>

using albion::gui::Renderer;
namespace fe = albion::foliageexport;
namespace te = albion::terrainexport;
static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main() {
    try {
        {
            using albion::cutoutmips::Cache;
            te::Image a; a.name="a"; a.width=a.height=8; a.rgba.assign(8*8*4,255);
            Cache cache(640,2);
            auto original=cache.acquire(a);
            auto same=cache.acquire(a);
            check(original==same && cache.stats().hits==1 && cache.stats().builds==1, "cutout cache did not reuse identical pixels");
            const auto expected=albion::cutoutmips::build(a);
            check(original->size()==expected.size() && original->at(0).rgba==expected[0].rgba, "cached mip differs from direct build");
            a.rgba[0]=0;
            auto replaced=cache.acquire(a);
            check(replaced!=original && cache.stats().entries==1 && cache.stats().builds==2, "same-name replacement reused stale cutout");
            check(original->at(0).rgba==expected[0].rgba, "replacement mutated an in-flight chain");
            te::Image b=a; b.name="b"; cache.acquire(b);
            cache.acquire(a); // make A newer than B
            te::Image c=a; c.name="c"; cache.acquire(c);
            const auto builds=cache.stats().builds;
            check(cache.acquire(a)==replaced && cache.stats().builds==builds, "LRU discarded the recently used chain");
            cache.acquire(b);
            check(cache.stats().builds==builds+1 && cache.stats().bytes<=640, "LRU did not evict the oldest chain within budget");
            te::Image large=a; large.width=large.height=32; large.rgba.assign(32*32*4,255);
            auto huge=cache.acquire(large);
            check(!huge->empty() && cache.stats().bytes<=640 && cache.stats().entries==2, "oversized cache request disturbed bounded residency");
            auto uncached=cache.acquire(a,false);
            check(cache.stats().bytes==0 && cache.stats().entries==0 && uncached->at(0).rgba==replaced->at(0).rgba, "cache bypass changed pixels or retained payload");
            a.rgba.pop_back();
            check(cache.acquire(a)->empty() && cache.stats().bytes==0, "invalid image entered the cutout cache");
            Cache empty(0); a.rgba.push_back(255); empty.acquire(a);
            check(empty.stats().bytes==0 && empty.stats().entries==0, "zero budget retained derived texture data");
        }
        {
            albion::gui::Camera camera;
            camera.posX=3398.75f; camera.posY=130; camera.posZ=-3612.5f;
            camera.yaw=.46365f; camera.pitch=.5f;
            float reference[16]; camera.view(reference);
            for(float distance : {.5f,5.0f,200.0f,8000.0f,20000.0f}) {
                camera.distance=distance;
                float view[16]; camera.view(view);
                for(int i=0;i<16;++i) check(view[i]==reference[i], "orbit distance perturbed view matrix");
            }
            float direction[3]; camera.dir(direction);
            for(int axis=0;axis<3;++axis) {
                const float projected=direction[0]*reference[axis]+direction[1]*reference[4+axis]+direction[2]*reference[8+axis];
                check(std::abs(projected-(axis==2 ? -1.0f : 0.0f))<1e-6f, "picking direction does not point down view centre");
            }
        }
        static_assert(sizeof(Renderer::LayerVertex) == 40);
        static_assert(offsetof(Renderer::LayerVertex, coarseNormal) == 36);
        for (const float v : {-1.0f, -.83f, -.2f, 0.0f, .33f, .8f, 1.0f}) {
            const auto packed = Renderer::packNormal(v, -v, v * .5f);
            const float expected[] = {v, -v, v * .5f};
            for (int axis = 0; axis < 3; ++axis) {
                const float decoded = float((packed >> (10 * axis)) & 1023) * (2.0f / 1023.0f) - 1;
                check(std::fabs(decoded - expected[axis]) <= .00098f, "normal quantization exceeded half a step");
            }
        }
        check((Renderer::packNormal(-2, 2, 0) & 1023) == 0, "normal lower clamp");
        check(((Renderer::packNormal(-2, 2, 0) >> 10) & 1023) == 1023, "normal upper clamp");
        {
            check(albion::texturePayloadBytes(4, 4, true) == 84, "mip payload accounting");
            check(albion::texturePayloadBytes(3, 5, true) == 72, "odd mip dimensions accounting");
            check(albion::texturePayloadBytes(1, 8, true) == 60, "narrow mip dimensions accounting");
            check(albion::texturePayloadBytes(0, 8, true) == 0, "empty mip accounting");
            albion::TexturePool<int> pool;
            te::Image image; image.name = "replacement"; image.width = image.height = 2;
            image.rgba.assign(16, 255);
            int creates = 0, destroys = 0;
            auto factory = [&] { ++creates; return std::shared_ptr<int>(new int(7), [&](int* p) { ++destroys; delete p; }); };
            auto a = pool.acquire(image, 0, 16, true, factory);
            auto b = pool.acquire(image, 0, 16, true, factory);
            check(a == b && creates == 1 && pool.stats().hits == 1, "texture sharing failed");
            image.rgba[0] = 0;
            auto replacement = pool.acquire(image, 0, 16, true, factory);
            check(replacement != a, "same-name replacement reused stale pixels");
            auto policy = pool.acquire(image, 1, 20, true, factory);
            check(policy != replacement, "sampling policies aliased");
            image.width = 1; image.height = 4;
            auto shape = pool.acquire(image, 1, 20, true, factory);
            check(shape != policy, "texture shapes aliased");
            auto unshared = pool.acquire(image, 1, 20, false, factory);
            check(unshared != shape, "diagnostic sharing bypass failed");
            a.reset(); check(destroys == 0, "shared texture released early");
            b.reset(); check(destroys == 1, "last owner retained texture");
            replacement.reset(); policy.reset(); shape.reset(); unshared.reset(); pool.sweep();
            check(pool.stats().allocations == 0 && destroys == creates, "pool retained unused resources");
            auto failed = pool.acquire(image, 0, 16, true, [] { return std::shared_ptr<int>{}; });
            check(!failed && pool.stats().allocations == 0, "failed allocation cached");
            image.rgba.pop_back();
            check(!pool.acquire(image, 0, 16, true, factory), "truncated texture accepted");
        }
        {
            te::Image leaf; leaf.width=leaf.height=32; leaf.rgba.resize(32*32*4);
            for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
                auto* p=&leaf.rgba[(y*32+x)*4];
                p[0]=255; p[1]=0; p[2]=255; p[3]=0; // hostile transparent RGB
                if((x-16)*(x-16)+(y-16)*(y-16)<100) { p[0]=0; p[1]=200; p[2]=0; p[3]=255; }
            }
            const auto original=leaf.rgba;
            const float coverage=albion::cutoutmips::coverage(leaf);
            auto chain=albion::cutoutmips::build(leaf);
            check(!chain.empty(), "leaf fixture did not generate mips");
            check(leaf.rgba==original, "authored cutout pixels modified");
            uint32_t width=leaf.width, height=leaf.height;
            for(const auto& mip:chain) {
                width=std::max(1u,width/2); height=std::max(1u,height/2);
                check(mip.width==width && mip.height==height, "cutout mip shape");
                check(std::abs(albion::cutoutmips::coverage(mip)-coverage)<=.030001f, "cutout coverage drift");
                for(size_t i=0;i<mip.rgba.size();i+=4) if(mip.rgba[i+3])
                    check(mip.rgba[i]==0 && mip.rgba[i+1]==200 && mip.rgba[i+2]==0, "transparent RGB leaked into leaves");
            }
            for(size_t i=3;i<leaf.rgba.size();i+=4) leaf.rgba[i]=0;
            for(const auto& mip:albion::cutoutmips::build(leaf)) check(albion::cutoutmips::coverage(mip)==0, "transparent mip became visible");
            for(size_t i=3;i<leaf.rgba.size();i+=4) leaf.rgba[i]=255;
            for(const auto& mip:albion::cutoutmips::build(leaf)) check(albion::cutoutmips::coverage(mip)==1, "opaque cutout lost coverage");
            for(size_t i=3;i<leaf.rgba.size();i+=4) leaf.rgba[i]=0;
            leaf.rgba[(16*32+16)*4+3]=255;
            check(albion::cutoutmips::build(leaf).empty(), "unrepresentable sparse leaf did not retain authored mip");
            te::Image odd; odd.width=3; odd.height=1; odd.rgba.assign(12,0); odd.rgba[11]=255;
            auto reduced=albion::cutoutmips::reduce(odd);
            check(reduced.width==1 && reduced.height==1 && reduced.rgba[3]==85, "odd/narrow edge omitted from mip");
            odd.rgba.pop_back(); check(albion::cutoutmips::build(odd).empty(), "invalid cutout accepted");
        }
        {
            const float basis[3][3]={{2,0,0},{1,3,0},{0,1,.5f}};
            float normals[3][3]; Renderer::normalBasis(basis,normals);
            float n[3], tangentA[3], tangentB[3];
            for(int axis=0;axis<3;++axis) {
                n[axis]=normals[0][axis]+normals[1][axis]+normals[2][axis];
                tangentA[axis]=basis[0][axis]-basis[1][axis];
                tangentB[axis]=basis[1][axis]-basis[2][axis];
            }
            float a=0,b=0;
            for(int axis=0;axis<3;++axis) { a+=n[axis]*tangentA[axis]; b+=n[axis]*tangentB[axis]; }
            check(std::abs(a)<1e-6f && std::abs(b)<1e-6f, "scaled/sheared normal no longer perpendicular to surface");
            fe::Scene scaledScene;
            fe::Mesh triangle;
            triangle.geometry.vertices={{0,0,0,1,1,1,0,0},{1,-1,0,1,1,1,1,0},{0,1,-1,1,1,1,0,1}};
            fe::SubMesh face; face.indices={0,1,2}; triangle.parts.push_back(face);
            scaledScene.meshes.push_back(triangle);
            fe::Instance instance; instance.mesh=0; instance.hasMatrix=true;
            for(int row=0;row<3;++row) for(int axis=0;axis<3;++axis) instance.m[row*3+axis]=basis[row][axis];
            scaledScene.instances.push_back(instance);
            const auto prepared=Renderer::prepareLayer(scaledScene,te::UpAxis::Z);
            const auto& v=prepared[0].vertices;
            for(int i=1;i<3;++i) check(std::abs(v[0].nx*(v[i].px-v[0].px)+v[0].ny*(v[i].py-v[0].py)+v[0].nz*(v[i].pz-v[0].pz))<1e-6f,
                "baked world mesh failed to use inverse-transpose normals");
            const float reflected[3][3]={{-2,0,0},{0,1,0},{0,0,1}};
            Renderer::normalBasis(reflected,normals);
            check(normals[0][0]<0 && normals[1][1]>0 && normals[2][2]>0, "reflection inverted normal direction");
            const float tiny[3][3]={{1e-20f,0,0},{0,1e-20f,0},{0,0,1e-20f}};
            Renderer::normalBasis(tiny,normals);
            check(normals[0][0]==1 && normals[1][1]==1 && normals[2][2]==1, "tiny transform lost normal precision");
            const float collapsed[3][3]={}; Renderer::normalBasis(collapsed,normals);
            for(const auto& row:normals) for(float value:row) check(std::isfinite(value), "degenerate normal produced NaN");
        }
        {
            using namespace albion::worldview;
            constexpr uint64_t GiB=1024ull*1024*1024;
            check(targetBytes(100,100,4)==360000, "MSAA target memory accounting");
            check(aaSamples(4,7,true,8*GiB,2*GiB,0,1920,1080,1)==4, "ample memory rejected 4x");
            check(aaSamples(4,3,true,8*GiB,2*GiB,0,1920,1080,1)==2, "unsupported 4x was selected");
            check(aaSamples(4,7,false,8*GiB,0,0,1920,1080,4)==1, "unknown budget must fall back");
            check(aaSamples(4,7,true,GiB,GiB,0,1920,1080,4)==1, "exhausted budget must fall back");
            check(aaSamples(1,7,true,8*GiB,0,0,1920,1080,4)==1, "off ignored");
            check(aaSamples(4,7,true,GiB,0,0,3840,2160,1)==1, "large target bypassed memory cap");
            AaBudget budget;
            for(int i=0;i<50;++i) budget.observe(.04f,false);
            check(budget.samples==4, "loading changed AA budget");
            for(int i=0;i<40;++i) budget.observe(.04f,true);
            check(budget.samples==2, "slow frames did not lower AA");
            for(int i=0;i<40;++i) budget.observe(.04f,true);
            check(budget.samples==1, "AA did not reach minimum");
            for(int i=0;i<1010;++i) budget.observe(.01f,true);
            check(budget.samples==2, "sustained fast frames did not restore AA");
            for(int i=0;i<610;++i) budget.observe(1.0f/60,true);
            check(budget.samples==4, "60 Hz VSync prevented AA recovery");
            budget.samples=1;
            for(int i=0;i<600;++i) budget.observe(1.0f/50,true);
            check(budget.samples==1, "marginal 50 FPS incorrectly raised AA");
            for(int i=0;i<500;++i) budget.observe(1.0f/60,true);
            budget.observe(1.0f/60,false);
            for(int i=0;i<120;++i) budget.observe(1.0f/60,true);
            check(budget.samples==1, "interrupted headroom window was not reset");
        }
        fe::Scene scene;
        fe::Mesh mesh;
        mesh.geometry.vertices = {{0,0,0,0,0,1,0,0}, {1,0,0,0,0,1,1,0},
                                  {1,1,0,0,0,1,1,1}, {0,1,0,0,0,1,0,1}};
        fe::SubMesh part;
        part.indices = {0,1,2,0,2,3,0,999,1,2}; // invalid triangle + trailing index skipped
        mesh.parts.push_back(part);
        scene.meshes.push_back(mesh);
        fe::Instance inst;
        inst.mesh = 0; inst.x = 10; inst.y = 20; inst.z = 30;
        scene.instances.push_back(inst);
        inst.x = 40;
        scene.instances.push_back(inst);
        inst.mesh = 99;
        scene.instances.push_back(inst);
        auto batches = Renderer::prepareLayer(scene, te::UpAxis::Y);
        check(batches.size() == 1, "material batching changed");
        const auto& b = batches.front();
        check(b.vertices.size() == 8 && b.indices.size() == 12, "shared vertices were expanded or triangles lost");
        const uint32_t sequence[] = {0,1,2,0,2,3,4,5,6,4,6,7};
        for (size_t i = 0; i < b.indices.size(); ++i) check(b.indices[i] == sequence[i], "triangle order changed");
        check(b.vertices[0].px == 10 && b.vertices[0].py == 30 && b.vertices[0].pz == -20, "first transform");
        check(b.vertices[4].px == 40 && b.vertices[4].ny == 1 && b.vertices[4].walk == 1, "second transform/normal");
        check(b.lo[0] == 10 && b.hi[0] == 41 && b.lo[2] == -21 && b.hi[2] == -20, "culling bounds");
        check(b.vertices[2].u == 1 && b.vertices[2].v == 1, "UV preservation");
        scene.meshes[0].parts[0].hasAlpha = true;
        part.indices = {0,1,2}; part.image = 3;
        scene.meshes[0].parts.push_back(part);
        check(Renderer::prepareLayer(scene, te::UpAxis::Z).size() == 2, "alpha/material separation");

        {
            fe::Scene lodScene;
            fe::Mesh base;
            base.geometry.vertices={{-1,-1,0,0,0,1,0,0},{1,-1,0,0,0,1,1,0},
                                    {1,1,2,0,0,1,1,1},{-1,1,2,0,0,1,0,1}};
            fe::SubMesh opaque,leaf;
            opaque.image=0; opaque.indices={0,1,2};
            leaf.image=1; leaf.hasAlpha=true; leaf.indices={0,2,3};
            base.parts={opaque,leaf};
            fe::Mesh lower;
            lower.geometry.vertices={{-2,-2,-2,0,0,1,0,0},{2,-2,4,0,0,1,1,0},{0,3,0,0,0,1,0,1}};
            leaf.indices={0,1,2}; lower.parts={opaque,leaf}; base.lods.push_back(lower);
            lodScene.meshes.push_back(base);
            fe::Instance transformed;
            transformed.mesh=0; transformed.x=10; transformed.y=20; transformed.z=30; transformed.hasMatrix=true;
            const float matrix[9]={0,2,0,-3,0,0,0,0,.5f};
            std::copy(std::begin(matrix),std::end(matrix),transformed.m);
            lodScene.instances.push_back(transformed);
            transformed.x=110; lodScene.instances.push_back(transformed);
            const auto original=Renderer::prepareLayer(lodScene,te::UpAxis::Y);
            const auto prepared=Renderer::prepareLayer(lodScene,te::UpAxis::Y,true);
            const auto coarse=Renderer::prepareLayer(lodScene,te::UpAxis::Y,true,true);
            const auto again=Renderer::prepareLayer(lodScene,te::UpAxis::Y);
            check(prepared.size()==2 && original.size()==2,"LOD material grouping changed");
            check(coarse.size()==prepared.size(),"scenery-only preparation omitted a material");
            for(size_t material=0;material<prepared.size();++material) {
                const auto& batch=prepared[material];
                check(batch.objects.size()==4 && batch.indices.size()==12,"LOD ranges omitted a material or instance");
                const auto& scenery=coarse[material];
                check(scenery.objects.size()==2 && scenery.indices.size()==6,"scenery-only ranges omitted or duplicated an instance");
                check(scenery.image==batch.image && scenery.alpha==batch.alpha,"scenery-only material or cutout classification changed");
                check(scenery.vertices.size()<batch.vertices.size() && scenery.indices.size()<batch.indices.size(),
                      "scenery-only preparation retained the full LOD chain payload");
                for(size_t instance=0;instance<scenery.objects.size();++instance) {
                    const auto& range=scenery.objects[instance];
                    const auto& fullRange=batch.objects[instance*2+1];
                    check(range.lod==1 && range.first==instance*3 && range.count==3,"scenery-only range did not select the final authored level");
                    check(range.nearPixels==0 && range.farPixels==1.5f,"scenery-only level cannot cover the near/full-detail fallback range");
                    check(range.radius==fullRange.radius && range.center[0]==fullRange.center[0] &&
                          range.center[1]==fullRange.center[1] && range.center[2]==fullRange.center[2],
                          "scenery-only bounds differ from the common full-chain transition sphere");
                    for(size_t i=0;i<range.count;++i) {
                        check(scenery.indices[range.first+i]<scenery.vertices.size() &&
                              batch.indices[fullRange.first+i]<batch.vertices.size(),"scenery comparison references an invalid vertex");
                        const auto& vertex=scenery.vertices[scenery.indices[range.first+i]];
                        const auto& fullVertex=batch.vertices[batch.indices[fullRange.first+i]];
                        check(vertex.walk==float(instance),"scenery-only vertex references a discarded LOD range");
                        check(vertex.px==fullVertex.px && vertex.py==fullVertex.py && vertex.pz==fullVertex.pz &&
                              vertex.u==fullVertex.u && vertex.v==fullVertex.v && vertex.nx==fullVertex.nx &&
                              vertex.ny==fullVertex.ny && vertex.nz==fullVertex.nz,
                              "scenery-only preparation changed authored geometry or attributes");
                    }
                }
                check(batch.alpha==(material==1),"LOD cutout material metadata changed");
                for(size_t rangeIndex=0;rangeIndex<batch.objects.size();++rangeIndex) {
                    const auto& range=batch.objects[rangeIndex];
                    check(range.first==rangeIndex*3 && range.count==3 && range.lod==rangeIndex%2,"LOD index range or level incorrect");
                    check(std::abs(range.center[0]-(8.5f+(rangeIndex/2)*100))<1e-5f &&
                          range.center[1]==30.5f && range.center[2]==-20,"transformed common LOD bounds incorrect");
                    check(std::abs(range.radius-std::sqrt(74.5f))<1e-5f,"LOD sphere omits lower-level extent");
                    const auto& other=prepared[1-material].objects[rangeIndex];
                    check(range.radius==other.radius && range.center[0]==other.center[0] &&
                          range.center[1]==other.center[1] && range.center[2]==other.center[2],"materials have different LOD transition bounds");
                    check((range.lod==0 && range.nearPixels==0 && range.farPixels==180) ||
                          (range.lod==1 && range.nearPixels==180 && range.farPixels==1.5f),"adjacent LOD boundaries disagree");
                    for(size_t i=range.first;i<range.first+range.count;++i) {
                        check(batch.indices[i]<batch.vertices.size(),"LOD range references invalid vertex");
                        const auto& vertex=batch.vertices[batch.indices[i]];
                        check(vertex.walk==float(rangeIndex),"vertex points at another object's bounds");
                        const float dx=vertex.px-range.center[0],dy=vertex.py-range.center[1],dz=vertex.pz-range.center[2];
                        check(dx*dx+dy*dy+dz*dz<=range.radius*range.radius+0.001f,"LOD vertex outside common sphere");
                    }
                }
                check(original[material].objects.empty() && original[material].indices.size()==6,"default path acquired preview LODs");
                check(original[material].indices==again[material].indices && original[material].vertices.size()==again[material].vertices.size(),"LOD preparation mutated source geometry");
                for(size_t i=0;i<original[material].vertices.size();++i) {
                    const auto& a=original[material].vertices[i]; const auto& b=again[material].vertices[i];
                    check(a.px==b.px && a.py==b.py && a.pz==b.pz && a.u==b.u && a.v==b.v && a.walk==1 && b.walk==1,
                          "default transform, UV or walk attribute changed after LOD preparation");
                }
            }
            check(lodScene.meshes[0].geometry.vertices.size()==4 && lodScene.meshes[0].lods[0].geometry.vertices.size()==3 &&
                  lodScene.meshes[0].parts[1].indices==base.parts[1].indices,"authored mesh modified by preview LOD preparation");
            lodScene.meshes[0].lods.clear();
            const auto baseFallback=Renderer::prepareLayer(lodScene,te::UpAxis::Y,true,true);
            check(baseFallback.size()==original.size(),"base-only scenery fallback lost a material");
            for(size_t material=0;material<baseFallback.size();++material) {
                const auto& batch=baseFallback[material];
                check(batch.objects.size()==2 && batch.indices==original[material].indices,
                      "base-only scenery fallback changed instance topology");
                for(const auto& range:batch.objects)
                    check(range.lod==0 && range.nearPixels==0 && range.farPixels==1.5f,
                          "base-only scenery fallback has invalid visibility thresholds");
            }
        }

        te::WaterMesh water;
        water.positions = {0,2,0, 1,2,0, 1,2,1, 0,2,1};
        water.fade = {0,.5f,1,1}; water.ice = {0,0,1,1};
        water.indices = {0,1,2}; water.iceIndices = {0,2,3};
        auto w = Renderer::prepareWater(water);
        check(w.water && w.vertices.size() == 4 && w.indices.size() == 6, "water vertex reuse");
        check(w.vertices[1].u == .5f && w.vertices[2].walk == 0 && w.vertices[0].walk == 1, "shore fade/ice preservation");
        water.indices.push_back(99);
        w = Renderer::prepareWater(water);
        check(w.vertices.empty() && w.indices.empty(), "incomplete water triangle accepted");
        water.indices = {0,1,99};
        check(Renderer::prepareWater(water).vertices.empty(), "invalid water index accepted");
        std::cout << "PASS: indexed geometry, transforms, bounds, materials, water fade/ice, invalid indices\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
