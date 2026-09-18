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
    float4 lightDir;      // xyz normalized, towards the light; w = cell grid on
    float4 params;        // x = mode, y = minH, z = maxH, w = time
    float4 eye;
    float4 flags;         // x = pass (0 terrain, 1 instances, 2 water), y = alpha test (cutout materials)
};
cbuffer Object : register(b1) {
    row_major float4x4 world;   // identity for baked geometry; per-thing for instances
    float4 tint;                // a > 0: flat colour (selection outline)
};
Texture2D albedo : register(t0);
SamplerState samp : register(s0);

struct VSIn  { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; float walk : TEXCOORD1; };
struct VSOut { float4 pos : SV_Position; float3 wpos : TEXCOORD2; float3 nrm : NORMAL; float2 uv : TEXCOORD0; float walk : TEXCOORD1; };

VSOut VS(VSIn i) {
    VSOut o;
    float4 wp = mul(float4(i.pos, 1.0), world);
    o.pos = mul(wp, viewProj);
    o.wpos = wp.xyz; o.nrm = mul(i.nrm, (float3x3)world); o.uv = i.uv; o.walk = i.walk;
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
    if (tint.a > 0.0) return float4(tint.rgb, 1.0);
    float3 n = normalize(i.nrm);
    float ndl = saturate(dot(n, lightDir.xyz));
    float hemi = 0.55 + 0.45 * saturate(n.y);           // sky ambient
    float light = 0.30 * hemi + 0.72 * ndl;
    int mode = (int)params.x;
    float t = saturate((i.wpos.y - params.y) / max(params.z - params.y, 0.001));
    float3 base;
    if (flags.x > 1.5) {
        // water sheet: deep tint, brighter towards grazing angles, faint ripple
        float3 v = normalize(eye.xyz - i.wpos);
        float rim = pow(1.0 - saturate(dot(n, v)), 2.0);
        float ripple = 0.5 + 0.5 * sin(i.wpos.x * 1.7 + params.w * 0.8) * sin(i.wpos.z * 1.3 - params.w * 0.6);
        float3 deep = float3(0.10, 0.26, 0.40), shallow = float3(0.30, 0.55, 0.70);
        if (i.walk < 0.5) { deep = float3(0.62, 0.70, 0.78); shallow = float3(0.86, 0.92, 0.97); ripple = 0.0; }   // ice
        float3 col = lerp(deep, shallow, 0.35 * rim + 0.15 * ripple) * (0.55 + 0.6 * ndl);
        float dist = distance(eye.xyz, i.wpos);
        float haze = saturate((dist - eye.w * 1.5) / (eye.w * 4.0));
        float fade = i.walk < 0.5 ? 0.9 : saturate(i.uv.x);   // engine depth fade: transparent at the shore
        return float4(lerp(col, float3(0.075, 0.07, 0.10), haze * 0.7), fade * (0.7 + 0.25 * rim));
    }
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
        // walkable = teal, blocked = orange-red AND diagonally striped, so the two read
        // apart without the hue (red/green colour blindness)
        float3 ok = float3(0.30, 0.80, 0.55), no = float3(0.90, 0.40, 0.20);
        base = lerp(no, ok, saturate(i.walk)) * 0.9;
        float stripe = step(0.5, frac((i.wpos.x + i.wpos.z) * 0.25));
        base = lerp(base, base * 0.6, stripe * (1.0 - saturate(i.walk)));
        // subtle 1-unit grid so cells read
        float2 g = abs(frac(float2(i.wpos.x, i.wpos.z)) - 0.5);
        float gridLine = smoothstep(0.47, 0.5, max(g.x, g.y));
        base = lerp(base, base * 0.6, gridLine * 0.5);
    } else {
        base = heightRamp(t);
    }
    if (lightDir.w > 0.5 && mode != 2) {
        // the LEV cell grid: one line per cell, a stronger one every 8 (the STB patch
        // size). Screen-space widths (fwidth) keep the lines ~1.5 px at any zoom; the
        // per-cell lines fade out when a cell is under ~6 px so they never moire.
        float2 pos = float2(i.wpos.x, i.wpos.z);
        float2 fw = max(fwidth(pos), 1e-4);
        float2 d1 = abs(frac(pos) - 0.5);
        float2 w1 = min(fw * 1.5, 0.2), w8 = min(fw / 8.0 * 1.5, 0.2);
        float2 l1 = smoothstep(0.5 - w1, 0.5, d1);
        float cell = max(l1.x, l1.y) * saturate((0.16 - max(fw.x, fw.y)) / 0.1);
        float2 d8 = abs(frac(pos / 8.0) - 0.5);
        float2 l8 = smoothstep(0.5 - w8, 0.5, d8);
        float patch = max(l8.x, l8.y) * saturate((0.16 - max(fw.x, fw.y) / 8.0) / 0.1);
        float3 ink = float3(0.08, 0.06, 0.12);
        base = lerp(base, ink, max(cell * 0.35, patch * 0.75));
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

struct ObjectCB {
    float world[16];
    float tint[4];
};

const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
const float kNoTint[4] = {0, 0, 0, 0};

// Row-vector axis swap Fable (x, y, z-up) -> render (x, z, -y): v_render = v_fable * P.
const float kFableToYUp[16] = {1, 0, 0, 0,
                               0, 0, -1, 0,
                               0, 1, 0, 0,
                               0, 0, 0, 1};

bool rayAabb(const float o[3], const float d[3], const float mn[3], const float mx[3], float& tNear) {
    float t0 = 0, t1 = 1e30f;
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(d[k]) < 1e-12f) { if (o[k] < mn[k] || o[k] > mx[k]) return false; continue; }
        float a = (mn[k] - o[k]) / d[k], b = (mx[k] - o[k]) / d[k];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a); t1 = std::min(t1, b);
        if (t0 > t1) return false;
    }
    tNear = t0;
    return true;
}

