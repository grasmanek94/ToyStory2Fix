#pragma once

#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <array>
#include <cstdint>
#include <cstring>

namespace AltTabRecovery
{
    // Recovered Direct3D6 display/texture layouts, not the separate legacy renderer.
    template <typename T> inline T Read(const uint8_t* bytes, size_t offset)
    {
        T value;
        std::memcpy(&value, bytes + offset, sizeof(value));
        return value;
    }

    inline bool UsesNativeModules(const uint8_t* context, HMODULE nativeD3D, HMODULE nativeDraw)
    {
        if (context == nullptr || nativeD3D == nullptr || nativeDraw == nullptr)
            return false;
        const auto device = Read<uint8_t*>(context, 0x40);
        const auto draw = Read<uint8_t*>(context, 0x48);
        if (device == nullptr || draw == nullptr)
            return false;
        const auto deviceMethods = Read<void**>(device, 0);
        const auto drawMethods = Read<void**>(draw, 0);
        MEMORY_BASIC_INFORMATION deviceCode{}, drawCode{};
        // A wrapper can load d3dim.dll without exposing native COM objects. Match
        // both method owners instead of taking a loaded DLL as proof of the backend.
        return deviceMethods != nullptr && drawMethods != nullptr &&
            VirtualQuery(deviceMethods[9], &deviceCode, sizeof(deviceCode)) == sizeof(deviceCode) &&
            VirtualQuery(drawMethods[26], &drawCode, sizeof(drawCode)) == sizeof(drawCode) &&
            deviceCode.AllocationBase == nativeD3D && drawCode.AllocationBase == nativeDraw;
    }

    enum class Stage { None, CooperativeLevel, ExclusiveOwnership, DisplayMode, SurfaceQuery, SurfaceRestore, TextureReload, RenderTarget, TextureBinding, BeginScene };
    inline const char* StageName(Stage stage)
    {
        switch (stage)
        {
        case Stage::None: return "complete";
        case Stage::CooperativeLevel: return "cooperative-level";
        case Stage::ExclusiveOwnership: return "exclusive-ownership";
        case Stage::DisplayMode: return "display-mode";
        case Stage::SurfaceQuery: return "surface-query";
        case Stage::SurfaceRestore: return "surface-restore";
        case Stage::TextureReload: return "texture-reload";
        case Stage::RenderTarget: return "render-target";
        case Stage::TextureBinding: return "texture-binding";
        case Stage::BeginScene: return "BeginScene";
        default: return "unknown";
        }
    }
    struct Result
    {
        HRESULT error = DD_OK;
        Stage stage = Stage::None;
        unsigned surfaces = 0;
        unsigned textures = 0;
        bool modeRestored = false;
        bool exclusiveReacquired = false;
        bool attempted = false;
    };
    inline DWORD NativeCooperativeFlags(bool fullscreen, uint32_t initializationFlags)
    {
        // InitializeNativeDirectDraw (004AEEE0): preserve its exact FPU policy.
        const DWORD flags = fullscreen ? DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN | DDSCL_ALLOWREBOOT : DDSCL_NORMAL;
        return (initializationFlags & 0x10) == 0 ? flags | DDSCL_FPUSETUP : flags;
    }
    struct CooperativeSettings
    {
        IDirectDraw4* draw = nullptr;
        HWND window = nullptr;
        DWORD flags = 0;
    };
    struct Bindings
    {
        uint8_t** textureHead = nullptr;
        int (__cdecl* recreateTexture)(uint8_t*) = nullptr;
        int32_t* cachedTextureHandle = nullptr;
        const CooperativeSettings* cooperativeSettings = nullptr;
    };

    struct State
    {
        const uint8_t* context = nullptr;
        const void* drawIdentity = nullptr;
        const void* deviceIdentity = nullptr;
        bool pending = false;
        bool needsRebind = false;
        bool needsTextureBindingReset = false;
        bool needsModeRestore = false;
        bool busy = false;
        DDSURFACEDESC2 selectedMode{};
        bool hasMode = false;
        Result lastResult{};

        void ObserveContext(const uint8_t* current)
        {
            const auto draw = current != nullptr ? Read<void*>(current, 0x48) : nullptr;
            const auto device = current != nullptr ? Read<void*>(current, 0x40) : nullptr;
            if (context != current || drawIdentity != draw || deviceIdentity != device)
            {
                *this = {};
                context = current;
                drawIdentity = draw;
                deviceIdentity = device;
            }
        }

        void ObservePresent(HRESULT result)
        {
            if (result == DDERR_SURFACELOST || result == DDERR_WRONGMODE || result == DDERR_NOEXCLUSIVEMODE)
                pending = true;
            if (result == DDERR_WRONGMODE)
                needsModeRestore = true;
        }

