#pragma once
#include "terrainlod.hpp"
// Minimal D3D11 terrain renderer: one mesh + one albedo texture drawn into an
// offscreen render target that ImGui shows as an image. Orbit camera, simple
// directional light, optional wireframe / walkability overlay. Feature level
// 10.0 is enough (runs on WARP too), so it survives on very old machines.

#include <cstdint>
#include <algorithm>
#include <d3d11.h>
#include <string>
#include <deque>
#include <map>
#include <vector>

#include "foliageexport.hpp"
#include "terrainexport.hpp"
#include "texturepool.hpp"
#include "cutoutmips.hpp"
#include "worldaa.hpp"
#include "deferredrelease.hpp"

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
    void view(float out[16]) const;        // eye/direction, independent of orbit distance
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
    struct VideoMemoryInfo { bool valid = false; uint64_t budget = 0, usage = 0; };
    VideoMemoryInfo queryVideoMemory() const;
    int worldAaMode = 0; // 0 automatic, 1 off, 2/4 fixed upper bound
    unsigned worldAaTestLimit = 4; // automation: fail multisample allocations above this count
    unsigned aaSamples() const { return targetSamples_; }
    unsigned aaSupport() const { return aaSupport_; }
    size_t aaRebuilds() const { return aaRebuilds_; }
    size_t aaFallbacks() const { return aaFallbacks_; }
    uint64_t renderTargetBytes() const { return worldview::targetBytes(width_, height_, targetSamples_); }
    void observeWorldAa(float dt, bool eligible) { aaBudget_.observe(dt, eligible && worldAaMode == 0); }
    void sampleWorldAaMemory(VideoMemoryInfo info) { aaMemory_ = info; aaSampledTargetBytes_ = renderTargetBytes(); }
    bool worldCutoutMask = false; // automation: white cutout coverage, no opaque geometry
    bool worldCutoutMips = true; // prepared off-thread; coverage-preserving partial chains
    bool worldCutoutAa = true; // smooth alpha-test edges when multisampling is active
    bool worldTextureMips = true; // opaque layer textures; cutouts preserve authored coverage
    bool worldTextureSharing = true; // automation comparison; set before loading
    using LayerTexturePool = TexturePool<ID3D11ShaderResourceView>;
    LayerTexturePool::Stats texturePoolStats() const { return texturePool_.stats(); }
    void pollTextureCleanup(); // CPU-only retirement, including frames without a viewport
    size_t retiredTextureBytes() const { return retiredTextureBytes_ + (texturePixelsRelease_.idle() ? 0 : texturePixelsInFlight_); }
    bool worldNormalBlend = true;
    bool worldMaterialBlend = true; // automation A/B diagnostic
    // Upload a scene (positions/normals/uv already in the scene's up-axis
    // space; the renderer expects Y-up). Frames the camera on the map.
    bool upload(const terrainexport::Scene& scene, Camera& camera, bool frameCamera = true);
    void clear();
    bool hasMesh() const { return indexCount_ > 0; }
    // Instance layers (0 = foliage, 1 = placed things): every instance baked
    // into world-space triangle batches, one batch per texture.
    static constexpr int kLayers = 6;   // editor layers, world ground, near detail, distant scenery
    static constexpr int kWorldLayer = 3;         // World tab 3D: one low-res tile per map
    static constexpr int kWorldDetailLayer = 4;   // World tab 3D: full terrain + foliage + things of the maps near the camera
    static constexpr int kWorldSceneryLayer = 5;  // objects across visible maps, independent of near terrain slots
    bool uploadLayer(int layer, const foliageexport::Scene& scene, terrainexport::UpAxis up);
    // Adds the scene's batches to the layer without clearing it (the world view streams map tiles in).
    bool appendLayer(int layer, const foliageexport::Scene& scene, terrainexport::UpAxis up, int tag = -1);
    struct LayerVertex { float px, py, pz, nx, ny, nz, u, v, walk; uint32_t coarseNormal = 0; };
    // R10G10B10A2_UNORM input, signed normal mapped into [0,1]. Four extra bytes
    // per vertex; no duplicate geometry, textures or render pass for normal morphs.
    static uint32_t packNormal(float x, float y, float z);
    // Basis images of local axes. Inverse transpose up to a common positive
    // scale; avoids overflow for tiny transforms and preserves reflections.
    static void normalBasis(const float basis[3][3], float result[3][3]);
    struct ObjectRange {
        uint32_t first = 0, count = 0;
        float center[3] = {}, radius = 0;
        float nearPixels = 0, farPixels = 0; // zero means unbounded
        uint32_t lod = 0;
    };
    struct PreparedBatch {
        std::vector<terrainlod::Patch> terrainPatches;
        std::vector<ObjectRange> objects;
        std::vector<LayerVertex> vertices;
        std::vector<uint32_t> indices;
        int image = -1;
        bool alpha = false, water = false, terrainMorph = false;
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    };
    // CPU only: safe on a streaming worker. GPU creation stays on the render thread.
    static std::vector<PreparedBatch> prepareLayer(const foliageexport::Scene& scene, terrainexport::UpAxis up, bool objectLods = false, bool coarseOnly = false);
    static PreparedBatch prepareWater(const terrainexport::WaterMesh& water);
    bool appendPreparedBatch(int layer, const PreparedBatch& batch, const std::vector<terrainexport::Image>& images, int tag, bool visible = true, const cutoutmips::Chain* cutout = nullptr);
    bool appendWorldWater(const terrainexport::WaterMesh& water, int tag, float worldX, float worldY);
    size_t worldWaterBatches() const;
    size_t layerTagBytes(int layer, int tag) const;
    size_t layerSetBytes(int layer, const std::vector<int>& tags, bool exclusiveTextures) const;
    // Overview water stays visible during terrain detail transitions; dropping a tag removes both.
    void removeLayerTag(int layer, int tag);
    void setLayerTagVisible(int layer, int tag, bool visible);
    void setLayerTagFade(int layer, int tag, float coverage, bool inverse = false, float objectCoverage = -1.0f);
    void clearLayer(int layer);
    bool hasLayer(int layer) const { return !layers_[layer].empty(); }
    bool showLayer[kLayers] = {true, true, true, true, true, true};
    // The World tab's 3D view draws world tiles, persistent water and streamed detail.
    bool worldOnly = false;
    bool worldCulling = true;
    bool worldObjectLods = true;
    bool worldTerrainLods = true;
    size_t worldTerrainTriangles = 0, worldTerrainFullTriangles = 0, worldTerrainCoarsePatches = 0;
    float worldObjectDistance = 650;
    size_t worldDrawnObjects = 0, worldCulledObjects = 0, worldLodObjects = 0, worldObjectDrawCalls = 0;
    size_t worldSceneryDrawnParts = 0;
    size_t worldDrawnBatches = 0, worldCulledBatches = 0;
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
    // Support queries may exclude owning things (including their child meshes)
    // and ignore visual-only instances with no placed thing owner.
    int pick(const float origin[3], const float dir[3], float& t,
             const std::vector<int>& excludedThings, bool placedOnly) const;
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
    bool projectVisible(const float p[3], float& u, float& v) const;
    int terrainCellsX() const { return cellsX_; }
    int terrainCellsY() const { return cellsY_; }

    // Renders into the offscreen target at the given size and returns its SRV
    // (valid until the next render call).
    ID3D11ShaderResourceView* render(uint32_t width, uint32_t height, const Camera& camera,
                                    ViewMode mode, float time, float nearPlane = 0.0f);

    const char* error() const { return error_; }

