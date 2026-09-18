#pragma once
// Minimal D3D11 terrain renderer: one mesh + one albedo texture drawn into an
// offscreen render target that ImGui shows as an image. Orbit camera, simple
// directional light, optional wireframe / walkability overlay. Feature level
// 10.0 is enough (runs on WARP too), so it survives on very old machines.

#include <cstdint>
#include <d3d11.h>
#include <string>
#include <map>
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
    bool upload(const terrainexport::Scene& scene, Camera& camera, bool frameCamera = true);
    void clear();
    bool hasMesh() const { return indexCount_ > 0; }
    // Instance layers (0 = foliage, 1 = placed things): every instance baked
    // into world-space triangle batches, one batch per texture.
    static constexpr int kLayers = 2;
    bool uploadLayer(int layer, const foliageexport::Scene& scene, terrainexport::UpAxis up);
    void clearLayer(int layer);
    bool hasLayer(int layer) const { return !layers_[layer].empty(); }
    bool showLayer[kLayers] = {true, true};
    bool& showFoliage = showLayer[0];
    bool& showThings = showLayer[1];
    bool showWater = true;
    bool showGrid = false;   // LEV cell grid over the ground (every mode; Walkable always has it)
    // Back-compat names used by the app.
    bool uploadFoliage(const foliageexport::Scene& scene, terrainexport::UpAxis up) { return uploadLayer(0, scene, up); }
    void clearFoliage() { clearLayer(0); }
    bool hasFoliage() const { return hasLayer(0); }

    // Placed things are drawn per instance (one world matrix each) so the
    // editor can move them without re-baking: meshes live on the GPU once,
    // instances are a CPU list of (mesh, world) that setInstanceWorld updates.
    // World matrices are 4x4 row-vector Fable-space (rows = local axes incl.
    // the cm scale, row 3 = position); the up-axis swap is applied here.
    struct InstanceDraw {
        int mesh = -1;
        int thing = -1;          // .tng thing index (selection / highlight)
        float world[16] = {};    // render space
        bool visible = true;
    };
    bool uploadThings(const foliageexport::Scene& scene, terrainexport::UpAxis up);
    void clearThings();
    bool hasThings() const { return !instances_.empty(); }
    size_t instanceCount() const { return instances_.size(); }
    const InstanceDraw& instance(size_t i) const { return instances_[i]; }
    void setInstanceWorld(size_t i, const float fableWorld[16]);
    void setInstanceVisible(size_t i, bool on) { if (i < instances_.size()) instances_[i].visible = on; }
    int selectedThing = -1;  // instances of this thing are outlined
    std::vector<int> alsoSelected;   // the rest of a multi-selection (outlined a shade dimmer)
    // Ray (render space) against every visible instance's mesh; returns the
    // instance index or -1, with `t` the hit distance.
    int pick(const float origin[3], const float dir[3], float& t) const;
    // Ray through viewport-relative (u, v) in [0,1] for the last rendered frame.
    void screenRay(float u, float v, float origin[3], float dir[3]) const;
    // Matrices of the last rendered frame (row-vector layout, translation at 12..14).
    const float* viewMatrix() const { return lastView_; }
    const float* projMatrix() const { return lastProj_; }
    // Bounding sphere of one instance in render space (for framing the camera).
    bool instanceBounds(size_t i, float center[3], float& radius) const;

    // Terrain editing: replace the heightfield (cellsX*cellsY vertex heights,
    // Fable z) and walkability of the uploaded terrain in place; normals are
    // recomputed. The grid must match the uploaded scene.
    bool updateTerrain(const float* heights, const uint8_t* walkable, int cellsX, int cellsY);
    // Ray (render space) against the uploaded heightfield; `hit` is the render-
    // space point. Marches the ray, so it is exact enough for a brush cursor.
    bool rayTerrain(const float origin[3], const float dir[3], float hit[3]) const;
    // Project a render-space point to viewport-relative (u, v) in [0,1]; false when behind the eye.
    bool project(const float p[3], float& u, float& v) const;
    int terrainCellsX() const { return cellsX_; }
    int terrainCellsY() const { return cellsY_; }

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
    ID3D11BlendState* alphaBlend_ = nullptr;
    ID3D11DepthStencilState* depthNoWrite_ = nullptr;
    ID3D11Buffer* waterVb_ = nullptr;
    ID3D11Buffer* waterIb_ = nullptr;
    uint32_t waterIndexCount_ = 0;
    ID3D11ShaderResourceView* albedo_ = nullptr;
    struct FoliageBatch { ID3D11Buffer* vb = nullptr; uint32_t count = 0; ID3D11ShaderResourceView* srv = nullptr; bool alpha = false; };
    std::vector<FoliageBatch> layers_[kLayers];
    struct GpuMesh {
        std::vector<FoliageBatch> parts;   // vertex buffers in mesh-local Fable axes (cm)
        std::vector<float> tris;           // CPU copy for picking: 9 floats per triangle
        float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};
    };
    std::vector<GpuMesh> meshes_;
    std::vector<InstanceDraw> instances_;
    terrainexport::UpAxis thingsUp_ = terrainexport::UpAxis::Y;
    ID3D11Buffer* ocbuffer_ = nullptr;     // per-object constants (world, tint)
    ID3D11DepthStencilState* depthOverlay_ = nullptr;
    void setObject(const float world[16], const float tint[4]);
    float lastView_[16] = {}, lastProj_[16] = {};
    Camera lastCamera_;
    float lastAspect_ = 1.0f;
    ID3D11ShaderResourceView* makeTexture(const terrainexport::Image& img);
public:
    // Small UI swatch of a decoded texture, cached by textures.big id (the theme picker);
    // owned by the renderer, freed with it. Downsampled to 64x64 so 200 themes cost ~3 MB.
    ID3D11ShaderResourceView* swatch(uint32_t id, const terrainexport::Image& img);
    // The Textures tab's preview: one full-size texture at a time (the previous is freed).
    ID3D11ShaderResourceView* previewTexture(const terrainexport::Image& img);
private:
    std::map<uint32_t, ID3D11ShaderResourceView*> swatches_;
    ID3D11ShaderResourceView* preview_ = nullptr;
    ID3D11ShaderResourceView* white_ = nullptr;
    ID3D11Texture2D* target_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;
    ID3D11ShaderResourceView* srv_ = nullptr;
    ID3D11Texture2D* depthTex_ = nullptr;
    ID3D11DepthStencilView* dsv_ = nullptr;
    uint32_t width_ = 0, height_ = 0;
    uint32_t indexCount_ = 0;
    float minH_ = 0, maxH_ = 1;
    std::vector<float> heights_;        // CPU copy of the terrain grid (Fable z), row-major y*cellsX+x
    std::vector<uint8_t> walk_;
    std::vector<float> terrainUv_;      // u,v per vertex (kept across height updates)
    int cellsX_ = 0, cellsY_ = 0;
    float originX_ = 0, originY_ = 0;   // world offset of vertex (0,0) in Fable x/y
    bool terrainYUp_ = true;
    bool rebuildTerrainBuffer();
    const char* error_ = "";
    std::string errorText_;
};

} // namespace albion::gui
