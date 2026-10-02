#include "particlepreviewrenderer.hpp"
#include "renderer.hpp"
#include "particlepreview.hpp"
#include <forge/meshpreview.hpp>

#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>

namespace albion::gui {
namespace {
template<class T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }
constexpr char shader[] = R"(
struct V { float4 pos:POSITION; float2 uv:TEXCOORD0; float4 color:COLOR0; float blendMode:TEXCOORD1; };
struct P { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; float4 color:COLOR0; float blendMode:TEXCOORD1; };
P VS(V v) { P p; p.pos=v.pos; p.uv=v.uv; p.color=v.color; p.blendMode=v.blendMode; return p; }
Texture2D tex:register(t0); SamplerState samp:register(s0);
// EgoCore 55bdc10 ParticleRenderer.h sprite PS and additive CPU preparation:
// MODULATE2X RGB; additive fades use vertex alpha, not texture alpha again.
float4 PS(P p):SV_TARGET { float4 t=tex.Sample(samp,p.uv); float4 c=t*p.color;
    c.rgb*=2.0;
    if(p.blendMode==3 || p.blendMode==4) c.rgb*=p.color.a;
    if(p.blendMode<=2) clip(c.a-(16.0/255.0));
    return c; }
)";
struct Vertex { float clip[4], uv[2], colour[4], blendMode; };
constexpr char meshShader[] = R"(
cbuffer MeshConstants:register(b0) { float4 clipX,clipY,clipZ,clipW,colour; float4 options; };
struct V { float3 pos:POSITION; float2 uv:TEXCOORD0; };
struct P { float4 pos:SV_POSITION; float2 uv:TEXCOORD0; };
P VS(V v) { P p; float4 x=float4(v.pos,1); p.pos=float4(dot(x,clipX),dot(x,clipY),dot(x,clipZ),dot(x,clipW)); p.uv=v.uv; return p; }
Texture2D tex:register(t0); SamplerState samp:register(s0);
// EgoCore 55bdc10 mesh shader: unlike sprites, additive uses final sampled alpha.
float4 PS(P p):SV_TARGET { float4 t=tex.Sample(samp,float2(p.uv.x,options.z*.5+frac(p.uv.y)*(options.y-options.z)));
 float brightness=max(t.r,max(t.g,t.b)); if(brightness<.05) t.a*=saturate((brightness-.01)*25);
 float4 c=float4(saturate(2*t.rgb*colour.rgb),t.a*colour.a);
 if(options.x==3 || options.x==4) c.rgb=saturate(c.rgb*c.a);
 else if(options.x<=2) clip(c.a-16.0/255.0); return c; }
)";
struct MeshVertex { float x,y,z,u,v; };
struct MeshConstants { float clip[4][4],colour[4],options[4]; };

int mode(int m) { return m>=0 && m<=5 ? m : 2; }
int operation(int op) { return op>=0 && op<=2 ? op : 0; }
void meshBasis(const particlepreview::DrawMesh& instance,float radius,float basis[3][3]) {
    // Row-vector quaternion matrix, matching XMMatrixRotationQuaternion.
    double length=0;for(float v:instance.orientation)length+=double(v)*v;
    float q[4]={0,0,0,1};if(length>=.0001)for(int k=0;k<4;++k)q[k]=float(instance.orientation[k]/std::sqrt(length));
    const float x=q[0],y=q[1],z=q[2],w=q[3];
    const float rotation[3][3]={{1-2*y*y-2*z*z,2*x*y+2*z*w,2*x*z-2*y*w},
             {2*x*y-2*z*w,1-2*x*x-2*z*z,2*y*z+2*x*w},
             {2*x*z+2*y*w,2*y*z-2*x*w,1-2*x*x-2*y*y}};
    for(int j=0;j<3;++j)for(int k=0;k<3;++k)basis[j][k]=rotation[j][k]*(instance.size[j]/radius*(j<2?-1.f:1.f));
}

float dot(const float a[3], const float b[3]) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
}

