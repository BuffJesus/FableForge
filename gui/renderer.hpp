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

// Free camera with Unreal-editor semantics. `distance` is the focus distance
// used by orbit / dolly / pan speed; the eye is `pos`, looking along dir().
struct Camera {
    float posX = 0, posY = 0, posZ = 0;   // eye (render space, Y-up)
    float yaw = 0.8f;      // radians around Y
    float pitch = 0.6f;    // radians; positive looks DOWN (eye above the focus point)
    float distance = 100;  // focus distance
    float fovY = 0.9f;
    float flySpeed = 20;   // world units per second (WASD)

    void dir(float out[3]) const;          // unit view direction
    void right(float out[3]) const;
    void up(float out[3]) const;
    void eye(float out[3]) const { out[0] = posX; out[1] = posY; out[2] = posZ; }
    void focus(float out[3]) const;        // pos + dir * distance
    void lookAt(float tx, float ty, float tz, float yaw, float pitch, float dist);

    void look(float dYaw, float dPitch);   // RMB: rotate in place
    void orbit(float dYaw, float dPitch);  // Alt+LMB: rotate around the focus point
    void pan(float dx, float dy);          // MMB: track in the view plane
    void dolly(float steps);               // wheel: move along the view direction
    void fly(float forward, float strafe, float rise, float dt); // WASD/QE while RMB
    void turn(float dYaw) { yaw += dYaw; }
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
    ID3D11SamplerState* wrapSampler_ = nullptr;
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