private:
    bool ensureTarget(uint32_t w, uint32_t h, unsigned samples);
    void resolveTarget();
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
    ID3D11BlendState* cutoutBlend_ = nullptr;
    ID3D11BlendState* alphaBlend_ = nullptr;
    ID3D11DepthStencilState* depthNoWrite_ = nullptr;
    ID3D11Buffer* waterVb_ = nullptr;
    ID3D11Buffer* waterIb_ = nullptr;
    uint32_t waterIndexCount_ = 0;
    ID3D11ShaderResourceView* albedo_ = nullptr;
    struct FoliageBatch {
        std::vector<terrainlod::Patch> terrainPatches;
        std::vector<ObjectRange> objects;
        ID3D11ShaderResourceView* objectBounds = nullptr;
        ID3D11Buffer* vb = nullptr; uint32_t count = 0; ID3D11ShaderResourceView* srv = nullptr;
        std::vector<uint32_t> sourceIndices; // populated for the animated head preview
        ID3D11Buffer* ib = nullptr;
        ID3D11ShaderResourceView* coarseSrv = nullptr; // retained shared overview albedo; no duplicate allocation
        std::shared_ptr<LayerTexturePool::Entry> texture, coarseTexture;
        bool alpha = false; int tag = -1; bool visible = true; bool water = false;
        size_t resourceBytes = 0; // VB/IB payload; pooled textures counted separately
        bool terrainMorph = false;
        float coverage = 1.0f;
        bool inverseFade = false;
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        void include(float x, float y, float z) {
            const float p[3] = {x, y, z};
            for (int i = 0; i < 3; ++i) { lo[i] = std::min(lo[i], p[i]); hi[i] = std::max(hi[i], p[i]); }
        }
    };
    LayerTexturePool texturePool_;
    void releaseBatch(FoliageBatch& batch);
    using TexturePixelPayload = std::vector<std::vector<uint8_t>>;
    std::optional<TexturePixelPayload> retiredTexturePixels_;
    DeferredRelease<TexturePixelPayload> texturePixelsRelease_;
    size_t retiredTextureBytes_ = 0, texturePixelsInFlight_ = 0;
    void retireTexturePixels(std::vector<uint8_t>& pixels);
    std::vector<FoliageBatch> layers_[kLayers];
    void drawBatch(const FoliageBatch& batch);
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
    ID3D11ShaderResourceView* makeTexture(const terrainexport::Image& img, bool mipmaps = false);
    ID3D11ShaderResourceView* makeCutoutTexture(const terrainexport::Image& img, const cutoutmips::Chain& chain);
