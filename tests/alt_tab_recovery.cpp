#define WIN32_LEAN_AND_MEAN
#include "../source/AltTabRecovery.h"
#include <cassert>
#include <cstdio>

namespace
{
    struct Surface
    {
        void** methods;
        HRESULT lost = DD_OK, restoreResult = DD_OK;
        unsigned restores = 0;
        Surface* attachment = nullptr;
    };
    struct Draw
    {
        void** methods;
        HRESULT cooperative = DD_OK, modeResult = DD_OK;
        unsigned checks = 0, modeQueries = 0, modeChanges = 0;
        HRESULT acquireResult = DD_OK;
        unsigned acquisitions = 0;
        DWORD expectedFlags = 0x813;
        bool regainOwnership = true;
        DWORD modeWidth = 2560, modeHeight = 1440, modeRefresh = 165;
    };
    struct Device
    {
        void** methods;
        HRESULT targetResult = DD_OK, bindingResult = DD_OK;
        unsigned targets = 0, unbinds = 0;
        IDirectDrawSurface4* expectedTarget = nullptr;
    };
    HRESULT STDMETHODCALLTYPE IsLost(Surface* surface) { return surface->lost; }
    HRESULT STDMETHODCALLTYPE Restore(Surface* surface)
    {
        ++surface->restores;
        if (SUCCEEDED(surface->restoreResult))
        {
            surface->lost = DD_OK;
            if (surface->attachment != nullptr)
                surface->attachment->lost = DD_OK;
        }
        return surface->restoreResult;
    }
    HRESULT STDMETHODCALLTYPE Cooperative(Draw* draw) { ++draw->checks; return draw->cooperative; }
    HRESULT STDMETHODCALLTYPE GetMode(Draw* draw, DDSURFACEDESC2* mode)
    {
        ++draw->modeQueries;
        assert(mode->dwSize == sizeof(*mode));
        mode->dwWidth = draw->modeWidth;
        mode->dwHeight = draw->modeHeight;
        mode->dwRefreshRate = draw->modeRefresh;
        mode->ddpfPixelFormat.dwRGBBitCount = 32;
        return DD_OK;
    }
    HRESULT STDMETHODCALLTYPE SetMode(Draw* draw, DWORD width, DWORD height, DWORD bpp, DWORD refresh, DWORD flags)
    {
        ++draw->modeChanges;
        assert(width == 2560 && height == 1440 && bpp == 32 && refresh == 165 && flags == 0);
        if (SUCCEEDED(draw->modeResult))
        {
            draw->cooperative = DD_OK;
            draw->modeWidth = width;
            draw->modeHeight = height;
            draw->modeRefresh = refresh;
        }
        return draw->modeResult;
    }
    HRESULT STDMETHODCALLTYPE SetCooperative(Draw* draw, HWND window, DWORD flags)
    {
        ++draw->acquisitions;
        assert(window == reinterpret_cast<HWND>(1) && flags == draw->expectedFlags);
        if (SUCCEEDED(draw->acquireResult) && draw->regainOwnership)
            draw->cooperative = DD_OK;
        return draw->acquireResult;
    }
    HRESULT STDMETHODCALLTYPE SetTarget(Device* device, IDirectDrawSurface4* target, DWORD flags)
    {
        assert(target == device->expectedTarget && flags == 0);
        ++device->targets;
        return device->targetResult;
    }
    HRESULT STDMETHODCALLTYPE SetTexture(Device* device, DWORD stage, IDirect3DTexture2* texture)
    {
        assert(stage == 0 && texture == nullptr);
        ++device->unbinds;
        return device->bindingResult;
    }
    using Record = std::array<uint8_t, 0x110>;
    template <typename T, size_t Bytes> void Write(std::array<uint8_t, Bytes>& bytes, size_t offset, T value)
    {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }
    unsigned recreated = 0;
    bool recreateFails = false;
    int __cdecl Recreate(uint8_t* record)
    {
        ++recreated;
        if (recreateFails)
            return 0;
        auto surface = AltTabRecovery::Read<Surface*>(record, 0);
        surface->lost = DD_OK; // Mock the native bitmap upload, preserving the record.
        return 1;
    }
}

