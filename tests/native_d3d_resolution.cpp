#define WIN32_LEAN_AND_MEAN
#define DIRECTDRAW_VERSION 0x0700
#define DIRECT3D_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <cstdio>
#include "../source/NativeD3DResolution.h"
#include "../source/AltTabRecovery.h"

// Native-Windows integration probe: hidden offscreen surfaces only. Never changes
// the desktop resolution, displays a window, or modifies a DLL file on disk.
bool CheckRendering(IDirect3D3* d3d, IDirect3DDevice3* device,
    IDirectDrawSurface4* surface, DWORD width, DWORD height)
{
    IDirect3DViewport3* viewport = nullptr;
    HRESULT hr = d3d->CreateViewport(&viewport, nullptr);
    if (FAILED(hr)) return false;
    hr = device->AddViewport(viewport);
    D3DVIEWPORT2 view{};
    view.dwSize = sizeof(view);
    view.dwWidth = width;
    view.dwHeight = height;
    view.dvClipX = -1.0f;
    view.dvClipY = 1.0f;
    view.dvClipWidth = 2.0f;
    view.dvClipHeight = 2.0f;
    view.dvMinZ = 0.0f;
    view.dvMaxZ = 1.0f;
    if (SUCCEEDED(hr)) hr = viewport->SetViewport2(&view);
    if (SUCCEEDED(hr)) hr = device->SetCurrentViewport(viewport);
    D3DRECT rectangle{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    if (SUCCEEDED(hr)) hr = viewport->Clear2(1, &rectangle, D3DCLEAR_TARGET, 0xFF112233, 1.0f, 0);
    if (SUCCEEDED(hr)) hr = device->SetRenderState(D3DRENDERSTATE_ZENABLE, FALSE);
    if (SUCCEEDED(hr)) hr = device->SetRenderState(D3DRENDERSTATE_CULLMODE, D3DCULL_NONE);
    if (SUCCEEDED(hr)) hr = device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
    const float right = static_cast<float>(width), bottom = static_cast<float>(height);
    D3DTLVERTEX vertices[3]{};
    vertices[0].sx = right - 64.0f;
    vertices[0].sy = bottom - 64.0f;
    vertices[1].sx = right - 1.0f;
    vertices[1].sy = bottom - 64.0f;
    vertices[2].sx = right - 64.0f;
    vertices[2].sy = bottom - 1.0f;
    for (auto& vertex : vertices)
    {
        vertex.sz = 0.5f;
        vertex.rhw = 1.0f;
        vertex.color = 0xFFFFFF00;
    }
    if (SUCCEEDED(hr))
    {
        hr = device->BeginScene();
        if (SUCCEEDED(hr))
        {
            hr = device->DrawPrimitive(D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, vertices, 3, 0);
            const HRESULT end = device->EndScene();
            if (SUCCEEDED(hr)) hr = end;
        }
    }
    bool matches = false;
    if (SUCCEEDED(hr))
    {
        DDSURFACEDESC2 locked{};
        locked.dwSize = sizeof(locked);
        hr = surface->Lock(nullptr, &locked, DDLOCK_READONLY | DDLOCK_WAIT, nullptr);
        if (SUCCEEDED(hr))
        {
            DWORD pixel = 0;
            const auto bytes = static_cast<uint8_t*>(locked.lpSurface);
            const DWORD pixelBytes = locked.ddpfPixelFormat.dwRGBBitCount / 8;
            if (pixelBytes > 0 && pixelBytes <= sizeof(pixel))
            {
                std::memcpy(&pixel, bytes + (height - 48) * locked.lPitch + (width - 48) * pixelBytes, pixelBytes);
                const DWORD masks = locked.ddpfPixelFormat.dwRBitMask | locked.ddpfPixelFormat.dwGBitMask |
                    locked.ddpfPixelFormat.dwBBitMask;
                const DWORD yellow = locked.ddpfPixelFormat.dwRBitMask | locked.ddpfPixelFormat.dwGBitMask;
                matches = (pixel & masks) == yellow;
            }
            surface->Unlock(nullptr);
        }
    }
    std::printf(", render=%08lX pixel verified=%d", static_cast<unsigned long>(hr), matches);
    device->DeleteViewport(viewport);
    viewport->Release();
    return matches;
}

bool CheckSize(IDirectDraw4* dd, IDirect3D3* d3d, DWORD width, DWORD height,
    bool patched, bool expectDevice)
{
    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = width;
    desc.dwHeight = height;
    desc.ddsCaps.dwCaps = DDSCAPS_3DDEVICE | DDSCAPS_OFFSCREENPLAIN;
    IDirectDrawSurface4* surface = nullptr;
    HRESULT hr = dd->CreateSurface(&desc, &surface, nullptr);
    std::printf("%s %lux%lu: surface=%08lX", patched ? "Patched" : "Baseline", width, height,
        static_cast<unsigned long>(hr));
    bool passed = false;
    if (SUCCEEDED(hr))
    {
        IDirect3DDevice3* device = nullptr;
        hr = d3d->CreateDevice(IID_IDirect3DHALDevice, surface, &device, nullptr);
        std::printf(", device=%08lX", static_cast<unsigned long>(hr));
        passed = expectDevice ? SUCCEEDED(hr) && device != nullptr : hr == DDERR_INVALIDOBJECT;
        if (device != nullptr)
        {
            hr = device->SetRenderTarget(surface, 0);
            passed = passed && SUCCEEDED(hr);
            std::printf(", target=%08lX", static_cast<unsigned long>(hr));
            // Verify actual COM method ownership and a focused recovery/rebind
            // with healthy offscreen surfaces. This does not simulate OS surface
            // loss or Alt-Tab the user's desktop; those remain in-game checks.
            std::array<uint8_t, 0x50> context{};
            const auto store = [&](size_t offset, const auto& value)
            {
                std::memcpy(context.data() + offset, &value, sizeof(value));
            };
            store(0x30, surface);
            store(0x34, surface);
            store(0x38, surface);
            store(0x40, device);
            store(0x48, dd);
            passed = passed && AltTabRecovery::UsesNativeModules(context.data(), GetModuleHandleA("d3dim.dll"), GetModuleHandleA("ddraw.dll"));
            AltTabRecovery::State recovery;
            const auto begin = [&]() { return device->BeginScene(); };
            hr = recovery.Begin(context.data(), {}, true, begin);
            passed = passed && SUCCEEDED(hr);
            if (SUCCEEDED(hr))
            {
                const auto end = device->EndScene();
                passed = passed && SUCCEEDED(end);
            }
            passed = passed && recovery.Begin(context.data(), {}, false, begin) == DDERR_NOEXCLUSIVEMODE;
            hr = recovery.Begin(context.data(), {}, true, begin);
            passed = passed && SUCCEEDED(hr) && recovery.lastResult.attempted && recovery.lastResult.surfaces == 0;
            if (SUCCEEDED(hr))
            {
                const auto end = device->EndScene();
                passed = passed && SUCCEEDED(end);
            }
            std::printf(", recovery=%08lX", static_cast<unsigned long>(hr));
            const bool rendered = CheckRendering(d3d, device, surface, width, height);
            passed = passed && rendered;
            device->Release();
        }
        surface->Release();
    }
    std::printf(" %s\n", passed ? "PASS" : "FAIL");
    return passed;
}

int main()
{
    IDirectDraw* dd = nullptr;
    HRESULT hr = DirectDrawCreate(nullptr, &dd, nullptr);
    if (FAILED(hr)) return 1;
    IDirectDraw4* dd4 = nullptr;
    hr = dd->QueryInterface(IID_IDirectDraw4, reinterpret_cast<void**>(&dd4));
    dd->Release();
    if (FAILED(hr)) return 1;
    IDirect3D3* d3d = nullptr;
    hr = dd4->QueryInterface(IID_IDirect3D3, reinterpret_cast<void**>(&d3d));
    if (FAILED(hr)) { dd4->Release(); return 1; }
    const HMODULE native = GetModuleHandleA("d3dim.dll");
    if (native == nullptr)
    {
        std::puts("SKIP: native D3DIM not loaded (a graphics wrapper may be active).");
        d3d->Release();
        dd4->Release();
        return 0;
    }
    const HWND window = CreateWindowExA(0, "STATIC", "Toy2 offscreen resolution probe", 0,
        0, 0, 16, 16, nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
    hr = dd4->SetCooperativeLevel(window, DDSCL_NORMAL);
    unsigned failures = (window == nullptr || FAILED(hr)) ? 1 : 0;
    const DWORD sizes[][2] = {{640,480}, {1920,1080}, {1920,1440}, {2048,1440}, {2049,1440},
        {1920,2048}, {1920,2049}, {2560,1440}, {1920,2160}, {3840,2160}, {4096,2160},
        {5120,2880}, {2880,5120}, {7680,4320}, {4320,7680},
        {NativeD3DResolution::RaisedLimit,2160}, {2160,NativeD3DResolution::RaisedLimit}};
    if (failures == 0)
    {
        for (const auto& size : sizes)
            failures += !CheckSize(dd4, d3d, size[0], size[1], false, size[0] <= 2048 && size[1] <= 2048);
        const auto patch = NativeD3DResolution::RaiseLimit(native);
        std::printf("Patch status=%d, create matches=%zu, target matches=%zu\n",
            static_cast<int>(patch.status), patch.createMatches, patch.targetMatches);
        if (patch.status != NativeD3DResolution::Status::Applied)
            ++failures;
        else
        {
            for (const auto& size : sizes)
                failures += !CheckSize(dd4, d3d, size[0], size[1], true, true);
            failures += !CheckSize(dd4, d3d, NativeD3DResolution::RaisedLimit + 1, 480, true, false);
            failures += !CheckSize(dd4, d3d, 640, NativeD3DResolution::RaisedLimit + 1, true, false);
            failures += NativeD3DResolution::RaiseLimit(native).status != NativeD3DResolution::Status::AlreadyApplied;
        }
    }
    DestroyWindow(window);
    d3d->Release();
    dd4->Release();
    std::printf("Native resolution integration tests: %u failures.\n", failures);
    return failures != 0;
}
