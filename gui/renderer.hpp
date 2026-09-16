#pragma once
// Minimal D3D11 terrain renderer: one mesh + one albedo texture drawn into an
// offscreen render target that ImGui shows as an image. Orbit camera, simple
// directional light, optional wireframe / walkability overlay. Feature level
// 10.0 is enough (runs on WARP too), so it survives on very old machines.

#include <cstdint>
#include <d3d11.h>
#include <string>
#include <vector>

#include "foliageexport.hpp"
#include "terrainexport.hpp"

namespace albion::gui {

struct Camera {
    float targetX = 0, targetY = 0, targetZ = 0;  // orbit centre (render space, Y-up)
    float yaw = 0.8f;      // radians around Y
    float pitch = 0.6f;    // radians above the horizon (0 = level)
    float distance = 100;  // eye distance from target
    float fovY = 0.9f;
    void orbit(float dYaw, float dPitch);
    void pan(float dx, float dy);        // screen-relative pan in world units
    void zoom(float steps);
    void eye(float out[3]) const;
};

enum class ViewMode { Textured, Wireframe, Walkable, Height };

class Renderer {
public:
    Renderer() = default;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init(ID3D11Device* device, ID3D11DeviceContext* context);
    // Upload a scene (positions/normals/uv already in the scene's up-axis
    // space; the renderer expects Y-up). Frames the camera on the map.
    bool upload(const terrainexport::Scene& scene, Camera& camera);
    void clear();
    bool hasMesh() const { return indexCount_ > 0; }
    // Bakes every instance into world-space triangle batches (one per texture).
    bool uploadFoliage(const foliageexport::Scene& scene, terrainexport::UpAxis up);
    void clearFoliage();
    bool hasFoliage() const { return !foliage_.empty(); }
    size_t foliageTriangles() const { return foliageTriangles_; }
    bool showFoliage = true;

    // Renders into the offscreen target at the given size and returns its SRV
    // (valid until the next render call).
    ID3D11ShaderResourceView* render(uint32_t width, uint32_t height, const Camera& camera,
                                     ViewMode mode, float time);

    const char* error() const { return error_; }

private:
    bool ensureTarget(uint32_t w, uint32_t h);
    void releaseTarget();
    void releaseMesh();

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* ctx_ = nullptr;
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11InputLayout* layout_ = nullptr;
    ID3D11Buffer* cbuffer_ = nullptr;
    ID3D11Buffer* vb_ = nullptr;
    ID3D11Buffer* ib_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
    ID3D11RasterizerState* solid_ = nullptr;
    ID3D11RasterizerState* wire_ = nullptr;
    ID3D11DepthStencilState* depth_ = nullptr;
    ID3D11BlendState* blend_ = nullptr;
    ID3D11ShaderResourceView* albedo_ = nullptr;
    struct FoliageBatch { ID3D11Buffer* vb = nullptr; uint32_t count = 0; ID3D11ShaderResourceView* srv = nullptr; bool alpha = false; };
    std::vector<FoliageBatch> foliage_;
    size_t foliageTriangles_ = 0;
    ID3D11ShaderResourceView* makeTexture(const terrainexport::Image& img);
    ID3D11ShaderResourceView* white_ = nullptr;
    ID3D11Texture2D* target_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;
    ID3D11ShaderResourceView* srv_ = nullptr;
    ID3D11Texture2D* depthTex_ = nullptr;
    ID3D11DepthStencilView* dsv_ = nullptr;
    uint32_t width_ = 0, height_ = 0;
    uint32_t indexCount_ = 0;
    float minH_ = 0, maxH_ = 1;
    const char* error_ = "";
    std::string errorText_;
};

} // namespace albion::gui