public:
    // Small UI swatch of a decoded texture, cached by textures.big id (the theme picker);
    // owned by the renderer, freed with it. Downsampled to 64x64 so 200 themes cost ~3 MB.
    ID3D11ShaderResourceView* swatch(uint32_t id, const terrainexport::Image& img);
    // The Textures list's row thumbnails, keyed by entry name: at most 32 px on the long side
    // (aspect kept), the oldest dropped past kListThumbCap. listThumb returns false when the
    // key has no thumbnail yet; makeListThumb stores one (null remembers a failure).
    static constexpr size_t kListThumbCap = 768;
    bool listThumb(const std::string& key, ID3D11ShaderResourceView*& srv) const;
    ID3D11ShaderResourceView* makeListThumb(const std::string& key, const terrainexport::Image& img);
    void clearListThumbs();
    // The Textures tab's preview: one full-size texture at a time (the previous is freed).
    ID3D11ShaderResourceView* previewTexture(const terrainexport::Image& img);
    // A UI image kept under `key` (the previous one of that key is freed): the fractal preview.
    ID3D11ShaderResourceView* uiTexture(const std::string& key, const terrainexport::Image& img);
    // A mesh thumbnail for the object palette: the mesh rendered once into its own small
    // target from a three-quarter view framing its bounds; cached by `key` and freed with
    // the renderer. Null when the mesh has no drawable part.
    ID3D11ShaderResourceView* thumbnail(const std::string& key, const foliageexport::Mesh& mesh,
                                        const std::vector<terrainexport::Image>& images, uint32_t size);
    void clearModelPreview();
    bool setModelPreview(const foliageexport::Mesh& mesh, const std::vector<terrainexport::Image>& images);
    ID3D11ShaderResourceView* modelPreview(uint32_t size, float yaw, float pitch, float zoom, bool wire);
    void clearHeadPreview();
    bool setHeadPreview(const foliageexport::Mesh& mesh, const std::vector<terrainexport::Image>& images);
    bool updateHeadPreview(const foliageexport::Mesh& posed);
    ID3D11ShaderResourceView* headPreview(uint32_t size, float yaw, float pitch, float zoom, bool wire);
private:
    GpuMesh modelMesh_;
    ID3D11ShaderResourceView* modelSrv_ = nullptr;
    uint32_t modelSize_ = 0;
    float modelYaw_ = 0, modelPitch_ = 0, modelZoom_ = 0;
    bool modelWire_ = false;
    GpuMesh headMesh_;
    ID3D11ShaderResourceView* headSrv_ = nullptr;
    uint32_t headSize_ = 0;
    float headYaw_ = 0, headPitch_ = 0, headZoom_ = 0;
    bool headWire_ = false;
    ID3D11ShaderResourceView* renderMeshPreview(const GpuMesh& g, uint32_t size,
                                               float yaw, float pitch, float zoom, bool wire);
    std::map<uint32_t, ID3D11ShaderResourceView*> swatches_;
    std::map<std::string, ID3D11ShaderResourceView*> listThumbs_;
    std::deque<std::string> listThumbOrder_;
    std::map<std::string, ID3D11ShaderResourceView*> uiTextures_;
    ID3D11ShaderResourceView* preview_ = nullptr;
    std::map<std::string, ID3D11ShaderResourceView*> thumbs_;
    void uploadMesh(const foliageexport::Mesh& m, const std::vector<terrainexport::Image>& images,
                    std::map<int, ID3D11ShaderResourceView*>& imageSrv, GpuMesh& g,
                    bool dynamic = false);
    void releaseMesh(GpuMesh& g);
    ID3D11ShaderResourceView* white_ = nullptr;
    ID3D11Texture2D* target_ = nullptr;
    ID3D11Texture2D* multisampleTarget_ = nullptr;
    unsigned targetSamples_ = 1, targetRequestedSamples_ = 1, targetTestLimit_ = 4, aaSupport_ = 1;
    size_t aaRebuilds_ = 0, aaFallbacks_ = 0;
    VideoMemoryInfo aaMemory_;
    uint64_t aaSampledTargetBytes_ = 0;
    worldview::AaBudget aaBudget_;
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