struct ParticlePreviewRenderer::Impl {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11VertexShader* meshVs = nullptr;
    ID3D11PixelShader* meshPs = nullptr;
    ID3D11InputLayout* meshLayout = nullptr;
    ID3D11Buffer* meshConstants = nullptr;
    ID3D11SamplerState* meshSampler = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11Buffer* buffer = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11RasterizerState* raster = nullptr;
    ID3D11DepthStencilState* depth = nullptr;
    ID3D11BlendState* blends[6][3] = {};
    ID3D11ShaderResourceView* white = nullptr;
    ID3D11RenderTargetView* target = nullptr;
    ID3D11ShaderResourceView* view = nullptr;
    ID3D11DepthStencilView* depthView = nullptr;
    int width = 0, height = 0;
    struct Texture { ID3D11ShaderResourceView* view; uint32_t width, height, frames; size_t bytes; };
    std::map<int32_t, Texture> textures;
    size_t textureBytes = 0, drawn = 0, missing = 0, dropped = 0;
    struct Mesh {
        ID3D11Buffer *vertices=nullptr,*indices=nullptr;
        struct Part { int32_t texture; UINT first,count; };
        std::vector<Part> parts;
        float centre[3]={}, radius=1;
        float lo[3]={},hi[3]={};
        bool authoredBounds=false;
        size_t bytes=0, triangles=0;
    };
    std::map<int32_t,Mesh> meshes;
    size_t meshBytes=0,drawnMeshes=0,missingMeshes=0,droppedMeshes=0,meshTriangles=0;
    void clearMeshes() { for(auto& [id,m]:meshes) { release(m.vertices);release(m.indices); } meshes.clear();meshBytes=0; }
    std::string error;
    bool ready = false;
    ~Impl() {
        clearMeshes(); release(meshVs);release(meshPs);release(meshLayout);release(meshConstants);release(meshSampler);
        clearTextures(); clearTarget(); release(white); release(vs); release(ps);
        release(layout); release(buffer); release(sampler); release(raster); release(depth);
        for(auto& row:blends) for(auto& b:row) release(b);
        release(context); release(device);
    }
    void clearTextures() { for(auto& [id,t]:textures) release(t.view); textures.clear(); textureBytes=0; }
    void clearTarget() { release(target); release(view); release(depthView); width=height=0; }
    bool check(HRESULT hr, const char* what) {
        if(SUCCEEDED(hr)) return true;
        error=std::string(what)+" (HRESULT "+std::to_string(static_cast<unsigned long>(hr))+")";
        return false;
    }
    bool resize(int w,int h) {
        if(w==width && h==height && target) return true;
        clearTarget();
        D3D11_TEXTURE2D_DESC td{}; td.Width=w; td.Height=h; td.MipLevels=td.ArraySize=1;
        td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count=1;
        td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        ID3D11Texture2D* t=nullptr;
        if(!check(device->CreateTexture2D(&td,nullptr,&t),"Create preview target")) return false;
        bool ok=check(device->CreateRenderTargetView(t,nullptr,&target),"Create preview RTV") &&
                check(device->CreateShaderResourceView(t,nullptr,&view),"Create preview SRV");
        release(t);
        if(!ok) { clearTarget(); return false; }
        td.Format=DXGI_FORMAT_D24_UNORM_S8_UINT; td.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        if(!check(device->CreateTexture2D(&td,nullptr,&t),"Create preview depth")) { clearTarget(); return false; }
        ok=check(device->CreateDepthStencilView(t,nullptr,&depthView),"Create preview DSV");
        release(t); if(!ok) { clearTarget(); return false; }
        width=w; height=h; return true;
    }
};

ParticlePreviewRenderer::ParticlePreviewRenderer() : p_(std::make_unique<Impl>()) {}
ParticlePreviewRenderer::~ParticlePreviewRenderer() = default;
const std::string& ParticlePreviewRenderer::error() const { return p_->error; }
size_t ParticlePreviewRenderer::drawnSprites() const { return p_->drawn; }
size_t ParticlePreviewRenderer::missingTextures() const { return p_->missing; }
size_t ParticlePreviewRenderer::droppedSprites() const { return p_->dropped; }
void ParticlePreviewRenderer::clearTextures() { p_->clearTextures(); p_->missing=0; }
size_t ParticlePreviewRenderer::drawnMeshes() const { return p_->drawnMeshes; }
size_t ParticlePreviewRenderer::missingMeshes() const { return p_->missingMeshes; }
size_t ParticlePreviewRenderer::droppedMeshes() const { return p_->droppedMeshes; }
size_t ParticlePreviewRenderer::meshTriangles() const { return p_->meshTriangles; }
size_t ParticlePreviewRenderer::meshBytes() const { return p_->meshBytes; }
void ParticlePreviewRenderer::clearMeshes() { p_->clearMeshes();p_->drawnMeshes=p_->missingMeshes=p_->droppedMeshes=p_->meshTriangles=0; }
void ParticlePreviewRenderer::clear() { clearMeshes(); clearTextures(); p_->clearTarget(); p_->drawn=p_->dropped=0; p_->error.clear(); }

