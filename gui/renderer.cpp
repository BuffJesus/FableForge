#include "renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <d3dcompiler.h>
#include <map>
#include <string>

namespace albion::gui {

namespace {

template <typename T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

const char* kShader = R"HLSL(
cbuffer Frame : register(b0) {
    row_major float4x4 viewProj;
    float4 lightDir;      // xyz normalized, towards the light
    float4 params;        // x = mode, y = minH, z = maxH, w = time
    float4 eye;
    float4 flags;         // x = instance pass, y = alpha test (cutout materials)
};
Texture2D albedo : register(t0);
SamplerState samp : register(s0);

struct VSIn  { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; float walk : TEXCOORD1; };
struct VSOut { float4 pos : SV_Position; float3 wpos : TEXCOORD2; float3 nrm : NORMAL; float2 uv : TEXCOORD0; float walk : TEXCOORD1; };

VSOut VS(VSIn i) {
    VSOut o;
    o.pos = mul(float4(i.pos, 1.0), viewProj);
    o.wpos = i.pos; o.nrm = i.nrm; o.uv = i.uv; o.walk = i.walk;
    return o;
}

float3 heightRamp(float t) {
    // deep indigo -> violet -> lavender -> warm white
    float3 a = float3(0.10, 0.06, 0.22);
    float3 b = float3(0.42, 0.22, 0.78);
    float3 c = float3(0.75, 0.62, 0.98);
    float3 d = float3(0.98, 0.95, 0.90);
    if (t < 0.33) return lerp(a, b, t / 0.33);
    if (t < 0.66) return lerp(b, c, (t - 0.33) / 0.33);
    return lerp(c, d, (t - 0.66) / 0.34);
}

float4 PS(VSOut i) : SV_Target {
    float3 n = normalize(i.nrm);
    float ndl = saturate(dot(n, lightDir.xyz));
    float hemi = 0.55 + 0.45 * saturate(n.y);           // sky ambient
    float light = 0.30 * hemi + 0.72 * ndl;
    int mode = (int)params.x;
    float t = saturate((i.wpos.y - params.y) / max(params.z - params.y, 0.001));
    float3 base;
    if (flags.x > 0.5) {
        float4 tex = albedo.Sample(samp, i.uv);
        if (flags.y > 0.5 && tex.a < 0.5) discard;
        // foliage: soften lighting so blades read as translucent-ish
        float3 col = tex.rgb * (0.45 + 0.65 * ndl + 0.2 * hemi);
        float dist = distance(eye.xyz, i.wpos);
        float haze = saturate((dist - eye.w * 1.5) / (eye.w * 4.0));
        return float4(lerp(col, float3(0.075, 0.07, 0.10), haze * 0.7), 1.0);
    }
    if (mode == 0) {
        base = albedo.Sample(samp, i.uv).rgb;
    } else if (mode == 1) {
        float d = distance(eye.xyz, i.wpos);
        float fade = saturate(1.0 - d / (eye.w * 3.0));
        return float4(lerp(float3(0.30, 0.20, 0.55), float3(0.72, 0.55, 1.0), fade), 1.0);
    } else if (mode == 2) {
        float3 ok = float3(0.30, 0.80, 0.55), no = float3(0.85, 0.28, 0.35);
        base = lerp(no, ok, saturate(i.walk)) * 0.9;
        // subtle 1-unit grid so cells read
        float2 g = abs(frac(float2(i.wpos.x, i.wpos.z)) - 0.5);
        float gridLine = smoothstep(0.47, 0.5, max(g.x, g.y));
        base = lerp(base, base * 0.6, gridLine * 0.5);
    } else {
        base = heightRamp(t);
    }
    // gentle distance haze towards the viewport background
    float dist = distance(eye.xyz, i.wpos);
    float haze = saturate((dist - eye.w * 1.5) / (eye.w * 4.0));
    float3 col = base * light;
    col = lerp(col, float3(0.075, 0.07, 0.10), haze * 0.7);
    return float4(col, 1.0);
}
)HLSL";

struct FrameCB {
    float viewProj[16];
    float lightDir[4];
    float params[4];
    float eye[4];
    float flags[4];
};

void mul4(const float a[16], const float b[16], float out[16]) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[r * 4 + k] * b[k * 4 + c];
            out[r * 4 + c] = s;
        }
}

