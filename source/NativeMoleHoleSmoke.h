#pragma once

#include "NativeObjectRendering.h"
#include "MoleHoleSmoke.h"

namespace NativeMoleHoleSmoke
{
    using Spawn = void*(__cdecl*)(int32_t, int32_t, int32_t, int, int);
    using Routine = void(__cdecl*)();
    using Texture = int(__cdecl*)(int);
    using TextureSize = void(__cdecl*)(int, int*, int*, void*, void*, void*);
    using QueueSprite = void(__cdecl*)(const float*, int, float, float, const float*, const float*, int, uint32_t, uint32_t);
    struct Bindings
    {
        const int* level;
        const int* ticks;
        const int32_t* camera;
        const MoleHoleSmoke::HoleList* const* holes;
        const void* const* world;
        const float* projectionFar;
        const uint8_t* const* sprite;
        Spawn spawn;
        Routine update;
        Routine render;
        Texture texture;
        TextureSize textureSize;
        QueueSprite queue;
    };
    inline Bindings bindings{};
    inline MoleHoleSmoke::Pool pool;
    inline bool installed = false;
    inline bool rendering = false;

    inline bool Sync()
    {
        if (!installed || bindings.level == nullptr || bindings.holes == nullptr || bindings.world == nullptr)
        {
            pool.Clear();
            return false;
        }
        // Do not dereference stale level-specific data after a transition.
        if (*bindings.level != 2)
        {
            pool.Clear();
            return false;
        }
        return pool.Sync(*bindings.level, *bindings.holes, *bindings.world);
    }

    inline float Distance()
    {
        return bindings.projectionFar == nullptr ? MoleHoleSmoke::NativeDistance :
            MoleHoleSmoke::DistanceLimit(NativeObjectRendering::drawDistance, *bindings.projectionFar);
    }

    inline void* __cdecl SpawnExtendedSmoke(int32_t x, int32_t y, int32_t z, int type, int motion)
    {
        // Only this guarded level02 call is replaced. Preserve native near emissions
        // exactly, including their random consumption and shared-pool behavior.
        if (!installed || type != 0x3A || motion != 3 || bindings.camera == nullptr ||
            MoleHoleSmoke::EmissionSquare(x, z, bindings.camera) < MoleHoleSmoke::NativeDistanceSquared)
            return bindings.spawn(x, y, z, type, motion);
        if (Sync())
            pool.Emit(x, y, z, bindings.camera, Distance());
        return nullptr; // The guarded caller ignores EAX, then reloads it.
    }

    inline void __cdecl UpdateWithExtendedSmoke()
    {
        bindings.update(); // Projectiles, pickups, collision and native RNG remain native.
        if (Sync() && bindings.ticks != nullptr)
            pool.Tick(*bindings.ticks);
    }

    inline void __cdecl RenderWithExtendedSmoke()
    {
        bindings.render();
        if (rendering || !Sync() || bindings.camera == nullptr || bindings.sprite == nullptr ||
            *bindings.sprite == nullptr)
            return;
        struct RestoreRendering { ~RestoreRendering() { rendering = false; } } restore;
        rendering = true;
        const auto descriptor = *bindings.sprite;
        int16_t textureId;
        std::memcpy(&textureId, descriptor, sizeof(textureId));
        const int texture = bindings.texture(textureId);
        if (texture == 0)
            return;
        int width = 255, height = 255;
        bindings.textureSize(texture, &width, &height, nullptr, nullptr, nullptr);
        if (width <= 0 || height <= 0)
            return;
        const float uv[] = { static_cast<float>(descriptor[8]) / width, static_cast<float>(descriptor[9]) / height };
        const float end[] = { static_cast<float>(descriptor[8] + descriptor[2]) / width,
            static_cast<float>(descriptor[9] + descriptor[3]) / height };
        const float distance = Distance();
        for (const auto& particle : pool.particles)
        {
            if (particle.life <= 0 || !MoleHoleSmoke::WithinRange(particle.x, particle.y, particle.z, bindings.camera, distance))
                continue;
            const float position[] = { static_cast<float>(particle.x) / 32.0f,
                static_cast<float>(particle.y) / 32.0f, static_cast<float>(particle.z) / 32.0f };
            // Same sprite, size growth, blend flags, fog and native hardware clipping
            // as type 0x3A. The existing expanded queue retains its exhaustion check.
            bindings.queue(position, 0, static_cast<float>(particle.size), static_cast<float>(particle.size),
                uv, end, texture, particle.Color(), 0x4840);
        }
    }