        Result Recover(uint8_t* display, const Bindings& bindings)
        {
            Result result;
            result.attempted = true;
            const auto fail = [&](HRESULT error, Stage stage)
            {
                result.error = error;
                result.stage = stage;
                if (error == DDERR_WRONGMODE)
                    needsModeRestore = true;
                return result;
            };
            const auto draw = Read<IDirectDraw4*>(display, 0x48);
            const auto device = Read<IDirect3DDevice3*>(display, 0x40);
            if (draw == nullptr || device == nullptr)
                return fail(DDERR_NOTINITIALIZED, Stage::CooperativeLevel);
            auto cooperative = draw->TestCooperativeLevel();
            const auto settings = bindings.cooperativeSettings;
            if (cooperative == DDERR_NOEXCLUSIVEMODE && Read<uint32_t>(display, 4) != 0 &&
                settings != nullptr && settings->draw == draw && settings->window == Read<HWND>(display, 0) &&
                settings->window != nullptr && (settings->flags & (DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN | DDSCL_NORMAL)) ==
                    (DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN))
            {
                // Focus has returned, but DirectDraw may never automatically regain
                // ownership. Replay only this context's successful initial settings,
                // including its original FPU flags; do not recreate the game/device.
                const auto acquired = draw->SetCooperativeLevel(settings->window, settings->flags);
                if (FAILED(acquired))
                    return fail(acquired, Stage::ExclusiveOwnership);
                cooperative = draw->TestCooperativeLevel();
                if (cooperative != DD_OK && cooperative != DDERR_WRONGMODE)
                    return fail(cooperative, Stage::ExclusiveOwnership);
                result.exclusiveReacquired = true;
                // Reclaiming ownership may leave the desktop's mode active.
                if (hasMode)
                {
                    DDSURFACEDESC2 current{};
                    current.dwSize = sizeof(current);
                    const auto queried = draw->GetDisplayMode(&current);
                    needsModeRestore |= FAILED(queried) || current.dwWidth != selectedMode.dwWidth ||
                        current.dwHeight != selectedMode.dwHeight ||
                        current.ddpfPixelFormat.dwRGBBitCount != selectedMode.ddpfPixelFormat.dwRGBBitCount ||
                        current.dwRefreshRate != selectedMode.dwRefreshRate;
                }
            }
            if ((cooperative == DDERR_WRONGMODE || needsModeRestore) && hasMode &&
                Read<uint32_t>(display, 4) != 0 && (cooperative == DD_OK || cooperative == DDERR_WRONGMODE))
            {
                // Reclaim ONLY the game's existing exclusive DirectDraw mode. This
                // is not the paused desktop-before-launch/DSR clipping workaround.
                const auto mode = draw->SetDisplayMode(selectedMode.dwWidth, selectedMode.dwHeight,
                    selectedMode.ddpfPixelFormat.dwRGBBitCount, selectedMode.dwRefreshRate, 0);
                if (FAILED(mode))
                    return fail(mode, Stage::DisplayMode);
                result.modeRestored = true;
                needsModeRestore = false;
                needsRebind = true;
                cooperative = draw->TestCooperativeLevel();
            }
            if (FAILED(cooperative))
                return fail(cooperative, Stage::CooperativeLevel);
            // The native presentation fallback may already have restored aliased
            // primary/back surfaces; still refresh the device's target binding.
            needsRebind = true;

            // Restore the primary chain first: its implicit back buffer cannot be
            // restored independently. Re-query aliases/attachments after each restore.
            std::array<IDirectDrawSurface4*, 4> surfaces{};
            const size_t offsets[] = { 0x30, 0x34, 0x38, 0x3C };
            for (size_t index = 0; index < surfaces.size(); ++index)
            {
                const auto surface = Read<IDirectDrawSurface4*>(display, offsets[index]);
                surfaces[index] = surface;
                bool duplicate = false;
                for (size_t previous = 0; previous < index; ++previous)
                    duplicate |= surfaces[previous] == surface;
                if (surface == nullptr || duplicate)
                    continue;
                const auto lost = surface->IsLost();
                if (lost == DD_OK)
                    continue;
                if (lost != DDERR_SURFACELOST)
                    return fail(lost, Stage::SurfaceQuery);
                needsRebind = true;
                const auto restored = surface->Restore();
                if (FAILED(restored))
                    return fail(restored, Stage::SurfaceRestore);
                ++result.surfaces;
                const auto ready = surface->IsLost();
                if (ready != DD_OK)
                    return fail(ready, Stage::SurfaceRestore);
            }

            // Do not RestoreAllSurfaces: restoring a texture clears its pixels.
            // Recreate lost owned textures from the game's retained CPU bitmap,
            // preserving each 0x110-byte record, handles, list links and reference counts.
            auto texture = bindings.textureHead != nullptr ? *bindings.textureHead : nullptr;
            size_t visited = 0;
            for (; texture != nullptr; texture = Read<uint8_t*>(texture, 0x108))
            {
                if (++visited > 4096)
                    return fail(DDERR_INVALIDOBJECT, Stage::TextureReload); // Corrupt/cyclic list guard.
                if ((Read<uint32_t>(texture, 0x100) & 0x40) != 0)
                    continue; // Borrowed framebuffer alias, restored with the display.
                auto surface = Read<IDirectDrawSurface4*>(texture, 0);
                const auto lost = surface != nullptr ? surface->IsLost() : DDERR_SURFACELOST;
                if (lost == DD_OK)
                    continue;
                if (lost != DDERR_SURFACELOST)
                    return fail(lost, Stage::SurfaceQuery);
                if (Read<void*>(texture, 0x84) != nullptr)
                {
                    if (bindings.recreateTexture == nullptr)
                        return fail(DDERR_NOTINITIALIZED, Stage::TextureReload);
                    needsTextureBindingReset = true;
                    if (bindings.recreateTexture(texture) == 0)
                        return fail(DDERR_SURFACELOST, Stage::TextureReload);
                    surface = Read<IDirectDrawSurface4*>(texture, 0);
                    if (surface == nullptr || surface->IsLost() != DD_OK)
                        return fail(DDERR_SURFACELOST, Stage::TextureReload);
                }
                else if (surface != nullptr)
                {
                    // Dynamic textures without a retained bitmap are filled by their
                    // native producer (movies/framebuffer copies), not guessed here.
                    const auto restored = surface->Restore();
                    if (FAILED(restored))
                        return fail(restored, Stage::TextureReload);
                    const auto ready = surface->IsLost();
                    if (ready != DD_OK)
                        return fail(ready, Stage::TextureReload);
                }
                else
                {
                    continue; // Uninitialized native record without pixel data.
                }
                ++result.textures;
            }
            if (needsRebind)
            {
                const auto target = Read<IDirectDrawSurface4*>(display, 0x38);
                if (target == nullptr)
                    return fail(DDERR_NOTINITIALIZED, Stage::RenderTarget);
                const auto rebound = device->SetRenderTarget(target, 0);
                if (FAILED(rebound))
                    return fail(rebound, Stage::RenderTarget);
                needsRebind = false;
            }
            if (needsTextureBindingReset)
            {
                const auto unbound = device->SetTexture(0, nullptr);
                if (FAILED(unbound))
                    return fail(unbound, Stage::TextureBinding);
                if (bindings.cachedTextureHandle != nullptr)
                    *bindings.cachedTextureHandle = -1;
                needsTextureBindingReset = false;
            }
            return result;
        }