bool rayTriangle(const float o[3], const float d[3], const float* v, float& t) {
    // Moller-Trumbore; v = 9 floats (a, b, c)
    const float e1[3] = {v[3] - v[0], v[4] - v[1], v[5] - v[2]};
    const float e2[3] = {v[6] - v[0], v[7] - v[1], v[8] - v[2]};
    const float p[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
    const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (std::fabs(det) < 1e-12f) return false;
    const float inv = 1.0f / det;
    const float s[3] = {o[0] - v[0], o[1] - v[1], o[2] - v[2]};
    const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
    if (u < 0 || u > 1) return false;
    const float q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
    const float w = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
    if (w < 0 || u + w > 1) return false;
    t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
    return t > 1e-6f;
}

bool invert4(const float m[16], float out[16]) {
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-30f) return false;
    for (int i = 0; i < 16; ++i) out[i] = inv[i] / det;
    return true;
}

void xformPoint(const float m[16], const float p[3], float out[3]) {
    out[0] = p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12];
    out[1] = p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13];
    out[2] = p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14];
}
void xformDir(const float m[16], const float p[3], float out[3]) {
    out[0] = p[0] * m[0] + p[1] * m[4] + p[2] * m[8];
    out[1] = p[0] * m[1] + p[1] * m[5] + p[2] * m[9];
    out[2] = p[0] * m[2] + p[1] * m[6] + p[2] * m[10];
}

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
    for (auto& [id, srv] : swatches_) release(srv);
    swatches_.clear();
    releaseTarget();
    releaseMesh();
    for (int i = 0; i < kLayers; ++i) clearLayer(i);
    clearThings();
    release(white_);
    release(blend_); release(alphaBlend_); release(depth_); release(depthNoWrite_); release(depthOverlay_); release(wire_); release(solid_); release(sampler_); release(wrapSampler_);
    release(cbuffer_); release(ocbuffer_); release(layout_); release(ps_); release(vs_);
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
    cb.ByteWidth = sizeof(ObjectCB);
    device_->CreateBuffer(&cb, nullptr, &ocbuffer_);

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
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    device_->CreateBlendState(&bd, &alphaBlend_);
    D3D11_DEPTH_STENCIL_DESC dw = {};
    dw.DepthEnable = TRUE; dw.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; dw.DepthFunc = D3D11_COMPARISON_LESS;
    device_->CreateDepthStencilState(&dw, &depthNoWrite_);
    dw.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    device_->CreateDepthStencilState(&dw, &depthOverlay_);

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
    return vs_ && ps_ && layout_ && cbuffer_ && ocbuffer_;
}

