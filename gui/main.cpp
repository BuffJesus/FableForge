// FableForge GUI — Win32 + Direct3D 11 host for Dear ImGui. Structure adapted
// from the upstream Dear ImGui example_win32_directx11 (MIT). The application UI
// lives in albion::gui::App; this file owns the window, swapchain, message loop
// and the automation hooks (--auto <script>, screenshots of the backbuffer).
//
//   FableForge [--install <fable-root>] [--auto <script.txt>] [--size WxH]

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <d3d11.h>
#include <shellapi.h>
#include <windows.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include "app.hpp"
#include "terrainexport.hpp"

static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_context = nullptr;
static IDXGISwapChain* g_swapChain = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static UINT g_resizeW = 0, g_resizeH = 0;
static bool g_occluded = false;
static albion::gui::App* g_app = nullptr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static void createRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) { g_device->CreateRenderTargetView(back, nullptr, &g_rtv); back->Release(); }
}

static void cleanupRenderTarget() { if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; } }

static bool createDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 3,
                                               D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &got, &g_context);
    if (FAILED(hr))  // no usable GPU: software rasterizer keeps the app alive on anything
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 3,
                                           D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &got, &g_context);
    if (FAILED(hr)) return false;
    createRenderTarget();
    return true;
}

static void cleanupDevice() {
    cleanupRenderTarget();
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

// Copy the backbuffer to a staging texture and write it as PNG.
static bool screenshot(const std::string& path) {
    ID3D11Texture2D* back = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (!back) return false;
    D3D11_TEXTURE2D_DESC d; back->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(g_device->CreateTexture2D(&d, nullptr, &staging))) { back->Release(); return false; }
    g_context->CopyResource(staging, back);
    D3D11_MAPPED_SUBRESOURCE m;
    bool ok = false;
    if (SUCCEEDED(g_context->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
        albion::terrainexport::Image img;
        img.width = d.Width; img.height = d.Height; img.rgba.resize(size_t(d.Width) * d.Height * 4);
        for (UINT y = 0; y < d.Height; ++y) {
            const uint8_t* src = static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch;
            uint8_t* dst = &img.rgba[size_t(y) * d.Width * 4];
            for (UINT x = 0; x < d.Width; ++x) {
                dst[x * 4 + 0] = src[x * 4 + 0]; dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4 + 2]; dst[x * 4 + 3] = 255;
            }
        }
        g_context->Unmap(staging, 0);
        try {
            const auto png = albion::terrainexport::encodePng(img);
            std::ofstream f(path, std::ios::binary);
            f.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
            ok = bool(f);
        } catch (...) {}
    }
    staging->Release(); back->Release();
    return ok;
}

static std::string narrow(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

static bool g_automated = false;   // --auto: the script owns the mouse, the real one never reaches ImGui

static bool isMouseMessage(UINT msg) {
    return (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_MOUSELEAVE || msg == WM_NCMOUSEMOVE ||
           msg == WM_NCMOUSELEAVE || msg == WM_SETCURSOR;
}

static LRESULT WINAPI wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // A scripted run used to race the user's real cursor: the backend queued WM_MOUSEMOVE
    // between the script's move/press/release frames and ImGui trickled the press onto a
    // frame hovering the wrong widget (the "ui paths" textures-toggle flake, and the
    // "suites fail while Fable.exe is running" note). Real mouse input is dropped instead.
    if (g_automated && isMouseMessage(msg)) return msg == WM_SETCURSOR ? DefWindowProcW(hwnd, msg, wp, lp) : 0;
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    switch (msg) {
        case WM_SIZE:
            if (wp == SIZE_MINIMIZED) return 0;
            g_resizeW = LOWORD(lp); g_resizeH = HIWORD(lp);
            return 0;
        case WM_SYSCOMMAND:
            if ((wp & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wp);
            wchar_t path[MAX_PATH];
            if (g_app && DragQueryFileW(drop, 0, path, MAX_PATH)) g_app->openDropped(narrow(path));
            DragFinish(drop);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    std::string installOverride, autoScript;
    int width = 1440, height = 900;
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i < argc; ++i) {
            const std::string a = narrow(argv[i]);
            auto next = [&]() { return i + 1 < argc ? narrow(argv[++i]) : std::string(); };
            if (a == "--install") installOverride = next();
            else if (a == "--auto") autoScript = next();
            else if (a == "--size") { const std::string v = next(); std::sscanf(v.c_str(), "%dx%d", &width, &height); }
        }
        LocalFree(argv);
    }

    ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, wndProc, 0L, 0L, hInstance, nullptr, nullptr, nullptr, nullptr, L"FableForge", nullptr};
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"FableForge", WS_OVERLAPPEDWINDOW, 80, 60, width, height,
                              nullptr, nullptr, wc.hInstance, nullptr);
    if (!createDevice(hwnd)) {
        cleanupDevice();
        MessageBoxW(nullptr, L"Direct3D 11 is not available on this machine.", L"FableForge", MB_OK);
        return 1;
    }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    albion::gui::App app;
    g_app = &app;
    app.setDpiScale(ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd));
    if (!autoScript.empty()) {
        std::ofstream(autoScript + ".log", std::ios::trunc);
        app.automation().load(autoScript);   // before init: disables settings persistence
    }
    app.init(g_device, g_context, hwnd, installOverride);
    DragAcceptFiles(hwnd, TRUE);
    const bool automated = app.automation().active();
    g_automated = automated;

    auto last = std::chrono::steady_clock::now();
    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;
        if (!automated && g_occluded && g_swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            Sleep(10);
            continue;
        }
        g_occluded = false;
        if (g_resizeW && g_resizeH) {
            cleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = g_resizeH = 0;
            createRenderTarget();
        }
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - last).count();
        last = now;

        if (app.fontsDirty()) {   // window height or DPI moved the UI scale: rebuild the atlas
            app.rebuildFonts();
            ImGui_ImplDX11_InvalidateDeviceObjects();
            ImGui_ImplDX11_CreateDeviceObjects();
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        if (automated) {   // scripted mouse wins over the backend's real-cursor fallback
            float vx, vy;
            if (app.automation().virtualMouse(vx, vy)) io.AddMousePosEvent(vx, vy);
        }
        ImGui::NewFrame();
        app.frame(dt);
        if (automated && !app.automation().tick(app)) running = false;
        ImGui::Render();

        const float clear[4] = {0.059f, 0.055f, 0.078f, 1.0f};
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        std::string shot;
        if (automated && app.automation().takeScreenshot(shot)) {
            if (!screenshot(shot)) app.automation().fail("screenshot failed: " + shot);
            else app.automation().note("shot " + shot);
        }
        const HRESULT hr = g_swapChain->Present(automated ? 0 : 1, 0);
        if (automated) Sleep(4);   // ~200 fps cap: keeps the script deterministic-ish without hogging a core
        g_occluded = hr == DXGI_STATUS_OCCLUDED;
        if (app.wantsQuit()) running = false;
    }

    g_app = nullptr;
    if (!automated) app.saveSettings();
    const int code = automated ? app.automation().exitCode() : 0;
    if (automated) {
        std::ofstream log(autoScript + ".log", std::ios::app);
        log << (code == 0 ? "RESULT PASS\n" : "RESULT FAIL\n");
        for (const auto& f : app.automation().failures()) log << "  " << f << "\n";
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanupDevice();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return code;
}