bool ParticlePreviewRenderer::init(ID3D11Device* device, ID3D11DeviceContext* context) {
    p_=std::make_unique<Impl>(); auto& p=*p_;
    if(!device || !context) { p.error="Particle preview requires a D3D device and context"; return false; }
    p.device=device; device->AddRef(); p.context=context; context->AddRef();
    ID3DBlob *v=nullptr,*f=nullptr,*err=nullptr;
    auto compile=[&](const char* entry,const char* profile,ID3DBlob** out) {
        HRESULT hr=D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,entry,profile,0,0,out,&err);
        if(FAILED(hr)) p.error=err ? std::string(static_cast<const char*>(err->GetBufferPointer()),err->GetBufferSize()) : "Particle shader compilation failed";
        release(err); return SUCCEEDED(hr);
    };
    if(!compile("VS","vs_4_0",&v)) return false;
    if(!compile("PS","ps_4_0",&f)) { release(v); return false; }
    const D3D11_INPUT_ELEMENT_DESC elements[]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32_FLOAT,0,40,D3D11_INPUT_PER_VERTEX_DATA,0}};
    bool ok=p.check(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&p.vs),"Create particle VS") &&
        p.check(device->CreatePixelShader(f->GetBufferPointer(),f->GetBufferSize(),nullptr,&p.ps),"Create particle PS") &&
        p.check(device->CreateInputLayout(elements,4,v->GetBufferPointer(),v->GetBufferSize(),&p.layout),"Create particle layout");
    release(v); release(f); if(!ok) return false;
    auto compileMesh=[&](const char* entry,const char* profile,ID3DBlob** out) {
        HRESULT hr=D3DCompile(meshShader,sizeof(meshShader)-1,nullptr,nullptr,nullptr,entry,profile,0,0,out,&err);
        if(FAILED(hr)) p.error=err?std::string(static_cast<const char*>(err->GetBufferPointer()),err->GetBufferSize()):"Mesh shader compilation failed";
        release(err);return SUCCEEDED(hr);
    };
    if(!compileMesh("VS","vs_4_0",&v)) return false;
    if(!compileMesh("PS","ps_4_0",&f)) {release(v);return false;}
    const D3D11_INPUT_ELEMENT_DESC meshElements[]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}};
    ok=p.check(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&p.meshVs),"Create mesh VS") &&
       p.check(device->CreatePixelShader(f->GetBufferPointer(),f->GetBufferSize(),nullptr,&p.meshPs),"Create mesh PS") &&
       p.check(device->CreateInputLayout(meshElements,2,v->GetBufferPointer(),v->GetBufferSize(),&p.meshLayout),"Create mesh layout");
    release(v);release(f);if(!ok)return false;
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(MeshConstants);cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    if(!p.check(device->CreateBuffer(&cb,nullptr,&p.meshConstants),"Create mesh constants"))return false;
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth=UINT((maxSprites*6+128)*sizeof(Vertex)); bd.Usage=D3D11_USAGE_DYNAMIC;
    bd.BindFlags=D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    if(!p.check(device->CreateBuffer(&bd,nullptr,&p.buffer),"Create particle vertex buffer")) return false;
    D3D11_SAMPLER_DESC sd{}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD=D3D11_FLOAT32_MAX;
    if(!p.check(device->CreateSamplerState(&sd,&p.sampler),"Create particle sampler")) return false;
    sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    if(!p.check(device->CreateSamplerState(&sd,&p.meshSampler),"Create mesh sampler"))return false;
    D3D11_RASTERIZER_DESC rd{}; rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; rd.DepthClipEnable=TRUE;
    if(!p.check(device->CreateRasterizerState(&rd,&p.raster),"Create particle rasterizer")) return false;
    D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable=TRUE; dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO; dd.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
    if(!p.check(device->CreateDepthStencilState(&dd,&p.depth),"Create particle depth state")) return false;
    constexpr D3D11_BLEND_OP ops[]={D3D11_BLEND_OP_ADD,D3D11_BLEND_OP_SUBTRACT,D3D11_BLEND_OP_REV_SUBTRACT};
    for(int m=0;m<6;++m) for(int op=0;op<3;++op) {
        D3D11_BLEND_DESC blend{}; auto& r=blend.RenderTarget[0]; r.BlendEnable=m!=0;
        // Keep the offscreen target opaque when ImGui composites it.
        r.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN|D3D11_COLOR_WRITE_ENABLE_BLUE;
        r.SrcBlend=D3D11_BLEND_SRC_ALPHA; r.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
        if(m==0) { r.SrcBlend=D3D11_BLEND_ONE; r.DestBlend=D3D11_BLEND_ZERO; }
        if(m==3 || m==4) { r.SrcBlend=D3D11_BLEND_ONE; r.DestBlend=m==3?D3D11_BLEND_ONE:D3D11_BLEND_INV_SRC_COLOR; }
        if(m==5) { r.SrcBlend=D3D11_BLEND_BLEND_FACTOR; r.DestBlend=D3D11_BLEND_INV_BLEND_FACTOR; }
        r.BlendOp=ops[op]; r.SrcBlendAlpha=D3D11_BLEND_ZERO; r.DestBlendAlpha=D3D11_BLEND_ONE; r.BlendOpAlpha=D3D11_BLEND_OP_ADD;
        if(!p.check(device->CreateBlendState(&blend,&p.blends[m][op]),"Create particle blend state")) return false;
    }
    terrainexport::Image white; white.width=white.height=1; white.rgba={255,255,255,255};
    if(!setTexture(-1,white,1)) return false;
    p.white=p.textures.at(-1).view; p.textures.erase(-1); p.textureBytes=0;
    p.ready=true; return true;
}