int main()
{
    static_assert(sizeof(void*) == 4, "Native layout tests require x86.");
    std::array<void*, 28> surfaceMethods{};
    surfaceMethods[24] = reinterpret_cast<void*>(&IsLost);
    surfaceMethods[27] = reinterpret_cast<void*>(&Restore);
    std::array<void*, 30> drawMethods{};
    drawMethods[12] = reinterpret_cast<void*>(&GetMode);
    drawMethods[21] = reinterpret_cast<void*>(&SetMode);
    drawMethods[20] = reinterpret_cast<void*>(&SetCooperative);
    drawMethods[26] = reinterpret_cast<void*>(&Cooperative);
    std::array<void*, 42> deviceMethods{};
    deviceMethods[14] = reinterpret_cast<void*>(&SetTarget);
    deviceMethods[38] = reinterpret_cast<void*>(&SetTexture);
    Surface primary{surfaceMethods.data()}, back{surfaceMethods.data()}, target{surfaceMethods.data()}, depth{surfaceMethods.data()};
    Surface textureSurface{surfaceMethods.data()}, borrowedSurface{surfaceMethods.data()};
    primary.attachment = &back;
    Draw draw{drawMethods.data()};
    Device device{deviceMethods.data()};
    device.expectedTarget = reinterpret_cast<IDirectDrawSurface4*>(&target);
    std::array<uint8_t, 0x50> context{};
    Write(context, 0, reinterpret_cast<HWND>(1));
    Write(context, 4, uint32_t{1});
    Write(context, 0x30, &primary);
    Write(context, 0x34, &back);
    Write(context, 0x38, &target);
    Write(context, 0x3C, &depth);
    Write(context, 0x40, &device);
    Write(context, 0x48, &draw);
    Record owned{}, borrowed{};
    Write(owned, 0, &textureSurface);
    Write(owned, 0x84, reinterpret_cast<void*>(0x1234));
    Write(owned, 0x108, borrowed.data());
    Write(owned, 0x104, uint32_t{3}); // Reference count must never change.
    Write(borrowed, 0, &borrowedSurface);
    Write(borrowed, 0x84, reinterpret_cast<void*>(0x5678));
    Write(borrowed, 0x100, uint32_t{0x40});
    auto head = owned.data();
    int32_t cachedHandle = 77;
    AltTabRecovery::Bindings bindings{ &head, Recreate, &cachedHandle };
    AltTabRecovery::State state;
    unsigned begins = 0;
    HRESULT beginResult = DD_OK;
    const auto begin = [&]() { ++begins; return beginResult; };
    const auto savedContext = context;
    const auto savedOwned = owned;
    const auto savedBorrowed = borrowed;

    assert(state.Begin(context.data(), bindings, true, begin) == DD_OK);
    assert(begins == 1 && draw.modeQueries == 1 && draw.checks == 0 && device.targets == 0);
    assert(state.Begin(context.data(), bindings, true, begin) == DD_OK && draw.modeQueries == 1);
    assert(state.Begin(context.data(), bindings, false, begin) == DDERR_NOEXCLUSIVEMODE);
    assert(state.Begin(context.data(), bindings, false, begin) == DDERR_NOEXCLUSIVEMODE);
    assert(begins == 2 && draw.checks == 0 && draw.modeChanges == 0 && primary.restores == 0);

    primary.lost = back.lost = target.lost = depth.lost = textureSurface.lost = borrowedSurface.lost = DDERR_SURFACELOST;
    assert(state.Begin(context.data(), bindings, true, begin) == DD_OK);
    assert(primary.restores == 1 && back.restores == 0 && target.restores == 1 && depth.restores == 1);
    assert(recreated == 1 && textureSurface.lost == DD_OK && borrowedSurface.restores == 0);
    assert(device.targets == 1 && device.unbinds == 1 && cachedHandle == -1);
    assert(state.lastResult.surfaces == 3 && state.lastResult.textures == 1 && !state.pending);
    assert(context == savedContext && owned == savedOwned && borrowed == savedBorrowed);

    // Presentation failure requests pre-BeginScene recovery even after the old
    // native fallback has already restored the primary/back chain.
    state.ObservePresent(DDERR_SURFACELOST);
    textureSurface.lost = DDERR_SURFACELOST;
    const auto previousBegins = begins;
    draw.cooperative = DDERR_NOEXCLUSIVEMODE;
    for (int attempt = 0; attempt < 3; ++attempt)
        assert(state.Begin(context.data(), bindings, true, begin) == DDERR_NOEXCLUSIVEMODE);
    assert(begins == previousBegins && recreated == 1 && state.pending && !state.busy);
    draw.cooperative = DD_OK;
    recreateFails = true;
    assert(state.Begin(context.data(), bindings, true, begin) == DDERR_SURFACELOST && state.pending);
    assert(state.lastResult.stage == AltTabRecovery::Stage::TextureReload);
    recreateFails = false;
    assert(state.Begin(context.data(), bindings, true, begin) == DD_OK && !state.pending);
    assert(recreated == 3 && textureSurface.lost == DD_OK);

    state.ObservePresent(DDERR_SURFACELOST);
    target.lost = DDERR_SURFACELOST;
    target.restoreResult = DDERR_WRONGMODE;
    assert(state.Begin(context.data(), bindings, true, begin) == DDERR_WRONGMODE);
    assert(state.lastResult.stage == AltTabRecovery::Stage::SurfaceRestore && state.pending);
    target.restoreResult = DD_OK;
    device.targetResult = DDERR_SURFACELOST;
    assert(state.Begin(context.data(), bindings, true, begin) == DDERR_SURFACELOST && state.needsRebind);
    device.targetResult = DD_OK;
    assert(state.Begin(context.data(), bindings, true, begin) == DD_OK && !state.needsRebind);
    assert(draw.modeChanges == 1 && !state.needsModeRestore); // Restore reported WRONGMODE with cooperative DD_OK.

    // Reapply only the cached exclusive game mode, and never while inactive.
    const auto previousModes = draw.modeChanges;
    state.ObservePresent(DDERR_WRONGMODE);
    draw.cooperative = DDERR_WRONGMODE;
    assert(state.Begin(context.data(), bindings, false, begin) == DDERR_NOEXCLUSIVEMODE && draw.modeChanges == previousModes);
    draw.modeResult = DDERR_UNSUPPORTEDMODE;
    assert(state.Begin(context.data(), bindings, true, begin) == DDERR_UNSUPPORTEDMODE && state.pending);
    draw.modeResult = DD_OK;
    assert(state.Begin(context.data(), bindings, true, begin) == DD_OK && draw.modeChanges == previousModes + 2);
    assert(state.lastResult.modeRestored);

    // Distinct render target is restored; an aliased back/render target is not
    // restored twice, and an implicit child failure never triggers a busy loop.
    Write(context, 0x38, &back);
    device.expectedTarget = reinterpret_cast<IDirectDrawSurface4*>(&back);
    state.ObservePresent(DDERR_SURFACELOST);
    back.lost = DDERR_SURFACELOST;
    assert(state.Begin(context.data(), bindings, true, begin) == DD_OK && back.restores == 1);
    state.ObservePresent(DDERR_SURFACELOST);
    back.lost = DDERR_SURFACELOST;
    back.restoreResult = DDERR_IMPLICITLYCREATED;
    assert(state.Begin(context.data(), bindings, true, begin) == DDERR_IMPLICITLYCREATED && back.restores == 2);
    back.restoreResult = DD_OK;
    back.lost = DD_OK;
    assert(state.Begin(context.data(), bindings, true, begin) == DD_OK);

    // Actual regression: the first BeginScene itself reports lost surfaces.
    // Restore before retrying, without relying on presentation ever being reached.
    unsigned lostBegins = 0;
    const auto lostOnce = [&]()
    {
        ++lostBegins;
        if (lostBegins == 1) { primary.lost = DDERR_SURFACELOST; return DDERR_SURFACELOST; }
        return DD_OK;
    };
    assert(state.Begin(context.data(), bindings, true, lostOnce) == DD_OK && lostBegins == 2);
    assert(!state.pending && !state.busy);
    unsigned alwaysLost = 0;
    assert(state.Begin(context.data(), bindings, true, [&]() { ++alwaysLost; return DDERR_SURFACELOST; }) == DDERR_SURFACELOST);
    assert(alwaysLost == 2 && state.pending);
    assert(state.Begin(context.data(), bindings, true, [&]() { ++alwaysLost; return DDERR_SURFACELOST; }) == DDERR_SURFACELOST);
    assert(alwaysLost == 3); // One BeginScene after a pending recovery, no infinite retry.
    assert(state.lastResult.error == DDERR_SURFACELOST && state.lastResult.stage == AltTabRecovery::Stage::BeginScene);

    state.pending = false;
    state.ObservePresent(DDERR_SURFACELOST);
    textureSurface.lost = DDERR_SURFACELOST;
    device.bindingResult = E_INVALIDARG;
    cachedHandle = 42;
    assert(state.Begin(context.data(), bindings, true, begin) == E_INVALIDARG && state.pending);
    assert(state.lastResult.stage == AltTabRecovery::Stage::TextureBinding && cachedHandle == 42);
    device.bindingResult = DD_OK;
    assert(state.Begin(context.data(), bindings, true, begin) == DD_OK && cachedHandle == -1);

    // Dynamic textures without CPU pixels are restored, empty native records
    // and borrowed framebuffer aliases are not recreated. Query errors retry.
    Record dynamic{}, empty{};
    Surface dynamicSurface{surfaceMethods.data()};
    dynamicSurface.lost = DDERR_SURFACELOST;
    Write(dynamic, 0, &dynamicSurface);
    Write(dynamic, 0x108, empty.data());
    head = dynamic.data();
    state.ObservePresent(DDERR_SURFACELOST);
    const auto previousRecreated = recreated;
    assert(state.Begin(context.data(), bindings, true, begin) == DD_OK && dynamicSurface.restores == 1);
    assert(state.lastResult.textures == 1 && recreated == previousRecreated);
    state.ObservePresent(DDERR_SURFACELOST);
    dynamicSurface.lost = DDERR_INVALIDOBJECT;
    assert(state.Begin(context.data(), bindings, true, begin) == DDERR_INVALIDOBJECT);
    assert(state.lastResult.stage == AltTabRecovery::Stage::SurfaceQuery);
    dynamicSurface.lost = DD_OK;
    assert(state.Begin(context.data(), bindings, true, begin) == DD_OK);
    Write(empty, 0x108, dynamic.data()); // Corrupt cyclic list is bounded.
    state.ObservePresent(DDERR_SURFACELOST);
    assert(state.Begin(context.data(), bindings, true, begin) == DDERR_INVALIDOBJECT);
    assert(state.lastResult.stage == AltTabRecovery::Stage::TextureReload);
    head = owned.data();
    assert(state.Begin(context.data(), bindings, true, begin) == DD_OK);

    // A windowed context never changes the display mode, even if requested.
    Write(context, 4, uint32_t{0});
    state.ObservePresent(DDERR_WRONGMODE);
    draw.cooperative = DDERR_WRONGMODE;
    const auto windowedModes = draw.modeChanges;
    assert(state.Begin(context.data(), bindings, true, begin) == DDERR_WRONGMODE && draw.modeChanges == windowedModes);
    draw.cooperative = DD_OK;
    assert(state.Begin(context.data(), bindings, true, begin) == DD_OK && draw.modeChanges == windowedModes);

    // A recreated graphics device in the same native context discards stale
    // pending/cache state, even if the context's allocation address is reused.
    Device replacement{deviceMethods.data()};
    replacement.expectedTarget = reinterpret_cast<IDirectDrawSurface4*>(&back);
    Write(context, 0x40, &replacement);
    const auto previousModeQueries = draw.modeQueries;
    assert(state.Begin(context.data(), bindings, true, begin) == DD_OK);
    assert(state.deviceIdentity == &replacement && !state.needsModeRestore && draw.modeQueries == previousModeQueries + 1);

    // User's real failure: TestCooperativeLevel remains NOEXCLUSIVEMODE after
    // focus returns. Waiting for automatic ownership recovery never gets past it.
    assert(AltTabRecovery::NativeCooperativeFlags(true, 7) == 0x813);
    assert(AltTabRecovery::NativeCooperativeFlags(true, 0x17) == 0x13);
    assert(AltTabRecovery::NativeCooperativeFlags(false, 7) == 0x808);
    assert(AltTabRecovery::NativeCooperativeFlags(false, 0x17) == DDSCL_NORMAL);
    Write(context, 4, uint32_t{1});
    AltTabRecovery::CooperativeSettings settings{ reinterpret_cast<IDirectDraw4*>(&draw),
        reinterpret_cast<HWND>(1), AltTabRecovery::NativeCooperativeFlags(true, 7) };
    bindings.cooperativeSettings = &settings;
    draw.cooperative = DDERR_NOEXCLUSIVEMODE;
    const auto ownershipBegins = begins;
    const auto ownershipModes = draw.modeChanges;
    const auto ownershipBackRestores = back.restores;
    const auto ownershipTextureReloads = recreated;
    assert(state.Begin(context.data(), bindings, false, begin) == DDERR_NOEXCLUSIVEMODE && draw.acquisitions == 0);
    back.lost = textureSurface.lost = DDERR_SURFACELOST;
    draw.acquireResult = DDERR_EXCLUSIVEMODEALREADYSET;
    assert(state.Begin(context.data(), bindings, true, begin) == DDERR_EXCLUSIVEMODEALREADYSET);
    assert(state.lastResult.stage == AltTabRecovery::Stage::ExclusiveOwnership && begins == ownershipBegins);
    assert(draw.acquisitions == 1 && state.pending);
    draw.acquireResult = DD_OK;
    draw.regainOwnership = false;
    assert(state.Begin(context.data(), bindings, true, begin) == DDERR_NOEXCLUSIVEMODE);
    assert(draw.acquisitions == 2 && begins == ownershipBegins && state.pending);
    assert(back.restores == ownershipBackRestores && recreated == ownershipTextureReloads);
    draw.regainOwnership = true;
    assert(state.Begin(context.data(), bindings, true, begin) == DD_OK && !state.pending);
    assert(draw.acquisitions == 3 && state.lastResult.exclusiveReacquired && begins == ownershipBegins + 1);
    assert(back.restores == ownershipBackRestores + 1 && recreated == ownershipTextureReloads + 1);
    assert(draw.modeChanges == ownershipModes); // Don't change an already-correct mode.

    // If Windows reverted to a different desktop mode, ownership comes first,
    // then reapply only the saved game mode. Preserve the original FPU policy.
    settings.flags = draw.expectedFlags = AltTabRecovery::NativeCooperativeFlags(true, 0x17);
    draw.modeWidth = 640;
    draw.modeHeight = 480;
    draw.modeRefresh = 60;
    draw.cooperative = DDERR_NOEXCLUSIVEMODE;
    state.ObservePresent(DDERR_NOEXCLUSIVEMODE);
    assert(state.Begin(context.data(), bindings, true, begin) == DD_OK);
    assert(state.lastResult.exclusiveReacquired && state.lastResult.modeRestored && draw.modeChanges == ownershipModes + 1);
    assert(draw.modeWidth == 2560 && draw.modeHeight == 1440 && draw.modeRefresh == 165);

    // Do not apply another display instance's settings, invent missing settings,
    // claim exclusivity for a windowed context, or retry unrelated errors.
    draw.cooperative = DDERR_NOEXCLUSIVEMODE;
    state.ObservePresent(draw.cooperative);
    const auto acquisitionsBeforeSkips = draw.acquisitions;
    settings.draw = reinterpret_cast<IDirectDraw4*>(1);
    assert(state.Begin(context.data(), bindings, true, begin) == DDERR_NOEXCLUSIVEMODE);
    settings.draw = reinterpret_cast<IDirectDraw4*>(&draw);
    settings.window = reinterpret_cast<HWND>(2);
    assert(state.Begin(context.data(), bindings, true, begin) == DDERR_NOEXCLUSIVEMODE);
    settings.window = reinterpret_cast<HWND>(1);
    settings.flags = DDSCL_NORMAL;
    assert(state.Begin(context.data(), bindings, true, begin) == DDERR_NOEXCLUSIVEMODE);
    settings.flags = draw.expectedFlags;
    Write(context, 4, uint32_t{0});
    assert(state.Begin(context.data(), bindings, true, begin) == DDERR_NOEXCLUSIVEMODE);
    Write(context, 4, uint32_t{1});
    draw.cooperative = E_INVALIDARG;
    assert(state.Begin(context.data(), bindings, true, begin) == E_INVALIDARG);
    assert(draw.acquisitions == acquisitionsBeforeSkips);
    draw.cooperative = DD_OK;
    assert(state.Begin(context.data(), bindings, true, begin) == DD_OK);
    unsigned exclusiveBegins = 0;
    const auto exclusiveLostOnce = [&]()
    {
        if (++exclusiveBegins == 1) { draw.cooperative = DDERR_NOEXCLUSIVEMODE; return DDERR_NOEXCLUSIVEMODE; }
        return DD_OK;
    };
    assert(state.Begin(context.data(), bindings, true, exclusiveLostOnce) == DD_OK && exclusiveBegins == 2);
    assert(state.lastResult.exclusiveReacquired && !state.pending);

    // Unrelated device errors and null/reentrant calls are passed through unchanged.
    state.pending = false;
    beginResult = E_INVALIDARG;
    assert(state.Begin(context.data(), bindings, true, begin) == E_INVALIDARG && !state.pending);
    state.busy = true;
    assert(state.Begin(context.data(), bindings, true, begin) == E_INVALIDARG);
    state.busy = false;
    assert(state.Begin(nullptr, bindings, true, begin) == E_INVALIDARG);
    assert(state.context == nullptr && !state.hasMode && !state.pending);
    std::puts("Alt-Tab exclusive ownership/FPU policy, surface/texture recovery, focus safety, saved mode, bounded retries and gameplay-context preservation tests passed.");
}