void lookAtRH(const float eye[3], const float at[3], float out[16]) {
    float z[3] = {eye[0] - at[0], eye[1] - at[1], eye[2] - at[2]};
    float zl = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
    for (float& v : z) v /= zl;
    const float up[3] = {0, 1, 0};
    float x[3] = {up[1] * z[2] - up[2] * z[1], up[2] * z[0] - up[0] * z[2], up[0] * z[1] - up[1] * z[0]};
    float xl = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    if (xl < 1e-6f) { x[0] = 1; x[1] = 0; x[2] = 0; xl = 1; }
    for (float& v : x) v /= xl;
    const float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
    const float m[16] = {x[0], y[0], z[0], 0,
                         x[1], y[1], z[1], 0,
                         x[2], y[2], z[2], 0,
                         -(x[0] * eye[0] + x[1] * eye[1] + x[2] * eye[2]),
                         -(y[0] * eye[0] + y[1] * eye[1] + y[2] * eye[2]),
                         -(z[0] * eye[0] + z[1] * eye[1] + z[2] * eye[2]), 1};
    std::memcpy(out, m, sizeof m);
}

void perspectiveRH(float fovY, float aspect, float zn, float zf, float out[16]) {
    const float h = 1.0f / std::tan(fovY * 0.5f), w = h / aspect;
    const float m[16] = {w, 0, 0, 0,
                         0, h, 0, 0,
                         0, 0, zf / (zn - zf), -1,
                         0, 0, zn * zf / (zn - zf), 0};
    std::memcpy(out, m, sizeof m);
}

struct GpuVertex { float px, py, pz, nx, ny, nz, u, v, walk; };

} // namespace

void Camera::dir(float out[3]) const {
    // The eye sits at focus + distance * (cos p sin y, sin p, cos p cos y), so the
    // view direction is the negative of that vector.
    out[0] = -std::cos(pitch) * std::sin(yaw);
    out[1] = -std::sin(pitch);
    out[2] = -std::cos(pitch) * std::cos(yaw);
}

void Camera::right(float out[3]) const {
    out[0] = std::cos(yaw); out[1] = 0; out[2] = -std::sin(yaw);
}

void Camera::up(float out[3]) const {
    float d[3], r[3]; dir(d); right(r);
    out[0] = r[1] * d[2] - r[2] * d[1];
    out[1] = r[2] * d[0] - r[0] * d[2];
    out[2] = r[0] * d[1] - r[1] * d[0];
    if (out[1] < 0) { out[0] = -out[0]; out[1] = -out[1]; out[2] = -out[2]; }
}

void Camera::focus(float out[3]) const {
    float d[3]; dir(d);
    out[0] = posX + d[0] * distance; out[1] = posY + d[1] * distance; out[2] = posZ + d[2] * distance;
}

void Camera::lookAt(float tx, float ty, float tz, float y, float p, float dist) {
    yaw = y; pitch = std::clamp(p, -1.55f, 1.55f); distance = std::max(dist, 0.5f);
    float d[3]; dir(d);
    posX = tx - d[0] * distance; posY = ty - d[1] * distance; posZ = tz - d[2] * distance;
}

void Camera::look(float dYaw, float dPitch) {
    yaw += dYaw;
    pitch = std::clamp(pitch + dPitch, -1.55f, 1.55f);
}

void Camera::orbit(float dYaw, float dPitch) {
    float f[3]; focus(f);
    lookAt(f[0], f[1], f[2], yaw + dYaw, pitch + dPitch, distance);
}