bool ParticlePreviewRenderer::setTexture(int32_t id,const terrainexport::Image& image,uint32_t frames) {
    auto& p=*p_; const size_t bytes=size_t(image.width)*image.height*4;
    if(!p.device || !image.width || !image.height || image.width>8192 || image.height>8192 ||
       !frames || image.height%frames || frames>image.height || image.rgba.size()!=bytes) {
        p.error="Invalid particle texture dimensions, pixels or frame count"; return false;
    }
    auto old=p.textures.find(id); size_t oldBytes=old==p.textures.end()?0:old->second.bytes;
    if(p.textureBytes-oldBytes+bytes>256ull*1024*1024) { p.error="Particle texture cache exceeds 256 MiB"; return false; }
    D3D11_TEXTURE2D_DESC td{}; td.Width=image.width; td.Height=image.height; td.MipLevels=td.ArraySize=1;
    td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_IMMUTABLE; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{image.rgba.data(),image.width*4,0}; ID3D11Texture2D* t=nullptr; ID3D11ShaderResourceView* srv=nullptr;
    if(!p.check(p.device->CreateTexture2D(&td,&data,&t),"Create particle texture")) return false;
    bool ok=p.check(p.device->CreateShaderResourceView(t,nullptr,&srv),"Create particle texture SRV"); release(t);
    if(!ok) return false;
    if(old!=p.textures.end()) release(old->second.view);
    p.textures[id]={srv,image.width,image.height,frames,bytes}; p.textureBytes=p.textureBytes-oldBytes+bytes;
    p.error.clear(); return true;
}