    inline std::vector<NativeObjectRendering::BytePatch> BuildPlan(float distance, uint32_t spawnHook,
        uint32_t updateHook, uint32_t renderHook)
    {
        using namespace NativeObjectRendering;
        std::vector<BytePatch> plan{
            WordPatch(0x00419709, {0x81,0xFB,0x00,0x00,0x04,0x00}, 2, MoleHoleSmoke::CullSquare(distance)),
            CallPatch(0x0041971C, 0x0040FDF0, spawnHook),
            CallPatch(0x0049E1A9, 0x00410F40, updateHook),
            CallPatch(0x00441850, 0x00445980, renderHook)
        };
        const auto guard = [&](uint32_t address, std::initializer_list<uint8_t> bytes)
            { plan.push_back({address, bytes, bytes}); };
        guard(0x004196B2, {0x8D,0x34,0x7F,0x39,0x5C,0xB1,0x08,0x8D,0x34,0xB1,0x0F,0x85,0x97,0x00,0x00,0x00});
        guard(0x004196C2, {0x8B,0x15,0xC4,0xAD,0x52,0x00,0x8B,0x76,0x04,0xA1,0xC0,0xAD,0x52,0x00});
        guard(0x004196D4, {0x81,0xC2,0xB0,0x0B,0x00,0x00,0x8B,0x3C,0xB9,0x8B,0x0D,0xC8,0xAD,0x52,0x00});
        guard(0x0041970F, {0x7D,0x2C,0x6A,0x03,0x6A,0x3A,0x57,0x68,0x50,0xF4,0xFF,0xFF,0x56});
        guard(0x00419721, {0x8B,0x0D,0x78,0x9C,0x55,0x00,0xA1,0xE8,0xF5,0x52,0x00,0x8B,0x15,0xDC,0xF5,0x52,0x00,0x8B,0x2D,0xD4,0xF2,0x52,0x00,0x83,0xC4,0x14});
        // Exact visual-only particle type and motion template: no collision, hit,
        // child-effect or sound behavior. Unknown definitions disable the patch.
        guard(0x004EC500, {0x13,0x64,0x18,0x00,0x01,0x00,0x6E,0x00,0x6E,0x00,0x20,0x00,0x09,0x60,0x50,0x40});
        guard(0x004EC990, {0x10,0x10,0x00,0x00,0x1F,0x00,0x00,0x00,0x00,0xFE,0xFF,0xFF,0x1F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00});
        guard(0x004EBFA8, {0x1F,0x00,0x1F,0x1F,0x00,0x00,0x00,0x00,0xC0,0x60});
        return plan;
    }

    enum class Status { Applied, PoolsUnavailable, SignatureMismatch, AllocationFailed, ProtectionFailed, AlreadyInstalled };
    inline const char* StatusName(Status status)
    {
        switch (status)
        {
        case Status::Applied: return "applied";
        case Status::PoolsUnavailable: return "extended object-renderer pools are not installed";
        case Status::SignatureMismatch: return "native mole-hole smoke signatures do not match";
        case Status::AllocationFailed: return "patch-plan allocation failed";
        case Status::ProtectionFailed: return "code protection change failed";
        case Status::AlreadyInstalled: return "already installed";
        }
        return "unknown status";
    }

    inline Status Install(HMODULE module)
    {
        if (NativeObjectRendering::arenaMemory == nullptr || reinterpret_cast<uintptr_t>(module) != NativeObjectRendering::ImageBase)
            return Status::PoolsUnavailable;
        if (installed)
            return Status::AlreadyInstalled;
        std::vector<NativeObjectRendering::BytePatch> plan;
        try
        {
            plan = BuildPlan(NativeObjectRendering::drawDistance, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&SpawnExtendedSmoke)),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&UpdateWithExtendedSmoke)),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&RenderWithExtendedSmoke)));
        }
        catch (const std::bad_alloc&) { return Status::AllocationFailed; }
        auto image = reinterpret_cast<uint8_t*>(module);
        if (!NativeObjectRendering::ValidatePlan(image, NativeObjectRendering::ImageSize, plan))
            return Status::SignatureMismatch;
        DWORD previous;
        if (!VirtualProtect(image + 0x1000, 0xDB000, PAGE_EXECUTE_READWRITE, &previous))
            return Status::ProtectionFailed;
        bindings = { reinterpret_cast<const int*>(0x0088278C), reinterpret_cast<const int*>(0x0052F2D4),
            reinterpret_cast<const int32_t*>(0x0052ADC0), reinterpret_cast<const MoleHoleSmoke::HoleList* const*>(0x00559C78),
            reinterpret_cast<const void* const*>(0x00B62410), reinterpret_cast<const float*>(0x00508D0C),
            reinterpret_cast<const uint8_t* const*>(0x0055754C), reinterpret_cast<Spawn>(0x0040FDF0),
            reinterpret_cast<Routine>(0x00410F40), reinterpret_cast<Routine>(0x00445980),
            reinterpret_cast<Texture>(0x004CE2C0), reinterpret_cast<TextureSize>(0x004BB5E0), reinterpret_cast<QueueSprite>(0x004B8E60) };
        pool.Clear();
        installed = true;
        NativeObjectRendering::ApplyValidatedPlan(image, plan);
        FlushInstructionCache(GetCurrentProcess(), image + 0x1000, 0xDB000);
        DWORD ignored;
        VirtualProtect(image + 0x1000, 0xDB000, previous, &ignored);
        return Status::Applied;
    }
}