        template <typename BeginScene>
        HRESULT Begin(uint8_t* display, const Bindings& bindings, bool focused, BeginScene begin)
        {
            ObserveContext(display);
            lastResult = {};
            if (display == nullptr || busy)
                return begin();
            if (!focused)
            {
                pending = true;
                return DDERR_NOEXCLUSIVEMODE; // Never restore/change modes while another app owns focus.
            }
            busy = true;
            struct ClearBusy { bool& value; ~ClearBusy() { value = false; } } clear{ busy };
            const auto draw = Read<IDirectDraw4*>(display, 0x48);
            if (!hasMode && draw != nullptr && !pending)
            {
                selectedMode.dwSize = sizeof(selectedMode);
                hasMode = SUCCEEDED(draw->GetDisplayMode(&selectedMode)) && selectedMode.dwWidth != 0 &&
                    selectedMode.dwHeight != 0 && selectedMode.ddpfPixelFormat.dwRGBBitCount != 0;
            }
            if (pending)
            {
                lastResult = Recover(display, bindings);
                if (FAILED(lastResult.error))
                    return lastResult.error; // Retry next focused frame, without an unbounded loop.
                pending = false;
            }
            const auto started = begin();
            if (lastResult.attempted && FAILED(started))
            {
                lastResult.error = started;
                lastResult.stage = Stage::BeginScene;
            }
            if (started != DDERR_SURFACELOST && started != DDERR_WRONGMODE && started != DDERR_NOEXCLUSIVEMODE)
                return started;
            ObservePresent(started);
            if (lastResult.attempted)
                return started; // At most one recovery attempt / two BeginScene calls per frame.
            lastResult = Recover(display, bindings);
            if (FAILED(lastResult.error))
                return lastResult.error;
            pending = false;
            const auto retried = begin();
            ObservePresent(retried);
            if (FAILED(retried))
            {
                lastResult.error = retried;
                lastResult.stage = Stage::BeginScene;
            }
            return retried;
        }
    };
}