void Renderer::setObject(const float world[16], const float tint[4]) {
    ObjectCB o;
    std::memcpy(o.world, world, sizeof o.world);
    std::memcpy(o.tint, tint, sizeof o.tint);
    D3D11_MAPPED_SUBRESOURCE map;
    if (SUCCEEDED(ctx_->Map(ocbuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) { std::memcpy(map.pData, &o, sizeof o); ctx_->Unmap(ocbuffer_, 0); }
}

void Renderer::clearThings() {
    for (auto& m : meshes_) for (auto& p : m.parts) { release(p.vb); release(p.srv); }
    meshes_.clear();
    instances_.clear();
}

bool Renderer::uploadThings(const foliageexport::Scene& scene, terrainexport::UpAxis up) {
    clearThings();
    thingsUp_ = up;
    if (scene.instances.empty()) return false;
    // meshes: one vertex buffer per part, mesh-local Fable axes (cm)
    std::map<int, ID3D11ShaderResourceView*> imageSrv;
    meshes_.resize(scene.meshes.size());
    for (size_t mi = 0; mi < scene.meshes.size(); ++mi) {
        const auto& m = scene.meshes[mi];
        auto& g = meshes_[mi];
        g.bmin[0] = g.bmin[1] = g.bmin[2] = 1e30f; g.bmax[0] = g.bmax[1] = g.bmax[2] = -1e30f;
        for (const auto& part : m.parts) {
            std::vector<GpuVertex> verts;
            verts.reserve(part.indices.size());
            for (size_t k = 0; k + 2 < part.indices.size(); k += 3) {
                const uint32_t ids[3] = {part.indices[k], part.indices[k + 1], part.indices[k + 2]};
                bool ok = true;
                for (uint32_t id : ids) ok = ok && id < m.geometry.vertices.size();
                if (!ok) continue;
                for (uint32_t id : ids) {
                    const auto& v = m.geometry.vertices[id];
                    verts.push_back({v.x, v.y, v.z, v.nx, v.ny, v.nz, v.u, v.v, 1.0f});
                    g.tris.push_back(v.x); g.tris.push_back(v.y); g.tris.push_back(v.z);
                    g.bmin[0] = std::min(g.bmin[0], v.x); g.bmin[1] = std::min(g.bmin[1], v.y); g.bmin[2] = std::min(g.bmin[2], v.z);
                    g.bmax[0] = std::max(g.bmax[0], v.x); g.bmax[1] = std::max(g.bmax[1], v.y); g.bmax[2] = std::max(g.bmax[2], v.z);
                }
            }
            if (verts.empty()) continue;
            FoliageBatch b;
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth = UINT(verts.size() * sizeof(GpuVertex)); bd.Usage = D3D11_USAGE_IMMUTABLE;
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA sd = {verts.data(), 0, 0};
            if (FAILED(device_->CreateBuffer(&bd, &sd, &b.vb))) continue;
            b.count = uint32_t(verts.size());
            b.alpha = part.hasAlpha;
            if (part.image >= 0 && size_t(part.image) < scene.images.size()) {
                auto hit = imageSrv.find(part.image);
                if (hit == imageSrv.end()) hit = imageSrv.emplace(part.image, makeTexture(scene.images[size_t(part.image)])).first;
                if (hit->second) { b.srv = hit->second; b.srv->AddRef(); }
            }
            g.parts.push_back(b);
        }
        if (g.tris.empty()) { g.bmin[0] = g.bmin[1] = g.bmin[2] = 0; g.bmax[0] = g.bmax[1] = g.bmax[2] = 0; }
    }
    for (auto& [id, srv] : imageSrv) release(srv);
    instances_.reserve(scene.instances.size());
    for (const auto& inst : scene.instances) {
        if (inst.mesh < 0 || size_t(inst.mesh) >= meshes_.size()) continue;
        float col[3][3]; foliageexport::instanceBasis(inst, col);
        const float world[16] = {col[0][0], col[0][1], col[0][2], 0,
                                 col[1][0], col[1][1], col[1][2], 0,
                                 col[2][0], col[2][1], col[2][2], 0,
                                 inst.x, inst.y, inst.z, 1};
        InstanceDraw d;
        d.mesh = inst.mesh; d.thing = inst.thing;
        instances_.push_back(d);
        setInstanceWorld(instances_.size() - 1, world);
    }
    return !instances_.empty();
}

void Renderer::setInstanceWorld(size_t i, const float fableWorld[16]) {
    if (i >= instances_.size()) return;
    if (thingsUp_ == terrainexport::UpAxis::Y) mul4(fableWorld, kFableToYUp, instances_[i].world);
    else std::memcpy(instances_[i].world, fableWorld, sizeof(float) * 16);
}

bool Renderer::instanceBounds(size_t i, float center[3], float& radius) const {
    if (i >= instances_.size()) return false;
    const auto& d = instances_[i];
    const auto& g = meshes_[size_t(d.mesh)];
    const float c[3] = {(g.bmin[0] + g.bmax[0]) * 0.5f, (g.bmin[1] + g.bmax[1]) * 0.5f, (g.bmin[2] + g.bmax[2]) * 0.5f};
    xformPoint(d.world, c, center);
    const float e[3] = {(g.bmax[0] - g.bmin[0]) * 0.5f, (g.bmax[1] - g.bmin[1]) * 0.5f, (g.bmax[2] - g.bmin[2]) * 0.5f};
    float ex[3]; xformDir(d.world, e, ex);
    float ey[3]; const float e2[3] = {e[0], -e[1], e[2]}; xformDir(d.world, e2, ey);
    radius = std::max(std::sqrt(ex[0] * ex[0] + ex[1] * ex[1] + ex[2] * ex[2]), std::sqrt(ey[0] * ey[0] + ey[1] * ey[1] + ey[2] * ey[2]));
    return true;
}

int Renderer::pick(const float origin[3], const float dir[3], float& tBest) const {
    int best = -1;
    tBest = 1e30f;
    for (size_t i = 0; i < instances_.size(); ++i) {
        const auto& d = instances_[i];
        if (!d.visible) continue;
        const auto& g = meshes_[size_t(d.mesh)];
        if (g.tris.empty()) continue;
        float inv[16];
        if (!invert4(d.world, inv)) continue;
        float lo[3], ld[3];
        xformPoint(inv, origin, lo);
        xformDir(inv, dir, ld);
        // parameter t is preserved by the affine map when we keep ld unnormalised
        float tBox;
        if (!rayAabb(lo, ld, g.bmin, g.bmax, tBox) || tBox > tBest) continue;
        for (size_t k = 0; k + 8 < g.tris.size(); k += 9) {
            float t;
            if (rayTriangle(lo, ld, &g.tris[k], t) && t < tBest) { tBest = t; best = int(i); }
        }
    }
    return best;
}

bool Renderer::rebuildTerrainBuffer() {
    if (!cellsX_ || !cellsY_ || heights_.size() != size_t(cellsX_) * cellsY_) return false;
    std::vector<GpuVertex> verts(heights_.size());
    const int cx = cellsX_, cy = cellsY_;
    auto h = [&](int x, int y) { x = std::clamp(x, 0, cx - 1); y = std::clamp(y, 0, cy - 1); return heights_[size_t(y) * cx + x]; };
    minH_ = 1e30f; maxH_ = -1e30f;
    for (int y = 0; y < cy; ++y)
        for (int x = 0; x < cx; ++x) {
            const size_t i = size_t(y) * cx + x;
            const float z = heights_[i];
            minH_ = std::min(minH_, z); maxH_ = std::max(maxH_, z);
            const float dx0 = x > 0 ? 1.0f : 0.0f, dx1 = x < cx - 1 ? 1.0f : 0.0f;
            const float dy0 = y > 0 ? 1.0f : 0.0f, dy1 = y < cy - 1 ? 1.0f : 0.0f;
            const float dzdx = (h(x + 1, y) - h(x - 1, y)) / std::max(dx0 + dx1, 1.0f);
            const float dzdy = (h(x, y + 1) - h(x, y - 1)) / std::max(dy0 + dy1, 1.0f);
            float nx = -dzdx, ny = -dzdy, nz = 1.0f;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= len; ny /= len; nz /= len;
            GpuVertex& g = verts[i];
            const float fx = originX_ + float(x), fy = originY_ + float(y);
            if (terrainYUp_) { g.px = fx; g.py = z; g.pz = -fy; g.nx = nx; g.ny = nz; g.nz = -ny; }
            else { g.px = fx; g.py = fy; g.pz = z; g.nx = nx; g.ny = ny; g.nz = nz; }
            g.u = terrainUv_[i * 2]; g.v = terrainUv_[i * 2 + 1];
            g.walk = walk_[i] ? 1.0f : 0.0f;
        }
    release(vb_);
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = UINT(verts.size() * sizeof(GpuVertex)); bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd = {verts.data(), 0, 0};
    return SUCCEEDED(device_->CreateBuffer(&bd, &sd, &vb_));
}

bool Renderer::updateTerrain(const float* heights, const uint8_t* walkable, int cellsX, int cellsY) {
    if (cellsX != cellsX_ || cellsY != cellsY_ || !indexCount_) return false;
    std::memcpy(heights_.data(), heights, heights_.size() * sizeof(float));
    if (walkable) std::memcpy(walk_.data(), walkable, walk_.size());
    return rebuildTerrainBuffer();
}

bool Renderer::rayTerrain(const float origin[3], const float dir[3], float hit[3]) const {
    if (!cellsX_ || !cellsY_) return false;
    // march along the ray; convert each sample to grid space and compare with the bilinear height
    auto heightAt = [&](float gx, float gy, float& out) {
        if (gx < 0 || gy < 0 || gx > float(cellsX_ - 1) || gy > float(cellsY_ - 1)) return false;
        const int x0 = std::min(int(gx), cellsX_ - 1), y0 = std::min(int(gy), cellsY_ - 1);
        const int x1 = std::min(x0 + 1, cellsX_ - 1), y1 = std::min(y0 + 1, cellsY_ - 1);
        const float fx = gx - float(x0), fy = gy - float(y0);
        auto h = [&](int x, int y) { return heights_[size_t(y) * cellsX_ + x]; };
        out = (h(x0, y0) * (1 - fx) + h(x1, y0) * fx) * (1 - fy) + (h(x0, y1) * (1 - fx) + h(x1, y1) * fx) * fy;
        return true;
    };
    auto sample = [&](float t, float& gx, float& gy, float& z) {
        const float px = origin[0] + dir[0] * t, py = origin[1] + dir[1] * t, pz = origin[2] + dir[2] * t;
        if (terrainYUp_) { gx = px - originX_; gy = -pz - originY_; z = py; }
        else { gx = px - originX_; gy = py - originY_; z = pz; }
    };
    const float maxT = 20000.0f;
    float step = 0.5f, tPrev = 0, gx, gy, z, hz;
    bool prevAbove = true, havePrev = false;
    for (float t = 0; t < maxT; t += step) {
        sample(t, gx, gy, z);
        if (!heightAt(gx, gy, hz)) { havePrev = false; step = std::min(step * 1.5f, 8.0f); continue; }
        step = 0.5f;
        const bool above = z > hz;
        if (havePrev && prevAbove && !above) {
            // refine between tPrev and t
            float lo = tPrev, hi = t;
            for (int k = 0; k < 12; ++k) {
                const float mid = (lo + hi) * 0.5f;
                sample(mid, gx, gy, z);
                if (heightAt(gx, gy, hz) && z > hz) lo = mid; else hi = mid;
            }
            const float th = (lo + hi) * 0.5f;
            hit[0] = origin[0] + dir[0] * th; hit[1] = origin[1] + dir[1] * th; hit[2] = origin[2] + dir[2] * th;
            return true;
        }
        prevAbove = above; havePrev = true; tPrev = t;
    }
    return false;
}

bool Renderer::project(const float p[3], float& u, float& v) const {
    float e[4], c[4];
    const float q[4] = {p[0], p[1], p[2], 1.0f};
    for (int j = 0; j < 4; ++j) e[j] = q[0] * lastView_[j] + q[1] * lastView_[4 + j] + q[2] * lastView_[8 + j] + q[3] * lastView_[12 + j];
    for (int j = 0; j < 4; ++j) c[j] = e[0] * lastProj_[j] + e[1] * lastProj_[4 + j] + e[2] * lastProj_[8 + j] + e[3] * lastProj_[12 + j];
    if (c[3] <= 1e-6f) return false;
    u = c[0] / c[3] * 0.5f + 0.5f; v = 0.5f - c[1] / c[3] * 0.5f;
    return true;
}

void Renderer::screenRay(float u, float v, float origin[3], float dir[3]) const {
    lastCamera_.eye(origin);
    float f[3], r[3], up[3];
    lastCamera_.dir(f); lastCamera_.right(r); lastCamera_.up(up);
    const float th = std::tan(lastCamera_.fovY * 0.5f);
    const float sx = (2.0f * u - 1.0f) * th * lastAspect_, sy = (1.0f - 2.0f * v) * th;
    for (int k = 0; k < 3; ++k) dir[k] = f[k] + r[k] * sx + up[k] * sy;
    const float l = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (l > 1e-9f) for (int k = 0; k < 3; ++k) dir[k] /= l;
}

void Renderer::releaseMesh() {
    release(vb_); release(ib_); release(albedo_); release(waterVb_); release(waterIb_);
    indexCount_ = 0; waterIndexCount_ = 0;
}

void Renderer::clear() { releaseMesh(); for (int i = 0; i < kLayers; ++i) clearLayer(i); }

void Renderer::clearLayer(int layer) {
    for (auto& b : layers_[layer]) { release(b.vb); release(b.srv); }
    layers_[layer].clear();
}

ID3D11ShaderResourceView* Renderer::swatch(uint32_t id, const terrainexport::Image& img) {
    auto it = swatches_.find(id);
    if (it != swatches_.end()) return it->second;
    ID3D11ShaderResourceView* srv = nullptr;
    if (img.width >= 64 && img.height >= 64 && !img.rgba.empty()) {
        // box-filter down to 64x64 (the picker draws it at 22..32 px)
        terrainexport::Image small;
        small.width = small.height = 64;
        small.rgba.resize(64 * 64 * 4);
        const uint32_t sx = img.width / 64, sy = img.height / 64;
        for (uint32_t y = 0; y < 64; ++y)
            for (uint32_t x = 0; x < 64; ++x) {
                uint32_t acc[4] = {0, 0, 0, 0};
                for (uint32_t yy = 0; yy < sy; ++yy)
                    for (uint32_t xx = 0; xx < sx; ++xx) {
                        const uint8_t* p = &img.rgba[((y * sy + yy) * img.width + (x * sx + xx)) * 4];
                        for (int c = 0; c < 4; ++c) acc[c] += p[c];
                    }
                uint8_t* o = &small.rgba[(y * 64 + x) * 4];
                for (int c = 0; c < 4; ++c) o[c] = uint8_t(acc[c] / (sx * sy));
            }
        srv = makeTexture(small);
    } else srv = makeTexture(img);
    swatches_[id] = srv;   // a null entry remembers a texture that could not be made
    return srv;
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
    // CPU grid copy for in-place height edits (the scene is a regular (w+1)x(h+1) grid)
    cellsX_ = scene.mapWidth + 1; cellsY_ = scene.mapHeight + 1;
    terrainYUp_ = scene.up == terrainexport::UpAxis::Y;
    if (size_t(cellsX_) * size_t(cellsY_) == scene.vertices.size()) {
        heights_.resize(scene.vertices.size()); walk_.resize(scene.vertices.size()); terrainUv_.resize(scene.vertices.size() * 2);
        for (size_t i = 0; i < scene.vertices.size(); ++i) {
            const auto& s = scene.vertices[i];
            heights_[i] = terrainYUp_ ? s.py : s.pz;
            walk_[i] = s.walkable ? 1 : 0;
            terrainUv_[i * 2] = s.u; terrainUv_[i * 2 + 1] = s.v;
        }
        originX_ = scene.vertices[0].px;
        originY_ = terrainYUp_ ? -scene.vertices[0].pz : scene.vertices[0].py;
    } else { cellsX_ = cellsY_ = 0; heights_.clear(); walk_.clear(); }
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
    if (!scene.water.empty()) {
        const size_t wn = scene.water.positions.size() / 3;
        std::vector<GpuVertex> wv(wn);
        for (size_t i = 0; i < wn; ++i) {
            const float* p = &scene.water.positions[i * 3];
            wv[i] = {p[0], p[1], p[2], 0.0f, 1.0f, 0.0f, scene.water.fade[i], 0.0f, scene.water.ice[i] ? 0.0f : 1.0f};   // u = depth fade, walk = 0 marks ice
            if (scene.up == terrainexport::UpAxis::Z) { wv[i].ny = 0.0f; wv[i].nz = 1.0f; }
        }
        D3D11_BUFFER_DESC wd = {};
        wd.ByteWidth = UINT(wv.size() * sizeof(GpuVertex)); wd.Usage = D3D11_USAGE_IMMUTABLE; wd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA wsd = {wv.data(), 0, 0};
        if (SUCCEEDED(device_->CreateBuffer(&wd, &wsd, &waterVb_))) {
            std::vector<uint32_t> all(scene.water.indices);
            all.insert(all.end(), scene.water.iceIndices.begin(), scene.water.iceIndices.end());
            wd.ByteWidth = UINT(all.size() * 4); wd.BindFlags = D3D11_BIND_INDEX_BUFFER;
            wsd.pSysMem = all.data();
            if (SUCCEEDED(device_->CreateBuffer(&wd, &wsd, &waterIb_))) waterIndexCount_ = uint32_t(all.size());
            else release(waterVb_);
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
    std::memcpy(lastView_, view, sizeof lastView_);
    std::memcpy(lastProj_, proj, sizeof lastProj_);
    lastCamera_ = camera;
    lastAspect_ = float(width) / float(height);
    FrameCB cb = {};
    mul4(view, proj, cb.viewProj);
    // Sun: from the upper-left-front, slowly not moving (stable screenshots).
    float l[3] = {-0.45f, 0.8f, 0.35f};
    const float ll = std::sqrt(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
    cb.lightDir[0] = l[0] / ll; cb.lightDir[1] = l[1] / ll; cb.lightDir[2] = l[2] / ll; cb.lightDir[3] = showGrid ? 1.0f : 0.0f;
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
    ctx_->VSSetConstantBuffers(1, 1, &ocbuffer_);
    ctx_->PSSetShader(ps_, nullptr, 0);
    ctx_->PSSetConstantBuffers(0, 1, &cbuffer_);
    ctx_->PSSetConstantBuffers(1, 1, &ocbuffer_);
    setObject(kIdentity, kNoTint);
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

    if (showThings && !instances_.empty() && mode != ViewMode::Wireframe) {
        cb.flags[0] = 1.0f; cb.flags[1] = 1.0f;
        if (SUCCEEDED(ctx_->Map(cbuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) { std::memcpy(map.pData, &cb, sizeof cb); ctx_->Unmap(cbuffer_, 0); }
        ctx_->RSSetState(solid_);
        ctx_->PSSetSamplers(0, 1, &wrapSampler_);
        bool currentAlpha = true;
        for (const auto& d : instances_) {
            if (!d.visible) continue;
            setObject(d.world, kNoTint);
            for (const auto& b : meshes_[size_t(d.mesh)].parts) {
                if (b.alpha != currentAlpha) {
                    currentAlpha = b.alpha;
                    cb.flags[1] = currentAlpha ? 1.0f : 0.0f;
                    if (SUCCEEDED(ctx_->Map(cbuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) { std::memcpy(map.pData, &cb, sizeof cb); ctx_->Unmap(cbuffer_, 0); }
                }
                ID3D11ShaderResourceView* t = b.srv ? b.srv : white_;
                ctx_->PSSetShaderResources(0, 1, &t);
                ctx_->IASetVertexBuffers(0, 1, &b.vb, &stride, &offset);
                ctx_->Draw(b.count, 0);
            }
        }
        if (selectedThing >= 0) {
            // outline: the selected thing's instances again as an accent wireframe
            const float accent[4] = {0.78f, 0.62f, 1.0f, 1.0f};
            ctx_->RSSetState(wire_);
            ctx_->OMSetDepthStencilState(depthOverlay_, 0);
            for (const auto& d : instances_) {
                if (!d.visible || d.thing != selectedThing) continue;
                setObject(d.world, accent);
                for (const auto& b : meshes_[size_t(d.mesh)].parts) {
                    ctx_->IASetVertexBuffers(0, 1, &b.vb, &stride, &offset);
                    ctx_->Draw(b.count, 0);
                }
            }
            ctx_->RSSetState(solid_);
            ctx_->OMSetDepthStencilState(depth_, 0);
        }
        setObject(kIdentity, kNoTint);
    }

    if (showWater && waterIndexCount_ && mode != ViewMode::Wireframe) {
        cb.flags[0] = 2.0f; cb.flags[1] = 0.0f;
        if (SUCCEEDED(ctx_->Map(cbuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) { std::memcpy(map.pData, &cb, sizeof cb); ctx_->Unmap(cbuffer_, 0); }
        ctx_->IASetVertexBuffers(0, 1, &waterVb_, &stride, &offset);
        ctx_->IASetIndexBuffer(waterIb_, DXGI_FORMAT_R32_UINT, 0);
        ctx_->RSSetState(solid_);
        ctx_->OMSetDepthStencilState(depthNoWrite_, 0);
        ctx_->OMSetBlendState(alphaBlend_, bf, 0xFFFFFFFF);
        ctx_->DrawIndexed(waterIndexCount_, 0, 0);
        ctx_->OMSetBlendState(blend_, bf, 0xFFFFFFFF);
        ctx_->OMSetDepthStencilState(depth_, 0);
    }

    ID3D11ShaderResourceView* nullSrv = nullptr;
    ctx_->PSSetShaderResources(0, 1, &nullSrv);
    ID3D11RenderTargetView* nullRtv = nullptr;
    ctx_->OMSetRenderTargets(1, &nullRtv, nullptr);
    return srv_;
}

} // namespace albion::gui
