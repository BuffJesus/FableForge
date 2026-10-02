#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <d3d11.h>

namespace forge::meshpreview { struct Geometry; }
namespace albion::terrainexport { struct Image; }
namespace albion::particlepreview { struct DrawSprite; struct DrawMesh; }
namespace albion::gui {
struct Camera;

// Independent offscreen effect viewport. Call on the owning immediate-context
// thread; the returned SRV remains owned here until resize/clear/destruction.
class ParticlePreviewRenderer {
public:
    static constexpr size_t maxSprites = 16384;
    ParticlePreviewRenderer();
    ~ParticlePreviewRenderer();
    ParticlePreviewRenderer(const ParticlePreviewRenderer&) = delete;
    ParticlePreviewRenderer& operator=(const ParticlePreviewRenderer&) = delete;
    bool init(ID3D11Device*, ID3D11DeviceContext*);
    void clear(); // releases effect textures and the offscreen target; keeps shaders
    void clearTextures();
    void clearMeshes();
    bool setMesh(int32_t id, const forge::meshpreview::Geometry&);
    size_t drawnMeshes() const;
    size_t missingMeshes() const;
    size_t droppedMeshes() const;
    size_t meshTriangles() const;
    size_t meshBytes() const;
    float meshBoundsFactor(int32_t id, bool centred) const;
    bool meshUsesAuthoredBounds(int32_t id) const;
    // Bounds of the rendered geometry in Fable coordinates, including scale and rotation.
    bool meshFrameBounds(const particlepreview::DrawMesh&, float lo[3], float hi[3]) const;
    // frameCount is the number of equal-height frames stacked vertically.
    // Nominal sprite size is its width; frame aspect scales its height once.
    bool setTexture(int32_t id, const terrainexport::Image&, uint32_t frameCount = 1);
    ID3D11ShaderResourceView* render(int width, int height, const Camera&,
                                   const std::vector<particlepreview::DrawSprite>&,
                                   const float background[3],bool grid=false);
    ID3D11ShaderResourceView* render(int width, int height, const Camera&,
                                   const std::vector<particlepreview::DrawSprite>&,
                                   const std::vector<particlepreview::DrawMesh>&,
                                   const float background[3],bool grid=false);
    const std::string& error() const;
    size_t drawnSprites() const;
    size_t missingTextures() const;
    size_t droppedSprites() const;
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}