void Camera::pan(float dx, float dy) {
    float r[3], u[3]; right(r); up(u);
    posX += r[0] * dx + u[0] * dy;
    posY += r[1] * dx + u[1] * dy;
    posZ += r[2] * dx + u[2] * dy;
}

void Camera::dolly(float steps) {
    const float newDist = std::clamp(distance * std::pow(0.85f, steps), 0.5f, 20000.0f);
    float d[3]; dir(d);
    const float move = distance - newDist;
    posX += d[0] * move; posY += d[1] * move; posZ += d[2] * move;
    distance = newDist;
}

void Camera::fly(float forward, float strafe, float rise, float dt) {
    float d[3], r[3]; dir(d); right(r);
    const float k = flySpeed * dt;
    posX += (d[0] * forward + r[0] * strafe) * k;
    posY += (d[1] * forward + r[1] * strafe + rise) * k;
    posZ += (d[2] * forward + r[2] * strafe) * k;
}

Renderer::~Renderer() {
    releaseTarget();
    releaseMesh();
    for (int i = 0; i < kLayers; ++i) clearLayer(i);
    release(white_);
    release(blend_); release(depth_); release(wire_); release(solid_); release(sampler_); release(wrapSampler_);
    release(cbuffer_); release(layout_); release(ps_); release(vs_);
}

bool Renderer::init(ID3D11Device* device, ID3D11DeviceContext* context) {
    device_ = device; ctx_ = context;
    ID3DBlob *v = nullptr, *p = nullptr, *err = nullptr;
    if (FAILED(D3DCompile(kShader, std::strlen(kShader), nullptr, nullptr, nullptr, "VS", "vs_4_0", 0, 0, &v, &err))) {
        errorText_ = err ? std::string(static_cast<const char*>(err->GetBufferPointer())) : "VS compile failed";
        error_ = errorText_.c_str();
        release(err);
        return false;
    }
    release(err);
    if (FAILED(D3DCompile(kShader, std::strlen(kShader), nullptr, nullptr, nullptr, "PS", "ps_4_0", 0, 0, &p, &err))) {
        errorText_ = err ? std::string(static_cast<const char*>(err->GetBufferPointer())) : "PS compile failed";
        error_ = errorText_.c_str();
        release(err); release(v);
        return false;
    }
    release(err);
    device_->CreateVertexShader(v->GetBufferPointer(), v->GetBufferSize(), nullptr, &vs_);
    device_->CreatePixelShader(p->GetBufferPointer(), p->GetBufferSize(), nullptr, &ps_);
    const D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    device_->CreateInputLayout(il, 4, v->GetBufferPointer(), v->GetBufferSize(), &layout_);
    release(v); release(p);

    D3D11_BUFFER_DESC cb = {};
    cb.ByteWidth = sizeof(FrameCB); cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device_->CreateBuffer(&cb, nullptr, &cbuffer_);

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    device_->CreateSamplerState(&sd, &sampler_);
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    device_->CreateSamplerState(&sd, &wrapSampler_);

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
    device_->CreateRasterizerState(&rd, &solid_);
    rd.FillMode = D3D11_FILL_WIREFRAME; rd.AntialiasedLineEnable = TRUE;
    device_->CreateRasterizerState(&rd, &wire_);

    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc = D3D11_COMPARISON_LESS;
    device_->CreateDepthStencilState(&dd, &depth_);

    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = FALSE;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device_->CreateBlendState(&bd, &blend_);

    // 1x1 white fallback texture.
    const uint32_t white = 0xFF9A93A6u;  // neutral grey-violet for untextured maps (ABGR)
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = td.Height = 1; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init = {&white, 4, 0};
    ID3D11Texture2D* tex = nullptr;
    if (SUCCEEDED(device_->CreateTexture2D(&td, &init, &tex))) {
        device_->CreateShaderResourceView(tex, nullptr, &white_);
        tex->Release();
    }
    return vs_ && ps_ && layout_ && cbuffer_;
}