bool ParticlePreviewRenderer::meshUsesAuthoredBounds(int32_t id) const {
    auto it=p_->meshes.find(id);return it!=p_->meshes.end() && it->second.authoredBounds;
}
float ParticlePreviewRenderer::meshBoundsFactor(int32_t id,bool centred) const {
    auto it=p_->meshes.find(id);if(centred || it==p_->meshes.end())return 1;
    const auto& m=it->second;double distance2=0;for(float value:m.centre)distance2+=double(value)*value;
    return float(1+std::sqrt(distance2)/m.radius);
}
bool ParticlePreviewRenderer::meshFrameBounds(const particlepreview::DrawMesh& instance,float lo[3],float hi[3]) const {
    const auto it=p_->meshes.find(instance.mesh);if(it==p_->meshes.end())return false;
    const auto& mesh=it->second;float basis[3][3];meshBasis(instance,mesh.radius,basis);
    for(int k=0;k<3;++k){lo[k]=INFINITY;hi[k]=-INFINITY;}
    for(int corner=0;corner<8;++corner) {
        float point[3];std::copy(instance.position,instance.position+3,point);
        for(int j=0;j<3;++j) {
            const float local=((corner&(1<<j))?mesh.hi[j]:mesh.lo[j])-(instance.centredOnPosition?mesh.centre[j]:0.f);
            for(int k=0;k<3;++k)point[k]+=basis[j][k]*local;
        }
        for(int k=0;k<3;++k){if(!std::isfinite(point[k]))return false;lo[k]=std::min(lo[k],point[k]);hi[k]=std::max(hi[k],point[k]);}
    }
    return true;
}
bool ParticlePreviewRenderer::setMesh(int32_t id,const forge::meshpreview::Geometry& geometry) {
    auto& p=*p_;
    constexpr size_t maxBytes=128ull*1024*1024;
    if(!p.device || geometry.empty() || geometry.vertices.size()>1000000 || geometry.triangles.size()>1000000 || geometry.materials.size()>1024) {
        p.error="Invalid or oversized particle mesh";return false;
    }
    auto old=p.meshes.find(id);
    const size_t bytes=geometry.vertices.size()*sizeof(MeshVertex)+geometry.triangles.size()*3*sizeof(uint32_t);
    if((old==p.meshes.end() && p.meshes.size()>=64) || p.meshBytes-(old==p.meshes.end()?0:old->second.bytes)+bytes>maxBytes) {
        p.error="Particle mesh cache exceeds 64 meshes or 128 MiB";return false;
    }
    std::vector<MeshVertex> vertices;vertices.reserve(geometry.vertices.size());
    float lo[3]={INFINITY,INFINITY,INFINITY},hi[3]={-INFINITY,-INFINITY,-INFINITY};
    for(const auto& v:geometry.vertices) {
        const float values[]={v.x,v.y,v.z,v.u,v.v};
        for(float x:values)if(!std::isfinite(x)) {p.error="Non-finite particle mesh vertex";return false;}
        for(int k=0;k<3;++k){lo[k]=std::min(lo[k],values[k]);hi[k]=std::max(hi[k],values[k]);}
        vertices.push_back({v.x,v.y,v.z,v.u,v.v});
    }
    Impl::Mesh mesh;mesh.bytes=bytes;mesh.triangles=geometry.triangles.size();
    std::copy(lo,lo+3,mesh.lo);std::copy(hi,hi+3,mesh.hi);
    for(int k=0;k<3;++k){mesh.centre[k]=lo[k]*.5f+hi[k]*.5f;
        if(std::abs(mesh.centre[k])>1e12f){p.error="Particle mesh centre exceeds preview bounds";return false;}}
    double radius=0;
    for(const auto& v:vertices) {const double x=double(v.x)-mesh.centre[0],y=double(v.y)-mesh.centre[1],z=double(v.z)-mesh.centre[2];radius=std::max(radius,std::sqrt(x*x+y*y+z*z));}
    if(!std::isfinite(radius) || radius>1e12) {p.error="Invalid particle mesh bounds";return false;}
    mesh.radius=radius>.0001?float(radius):1.f;
    if(geometry.boundingSphere) {
        const auto& sphere=*geometry.boundingSphere;
        // Match the reference radius epsilon, and bound preview transforms independently of decoding.
        bool valid=std::isfinite(sphere.radius) && sphere.radius>.0001f && sphere.radius<=1e12f;
        for(float value:sphere.centre)valid=valid && std::isfinite(value) && std::abs(value)<=1e12f;
        if(valid) {
            std::copy(std::begin(sphere.centre),std::end(sphere.centre),mesh.centre);
            mesh.radius=sphere.radius;mesh.authoredBounds=true;
        }
    }
    std::map<int32_t,std::vector<uint32_t>> groups;
    for(const auto& t:geometry.triangles) {
        if(t.a>=vertices.size() || t.b>=vertices.size() || t.c>=vertices.size()) {p.error="Invalid particle mesh triangle index";return false;}
        int32_t texture=-1;
        if(t.material>=0 && size_t(t.material)<geometry.materials.size() && geometry.materials[t.material].diffuseTexture>0)texture=geometry.materials[t.material].diffuseTexture;
        auto& indices=groups[texture];indices.insert(indices.end(),{t.a,t.b,t.c});
    }
    std::vector<uint32_t> indices;indices.reserve(geometry.triangles.size()*3);
    for(const auto& [texture,part]:groups){mesh.parts.push_back({texture,UINT(indices.size()),UINT(part.size())});indices.insert(indices.end(),part.begin(),part.end());}
    D3D11_BUFFER_DESC bd{};bd.Usage=D3D11_USAGE_IMMUTABLE;bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;bd.ByteWidth=UINT(vertices.size()*sizeof(MeshVertex));
    D3D11_SUBRESOURCE_DATA data{vertices.data(),0,0};
    if(!p.check(p.device->CreateBuffer(&bd,&data,&mesh.vertices),"Create particle mesh vertices"))return false;
    bd.BindFlags=D3D11_BIND_INDEX_BUFFER;bd.ByteWidth=UINT(indices.size()*sizeof(uint32_t));data.pSysMem=indices.data();
    if(!p.check(p.device->CreateBuffer(&bd,&data,&mesh.indices),"Create particle mesh indices")){release(mesh.vertices);return false;}
    if(old!=p.meshes.end()){p.meshBytes-=old->second.bytes;release(old->second.vertices);release(old->second.indices);}
    p.meshBytes+=bytes;p.meshes[id]=std::move(mesh);p.error.clear();return true;
}
ID3D11ShaderResourceView* ParticlePreviewRenderer::render(int width,int height,const Camera& camera,
        const std::vector<particlepreview::DrawSprite>& sprites,const float background[3],bool grid) {
    static const std::vector<particlepreview::DrawMesh> empty;
    return render(width,height,camera,sprites,empty,background,grid);
}
ID3D11ShaderResourceView* ParticlePreviewRenderer::render(int width,int height,const Camera& camera,
        const std::vector<particlepreview::DrawSprite>& sprites,const std::vector<particlepreview::DrawMesh>& meshes,
        const float background[3],bool grid) {
    auto& p=*p_; p.drawn=p.missing=p.dropped=0;
    p.drawnMeshes=p.missingMeshes=p.droppedMeshes=p.meshTriangles=0;
    if(!p.ready || width<=0 || height<=0) return nullptr;
    if(!p.resize(std::min(width,4096),std::min(height,4096))) return nullptr;
    float right[3],up[3],forward[3]; camera.right(right); camera.up(up); camera.dir(forward);
    const float eye[]={camera.posX,camera.posY,camera.posZ};
    if(!std::isfinite(camera.fovY) || camera.fovY<=0 || camera.fovY>=3.14f) { p.error="Invalid particle camera"; return nullptr; }
    const float sy=1/std::tan(camera.fovY*.5f), sx=sy*float(p.height)/float(p.width);
    struct Item { const particlepreview::DrawSprite* sprite; float depth; const Impl::Texture* texture; };
    std::vector<Item> order; order.reserve(std::min(sprites.size(),maxSprites));
    for(const auto& s:sprites) {
        bool valid=std::isfinite(s.angle)&&std::isfinite(s.framePhase)&&s.size[0]>0&&s.size[1]>0;
        for(float v:s.position) valid=valid&&std::isfinite(v);
        for(float v:s.size) valid=valid&&std::isfinite(v);
        for(float v:s.colour) valid=valid&&std::isfinite(v);
        if(!valid) { ++p.dropped; continue; }
        float rel[]={s.position[0]-eye[0],s.position[2]-eye[1],-s.position[1]-eye[2]}; float z=dot(rel,forward);
        if(z<=.01f || z>=100000.f || s.colour[3]<=0) continue;
        const Impl::Texture* texture=nullptr;
        if(s.texture>=0) { auto it=p.textures.find(s.texture); if(it==p.textures.end()) { ++p.missing; continue; } texture=&it->second; }
        if(order.size()>=maxSprites) { ++p.dropped; continue; }
        order.push_back({&s,z,texture});
    }
    std::stable_sort(order.begin(),order.end(),[](const Item& a,const Item& b){ return a.depth>b.depth; });
    std::vector<Vertex> vertices; vertices.reserve(order.size()*6+(grid?128:0));
    if(grid) {
        const float brightness=(background[0]+background[1]+background[2])/3;
        const bool light=brightness>.5f;
        auto projectGrid=[&](float x,float y,const float colour[4],Vertex& vertex) {
            const float rel[]={x-eye[0],-eye[1],-y-eye[2]};
            const float z=dot(rel,forward);
            if(z<=.01f || z>=100000.f) return false;
            vertex={};vertex.clip[0]=dot(rel,right)*sx;vertex.clip[1]=dot(rel,up)*sy;
            vertex.clip[2]=(100000.f/(100000.f-.01f))*z-(100000.f*.01f/(100000.f-.01f));
            vertex.clip[3]=z;
            std::copy_n(colour,4,vertex.colour);
            vertex.blendMode=2;
            return true;
        };
        for(int i=-10;i<=10;++i) for(int axis=0;axis<2;++axis) {
            const float ordinary[4]={light?.08f:.27f,light?.08f:.27f,light?.08f:.27f,light?.48f:.34f};
            const float red[4]={light?.34f:.42f,.10f,.10f,.58f};
            const float green[4]={.10f,light?.31f:.38f,.12f,.58f};
            const float* colour=i==0?(axis==0?red:green):ordinary;
            Vertex a{},b{};
            const bool okay=axis==0?
                projectGrid(float(i),-10,colour,a)&&projectGrid(float(i),10,colour,b):
                projectGrid(-10,float(i),colour,a)&&projectGrid(10,float(i),colour,b);
            if(okay){vertices.push_back(a);vertices.push_back(b);}
        }
    }
    const UINT gridVertices=UINT(vertices.size());
    struct Batch { ID3D11ShaderResourceView* texture; int mode,op; UINT first,count; float depth; };
    std::vector<Batch> batches;
    constexpr int corners[]={0,1,2,0,2,3}; constexpr float xy[4][2]={{-1,1},{1,1},{1,-1},{-1,-1}};
    for(const auto& item:order) {
        const auto& s=*item.sprite; const auto* t=item.texture; const int m=mode(s.blendMode), op=operation(s.blendOperation);
        ID3D11ShaderResourceView* srv=t?t->view:p.white;
        batches.push_back({srv,m,op,UINT(vertices.size()),0,item.depth});
        float aspect=t?(float(t->height)/t->frames)/float(t->width):1;
        float halfX=s.size[0]*.5f, halfY=s.size[1]*aspect*.5f, cs=std::cos(s.angle), sn=std::sin(s.angle);
        uint32_t frame=t?std::min(uint32_t(std::clamp(s.framePhase,0.f,1.f)*t->frames),t->frames-1):0;
        float uv0=t?(float(frame)*t->height/t->frames+.5f)/t->height:0;
        float uv1=t?(float(frame+1)*t->height/t->frames-.5f)/t->height:1;
        const float u0=t?.5f/t->width:0, u1=t?1-.5f/t->width:1;
        for(int c:corners) {
            float x=xy[c][0]*halfX,y=xy[c][1]*halfY, rx=x*cs-y*sn,ry=x*sn+y*cs;
            float rel[]={s.position[0]-eye[0]+right[0]*rx+up[0]*ry,
                         s.position[2]-eye[1]+right[1]*rx+up[1]*ry,
                         -s.position[1]-eye[2]+right[2]*rx+up[2]*ry};
            float z=dot(rel,forward); Vertex v{};
            v.clip[0]=dot(rel,right)*sx; v.clip[1]=dot(rel,up)*sy;
            v.clip[2]=(100000.f/(100000.f-.01f))*z-(100000.f*.01f/(100000.f-.01f)); v.clip[3]=z;
            v.uv[0]=xy[c][0]<0?u0:u1; v.uv[1]=xy[c][1]>0?uv0:uv1;
            for(int k=0;k<4;++k) v.colour[k]=std::clamp(s.colour[k],0.f,1.f);
            v.blendMode=float(m); vertices.push_back(v);
        }
        batches.back().count+=6;
    }
    if(!vertices.empty()) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if(!p.check(p.context->Map(p.buffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"Map particle vertices")) return nullptr;
        std::memcpy(mapped.pData,vertices.data(),vertices.size()*sizeof(Vertex)); p.context->Unmap(p.buffer,0);
    }
    ID3D11RenderTargetView* previousTarget=nullptr; ID3D11DepthStencilView* previousDepth=nullptr;
    p.context->OMGetRenderTargets(1,&previousTarget,&previousDepth);
    UINT viewportCount=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_VIEWPORT previousViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    p.context->RSGetViewports(&viewportCount,previousViewports);
    ID3D11ShaderResourceView* nullSrv=nullptr; p.context->PSSetShaderResources(0,1,&nullSrv);
    p.context->OMSetRenderTargets(1,&p.target,p.depthView);
    const float clear[]={std::clamp(background[0],0.f,1.f),std::clamp(background[1],0.f,1.f),
                         std::clamp(background[2],0.f,1.f),1.f};
    p.context->ClearRenderTargetView(p.target,clear);
    p.context->ClearDepthStencilView(p.depthView,D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);
    D3D11_VIEWPORT viewport{0,0,float(p.width),float(p.height),0,1}; p.context->RSSetViewports(1,&viewport);
    p.context->RSSetState(p.raster); p.context->OMSetDepthStencilState(p.depth,0);
    p.context->IASetInputLayout(p.layout); p.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride=sizeof(Vertex),offset=0; p.context->IASetVertexBuffers(0,1,&p.buffer,&stride,&offset);
    p.context->VSSetShader(p.vs,nullptr,0); p.context->PSSetShader(p.ps,nullptr,0);
    p.context->GSSetShader(nullptr,nullptr,0); p.context->HSSetShader(nullptr,nullptr,0); p.context->DSSetShader(nullptr,nullptr,0);
    p.context->PSSetSamplers(0,1,&p.sampler); const float factor[]={1,1,1,1};
    if(gridVertices) {
        p.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        p.context->OMSetBlendState(p.blends[2][0],factor,0xffffffff);
        p.context->PSSetShaderResources(0,1,&p.white);
        p.context->Draw(gridVertices,0);
        p.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }
    struct Command { float depth; size_t index; bool mesh; };
    std::vector<Command> commands;
    for(size_t i=0;i<batches.size();++i)commands.push_back({batches[i].depth,i,false});
    size_t acceptedMeshes=0,acceptedTriangles=0;
    for(size_t i=0;i<meshes.size();++i) {
        const auto& m=meshes[i];auto it=p.meshes.find(m.mesh);
        if(it==p.meshes.end()){++p.missingMeshes;continue;}
        bool valid=true;for(float v:m.position)valid=valid&&std::isfinite(v);
        for(float v:m.size)valid=valid&&std::isfinite(v);
        for(float v:m.colour)valid=valid&&std::isfinite(v);
        for(float v:m.orientation)valid=valid&&std::isfinite(v);
        if(!valid || acceptedMeshes>=4096 || acceptedTriangles+it->second.triangles>2000000){++p.droppedMeshes;continue;}
        if(m.colour[3]<=0)continue;
        float rel[]={m.position[0]-eye[0],m.position[2]-eye[1],-m.position[1]-eye[2]};
        commands.push_back({dot(rel,forward),i,true});++acceptedMeshes;acceptedTriangles+=it->second.triangles;
    }
    std::stable_sort(commands.begin(),commands.end(),[](const auto& a,const auto& b){return a.depth>b.depth;});
    for(size_t ci=0;ci<commands.size();++ci) {
        const auto& command=commands[ci];
        if(!command.mesh) {
            const auto& b=batches[command.index];UINT count=b.count;
            while(ci+1<commands.size() && !commands[ci+1].mesh) {
                const auto& next=batches[commands[ci+1].index];
                if(next.texture!=b.texture || next.mode!=b.mode || next.op!=b.op || next.first!=b.first+count)break;
                count+=next.count;++ci;
            }
            p.context->IASetInputLayout(p.layout);p.context->IASetVertexBuffers(0,1,&p.buffer,&stride,&offset);
            p.context->VSSetShader(p.vs,nullptr,0);p.context->PSSetShader(p.ps,nullptr,0);p.context->PSSetSamplers(0,1,&p.sampler);
            p.context->OMSetBlendState(p.blends[b.mode][b.op],factor,0xffffffff);
            p.context->PSSetShaderResources(0,1,&b.texture);p.context->Draw(count,b.first);continue;
        }
        const auto& instance=meshes[command.index];const auto& mesh=p.meshes.at(instance.mesh);
        float basis[3][3];meshBasis(instance,mesh.radius,basis);
        float origin[3]={instance.position[0],instance.position[1],instance.position[2]};
        if(instance.centredOnPosition)for(int j=0;j<3;++j)for(int k=0;k<3;++k)origin[k]-=basis[j][k]*mesh.centre[j];
        MeshConstants constants{};
        constexpr float az=100000.f/(100000.f-.01f),bz=100000.f*.01f/(100000.f-.01f);
        for(int j=0;j<4;++j){float v[3];if(j<3){v[0]=basis[j][0];v[1]=basis[j][2];v[2]=-basis[j][1];}
            else {v[0]=origin[0]-eye[0];v[1]=origin[2]-eye[1];v[2]=-origin[1]-eye[2];}
            constants.clip[0][j]=dot(v,right)*sx;constants.clip[1][j]=dot(v,up)*sy;constants.clip[2][j]=dot(v,forward)*az-(j==3?bz:0);constants.clip[3][j]=dot(v,forward);}
        bool finiteTransform=true;for(const auto& row:constants.clip)for(float value:row)finiteTransform=finiteTransform&&std::isfinite(value);
        if(!finiteTransform){++p.droppedMeshes;continue;}
        for(int k=0;k<4;++k)constants.colour[k]=std::clamp(instance.colour[k],0.f,1.f);
        const int m=mode(instance.blendMode),op=operation(instance.blendOperation);constants.options[0]=float(m);
        UINT meshStride=sizeof(MeshVertex);p.context->IASetInputLayout(p.meshLayout);p.context->IASetVertexBuffers(0,1,&mesh.vertices,&meshStride,&offset);p.context->IASetIndexBuffer(mesh.indices,DXGI_FORMAT_R32_UINT,0);
        p.context->VSSetShader(p.meshVs,nullptr,0);p.context->PSSetShader(p.meshPs,nullptr,0);
        p.context->VSSetConstantBuffers(0,1,&p.meshConstants);p.context->PSSetConstantBuffers(0,1,&p.meshConstants);p.context->PSSetSamplers(0,1,&p.meshSampler);
        p.context->OMSetBlendState(p.blends[m][op],factor,0xffffffff);
        bool drawn=false;
        for(const auto& part:mesh.parts){ID3D11ShaderResourceView* texture=p.white;constants.options[1]=1;constants.options[2]=0;
            if(part.texture>0){auto it=p.textures.find(part.texture);if(it==p.textures.end()){++p.missing;continue;}texture=it->second.view;constants.options[1]=1.f/it->second.frames;constants.options[2]=it->second.frames>1?1.f/it->second.height:0;}
            p.context->UpdateSubresource(p.meshConstants,0,nullptr,&constants,0,0);p.context->PSSetShaderResources(0,1,&texture);p.context->DrawIndexed(part.count,part.first,0);p.meshTriangles+=part.count/3;drawn=true;
        }
        if(drawn)++p.drawnMeshes;
    }
    p.context->PSSetShaderResources(0,1,&nullSrv);
    p.context->OMSetRenderTargets(1,&previousTarget,previousDepth); release(previousTarget); release(previousDepth);
    p.context->RSSetViewports(viewportCount,previousViewports);
    p.drawn=order.size(); return p.view;
}
}
