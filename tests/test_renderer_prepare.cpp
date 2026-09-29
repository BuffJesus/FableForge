// Pure CPU checks: this executable never creates a window or D3D device.
#include "renderer.hpp"
#include <iostream>
#include <stdexcept>

using albion::gui::Renderer;
namespace fe = albion::foliageexport;
namespace te = albion::terrainexport;
static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main() {
    try {
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