void Renderer::releaseMesh() {
    release(vb_); release(ib_); release(albedo_);
    indexCount_ = 0;
}

void Renderer::clear() { releaseMesh(); for (int i = 0; i < kLayers; ++i) clearLayer(i); }

void Renderer::clearLayer(int layer) {
    for (auto& b : layers_[layer]) { release(b.vb); release(b.srv); }
    layers_[layer].clear();
}

ID3D11ShaderResourceView* Renderer::makeTexture(const terrainexport::Image& img) {
    if (!img.width || !img.height) return nullptr;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = img.width; td.Height = img.height; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init = {img.rgba.data(), img.width * 4, 0};
    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    if (SUCCEEDED(device_->CreateTexture2D(&td, &init, &tex))) {
        device_->CreateShaderResourceView(tex, nullptr, &srv);
        tex->Release();
    }
    return srv;
}

bool Renderer::uploadLayer(int layer, const foliageexport::Scene& scene, terrainexport::UpAxis up) {
    clearLayer(layer);
    if (scene.instances.empty()) return false;
    auto& foliage_ = layers_[layer];
    auto toUp = [up](float x, float y, float z, float& ox, float& oy, float& oz) {
        if (up == terrainexport::UpAxis::Y) { ox = x; oy = z; oz = -y; } else { ox = x; oy = y; oz = z; }
    };
    std::map<std::pair<int, bool>, std::vector<GpuVertex>> byImage;   // (image index, cutout) ; -1 = untextured
    for (const auto& inst : scene.instances) {
        if (inst.mesh < 0) continue;
        const auto& m = scene.meshes[size_t(inst.mesh)];
        float col[3][3]; foliageexport::instanceBasis(inst, col);
        for (const auto& part : m.parts) {
          auto& out = byImage[{part.image, part.hasAlpha}];
          for (size_t k = 0; k + 2 < part.indices.size(); k += 3) {
            const uint32_t ids[3] = {part.indices[k], part.indices[k + 1], part.indices[k + 2]};
            for (uint32_t id : ids) {
                if (id >= m.geometry.vertices.size()) continue;
                const auto& v = m.geometry.vertices[id];
                GpuVertex g{};
                toUp(inst.x + v.x * col[0][0] + v.y * col[1][0] + v.z * col[2][0],
                     inst.y + v.x * col[0][1] + v.y * col[1][1] + v.z * col[2][1],
                     inst.z + v.x * col[0][2] + v.y * col[1][2] + v.z * col[2][2], g.px, g.py, g.pz);
                toUp(v.nx * col[0][0] + v.ny * col[1][0] + v.nz * col[2][0],
                     v.nx * col[0][1] + v.ny * col[1][1] + v.nz * col[2][1],
                     v.nx * col[0][2] + v.ny * col[1][2] + v.nz * col[2][2], g.nx, g.ny, g.nz);
                g.u = v.u; g.v = v.v; g.walk = 1.0f;
                out.push_back(g);
            }
          }
        }
    }
    for (auto& [key, verts] : byImage) {
        const int image = key.first;
        if (verts.empty()) continue;
        FoliageBatch b;
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = UINT(verts.size() * sizeof(GpuVertex)); bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA sd = {verts.data(), 0, 0};
        if (FAILED(device_->CreateBuffer(&bd, &sd, &b.vb))) continue;
        b.count = uint32_t(verts.size());
        if (image >= 0 && size_t(image) < scene.images.size()) b.srv = makeTexture(scene.images[size_t(image)]);
        b.alpha = key.second;
        foliage_.push_back(b);
    }
    return !foliage_.empty();
}

bool Renderer::upload(const terrainexport::Scene& scene, Camera& camera, bool frameCamera) {
    releaseMesh();
    if (scene.vertices.empty() || scene.indices.empty()) return false;
    std::vector<GpuVertex> verts(scene.vertices.size());
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    for (size_t i = 0; i < verts.size(); ++i) {
        const auto& s = scene.vertices[i];
        verts[i] = {s.px, s.py, s.pz, s.nx, s.ny, s.nz, s.u, s.v, s.walkable ? 1.0f : 0.0f};
        mn[0] = std::min(mn[0], s.px); mn[1] = std::min(mn[1], s.py); mn[2] = std::min(mn[2], s.pz);
        mx[0] = std::max(mx[0], s.px); mx[1] = std::max(mx[1], s.py); mx[2] = std::max(mx[2], s.pz);
    }
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = UINT(verts.size() * sizeof(GpuVertex)); bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd = {verts.data(), 0, 0};
    if (FAILED(device_->CreateBuffer(&bd, &sd, &vb_))) { error_ = "vertex buffer"; return false; }
    bd.ByteWidth = UINT(scene.indices.size() * 4); bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    sd.pSysMem = scene.indices.data();
    if (FAILED(device_->CreateBuffer(&bd, &sd, &ib_))) { error_ = "index buffer"; releaseMesh(); return false; }
    indexCount_ = uint32_t(scene.indices.size());

    if (scene.hasAlbedo && scene.albedo.width && scene.albedo.height) {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = scene.albedo.width; td.Height = scene.albedo.height;
        td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init = {scene.albedo.rgba.data(), scene.albedo.width * 4, 0};
        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(device_->CreateTexture2D(&td, &init, &tex))) {
            device_->CreateShaderResourceView(tex, nullptr, &albedo_);
            tex->Release();
        }
    }
    minH_ = mn[1]; maxH_ = mx[1];
    const float span = std::max({mx[0] - mn[0], mx[2] - mn[2], 8.0f});
    if (frameCamera) {
        camera.lookAt((mn[0] + mx[0]) * 0.5f, (mn[1] + mx[1]) * 0.5f, (mn[2] + mx[2]) * 0.5f, 0.8f, 0.62f, span * 0.95f);
        camera.flySpeed = std::max(span * 0.25f, 4.0f);
    }
    return true;
}

bool Renderer::ensureTarget(uint32_t w, uint32_t h) {
    if (w == width_ && h == height_ && rtv_) return true;
    releaseTarget();
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &target_))) return false;
    device_->CreateRenderTargetView(target_, nullptr, &rtv_);
    device_->CreateShaderResourceView(target_, nullptr, &srv_);
    td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &depthTex_))) return false;
    device_->CreateDepthStencilView(depthTex_, nullptr, &dsv_);
    width_ = w; height_ = h;
    return rtv_ && srv_ && dsv_;
}

void Renderer::releaseTarget() {
    release(dsv_); release(depthTex_); release(srv_); release(rtv_); release(target_);
    width_ = height_ = 0;
}

ID3D11ShaderResourceView* Renderer::render(uint32_t width, uint32_t height, const Camera& camera,
                                           ViewMode mode, float time) {
    width = std::max(width, 8u); height = std::max(height, 8u);
    if (!ensureTarget(width, height)) return nullptr;

    const float clearCol[4] = {0.075f, 0.07f, 0.10f, 1.0f};
    ctx_->OMSetRenderTargets(1, &rtv_, dsv_);
    ctx_->ClearRenderTargetView(rtv_, clearCol);
    ctx_->ClearDepthStencilView(dsv_, D3D11_CLEAR_DEPTH, 1.0f, 0);
    if (!indexCount_) return srv_;

    D3D11_VIEWPORT vp = {0, 0, float(width), float(height), 0, 1};
    ctx_->RSSetViewports(1, &vp);

    float eye[3]; camera.eye(eye);
    float at[3]; camera.focus(at);
    float view[16], proj[16];
    lookAtRH(eye, at, view);
    const float zn = std::max(camera.distance * 0.01f, 0.05f), zf = camera.distance * 30.0f + 1000.0f;
    perspectiveRH(camera.fovY, float(width) / float(height), zn, zf, proj);
    FrameCB cb = {};
    mul4(view, proj, cb.viewProj);
    // Sun: from the upper-left-front, slowly not moving (stable screenshots).
    float l[3] = {-0.45f, 0.8f, 0.35f};
    const float ll = std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
    cb.lightDir[0] = l[0] / ll; cb.lightDir[1] = l[1] / ll; cb.lightDir[2] = l[2] / ll; cb.lightDir[3] = 0;
    cb.params[0] = float(int(mode)); cb.params[1] = minH_; cb.params[2] = maxH_; cb.params[3] = time;
    cb.eye[0] = eye[0]; cb.eye[1] = eye[1]; cb.eye[2] = eye[2]; cb.eye[3] = camera.distance;
    D3D11_MAPPED_SUBRESOURCE map;
    if (SUCCEEDED(ctx_->Map(cbuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
        std::memcpy(map.pData, &cb, sizeof cb);
        ctx_->Unmap(cbuffer_, 0);
    }

    const UINT stride = sizeof(GpuVertex), offset = 0;
    ctx_->IASetInputLayout(layout_);
    ctx_->IASetVertexBuffers(0, 1, &vb_, &stride, &offset);
    ctx_->IASetIndexBuffer(ib_, DXGI_FORMAT_R32_UINT, 0);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(vs_, nullptr, 0);
    ctx_->VSSetConstantBuffers(0, 1, &cbuffer_);
    ctx_->PSSetShader(ps_, nullptr, 0);
    ctx_->PSSetConstantBuffers(0, 1, &cbuffer_);
    ID3D11ShaderResourceView* tex = albedo_ ? albedo_ : white_;
    ctx_->PSSetShaderResources(0, 1, &tex);
    ctx_->PSSetSamplers(0, 1, &sampler_);
    ctx_->RSSetState(mode == ViewMode::Wireframe ? wire_ : solid_);
    ctx_->OMSetDepthStencilState(depth_, 0);
    const float bf[4] = {0, 0, 0, 0};
    ctx_->OMSetBlendState(blend_, bf, 0xFFFFFFFF);
    ctx_->DrawIndexed(indexCount_, 0, 0);

    if (mode != ViewMode::Wireframe) {
        bool any = false;
        for (int i = 0; i < kLayers; ++i) any = any || (showLayer[i] && !layers_[i].empty());
        if (any) {
            cb.flags[0] = 1.0f; cb.flags[1] = 1.0f;   // instance pass; y = alpha test on
            if (SUCCEEDED(ctx_->Map(cbuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
                std::memcpy(map.pData, &cb, sizeof cb);
                ctx_->Unmap(cbuffer_, 0);
            }
            ctx_->RSSetState(solid_);
            ctx_->PSSetSamplers(0, 1, &wrapSampler_);
            bool currentAlpha = true;
            for (int i = 0; i < kLayers; ++i) {
                if (!showLayer[i]) continue;
                for (const auto& b : layers_[i]) {
                    if (b.alpha != currentAlpha) {
                        currentAlpha = b.alpha;
                        cb.flags[0] = 1.0f; cb.flags[1] = currentAlpha ? 1.0f : 0.0f;
                        if (SUCCEEDED(ctx_->Map(cbuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) { std::memcpy(map.pData, &cb, sizeof cb); ctx_->Unmap(cbuffer_, 0); }
                    }
                    ID3D11ShaderResourceView* t = b.srv ? b.srv : white_;
                    ctx_->PSSetShaderResources(0, 1, &t);
                    ctx_->IASetVertexBuffers(0, 1, &b.vb, &stride, &offset);
                    ctx_->Draw(b.count, 0);
                }
            }
        }
    }

    ID3D11ShaderResourceView* nullSrv = nullptr;
    ctx_->PSSetShaderResources(0, 1, &nullSrv);
    ID3D11RenderTargetView* nullRtv = nullptr;
    ctx_->OMSetRenderTargets(1, &nullRtv, nullptr);
    return srv_;
}

} // namespace albion::gui
