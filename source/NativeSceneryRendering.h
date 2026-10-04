#pragma once

#include "NativeObjectRendering.h"
#include "SceneryDrawDistance.h"

namespace NativeSceneryRendering
{
    using RenderWorld = void(__cdecl*)(int, uint8_t);
    using RenderGrid = void(__cdecl*)(int, int, const uint8_t*);
    struct Bindings
    {
        float* distantNear;
        float* detailedFar;
        float* detailedDistanceSquared;
        const float* projectionFar;
        RenderWorld renderWorld;
        RenderGrid renderGrid;
    };
    inline Bindings bindings{};
    inline bool installed = false;
    inline bool active = false;

    inline void __cdecl RenderExtendedScenery(int room, uint8_t flags)
    {
        if (!installed || active || bindings.distantNear == nullptr || bindings.detailedFar == nullptr ||
            bindings.detailedDistanceSquared == nullptr || bindings.projectionFar == nullptr)
        {
            bindings.renderWorld(room, flags);
            return;
        }
        const SceneryDrawDistance::Settings saved{
            *bindings.distantNear, *bindings.detailedFar, *bindings.detailedDistanceSquared };
        SceneryDrawDistance::Settings extended;
        if (!SceneryDrawDistance::Extend(saved, *bindings.projectionFar, NativeObjectRendering::drawDistance, extended))
        {
            bindings.renderWorld(room, flags);
            return;
        }
        struct RestoreSettings
        {
            SceneryDrawDistance::Settings saved;
            ~RestoreSettings()
            {
                *bindings.distantNear = saved.distantNear;
                *bindings.detailedFar = saved.detailedFar;
                *bindings.detailedDistanceSquared = saved.detailedDistanceSquared;
                active = false;
            }
        } restore{ saved };
        active = true;
        *bindings.distantNear = extended.distantNear;
        *bindings.detailedFar = extended.detailedFar;
        *bindings.detailedDistanceSquared = extended.detailedDistanceSquared;
        // Native room/portal traversal, hide bits, frustum tests, material alpha,
        // queue exhaustion checks and special render flags all remain native.
        bindings.renderWorld(room, flags);
    }

    inline void __cdecl RenderExtendedSceneryGrid(int radius, int pass, const uint8_t* world)
    {
        if (active && world != nullptr && (pass == 0 || pass == 1))
        {
            int columns, rows;
            std::memcpy(&columns, world + 0x34, sizeof(columns));
            std::memcpy(&rows, world + 0x38, sizeof(rows));
            radius = SceneryDrawDistance::GridRadius(radius, columns, rows);
        }
        bindings.renderGrid(radius, pass, world);
    }

    inline std::vector<NativeObjectRendering::BytePatch> BuildPlan(uint32_t worldHook, uint32_t gridHook)
    {
        using namespace NativeObjectRendering;
        std::vector<BytePatch> plan{
            CallPatch(0x0044127B, 0x004CDDD0, worldHook),
            CallPatch(0x004CDEDA, 0x004BC720, gridHook),
            CallPatch(0x004CDFDB, 0x004BC720, gridHook)
        };
        const auto guard = [&](uint32_t address, std::initializer_list<uint8_t> bytes)
            { plan.push_back({address, bytes, bytes}); };
        guard(0x004CDDD0, {0xA1,0x10,0x24,0xB6,0x00,0x81,0xEC,0x14,0x02,0x00,0x00,0x56,0x33,0xF6,0x3B,0xC6});
        // Bind the split globals to their actual native camera-field consumers.
        guard(0x004CDE39, {0xD9,0x05,0x14,0x8D,0x50,0x00,0xA1,0xFC,0x23,0xB6,0x00,0x8B,0xD3,0x83,0xE2,0x03,0xD9,0x58,0x50});
        guard(0x004CDF24, {0xA1,0xFC,0x23,0xB6,0x00,0xD9,0x05,0x18,0x8D,0x50,0x00,0x80,0xF9,0x03,0xD9,0x58,0x54});
        guard(0x004CDEF9, {0xD9,0x05,0x0C,0x8D,0x50,0x00,0xA1,0xFC,0x23,0xB6,0x00,0xD9,0x58,0x4C});
        // Both grid and room traversals read the same primary squared radius.
        guard(0x004BC832, {0xD8,0x1D,0xB0,0x88,0x50,0x00,0xDF,0xE0,0xF6,0xC4,0x01,0x74,0x70});
        guard(0x004BC59C, {0xD8,0x1D,0xB0,0x88,0x50,0x00});
        guard(0x004BC720, {0x83,0xEC,0x3C,0x8D,0x44,0x24,0x24,0x53,0x56,0x57,0x50,0x68,0x80,0xD8,0xE4,0x00});
        // Grid lookup rejects indices before indexing allocated cell tables.
        guard(0x004C3130, {0x56,0x57,0x8B,0x7C,0x24,0x0C,0x8B,0x0F,0x85,0xC9,0x7C,0x2D,0x8B,0x54,0x24,0x10,0x8B,0x72,0x34,0x3B,0xCE,0x7D,0x22,0x8B,0x47,0x04,0x85,0xC0,0x7C,0x1B,0x53,0x8B,0x5A,0x38,0x3B,0xC3,0x5B,0x7D,0x12});
        guard(0x004BC7C5, {0x8B,0x7C,0x24,0x54,0x8B,0x86,0x90,0x00,0x00,0x00,0x3B,0xC7,0x0F,0x85,0xD8,0x00,0x00,0x00,0x8B,0x8E,0x8C,0x00,0x00,0x00,0x83,0xE1,0x01,0x84,0xC9,0x0F,0x85,0xC7,0x00,0x00,0x00});
        return plan;
    }

    enum class Status { Applied, PoolsUnavailable, SignatureMismatch, AllocationFailed, ProtectionFailed, AlreadyInstalled };
    inline const char* StatusName(Status status)
    {
        switch (status)
        {
        case Status::Applied: return "applied";
        case Status::PoolsUnavailable: return "extended object-renderer pools are not installed";
        case Status::SignatureMismatch: return "native scenery signatures do not match";
        case Status::AllocationFailed: return "patch-plan allocation failed";
        case Status::ProtectionFailed: return "code protection change failed";
        case Status::AlreadyInstalled: return "already installed";
        }
        return "unknown status";
    }

    inline Status Install(HMODULE module)
    {
        // Never extend scenery using the small original transform/entry pools.
        if (NativeObjectRendering::arenaMemory == nullptr || reinterpret_cast<uintptr_t>(module) != NativeObjectRendering::ImageBase)
            return Status::PoolsUnavailable;
        if (installed)
            return Status::AlreadyInstalled;
        std::vector<NativeObjectRendering::BytePatch> plan;
        try
        {
            plan = BuildPlan(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&RenderExtendedScenery)),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&RenderExtendedSceneryGrid)));
        }
        catch (const std::bad_alloc&)
        {
            return Status::AllocationFailed;
        }
        auto image = reinterpret_cast<uint8_t*>(module);
        if (!NativeObjectRendering::ValidatePlan(image, NativeObjectRendering::ImageSize, plan))
            return Status::SignatureMismatch;
        DWORD previous;
        if (!VirtualProtect(image + 0x1000, 0xDB000, PAGE_EXECUTE_READWRITE, &previous))
            return Status::ProtectionFailed;
        bindings = {reinterpret_cast<float*>(0x00508D14), reinterpret_cast<float*>(0x00508D18),
            reinterpret_cast<float*>(0x005088B0), reinterpret_cast<const float*>(0x00508D0C),
            reinterpret_cast<RenderWorld>(0x004CDDD0), reinterpret_cast<RenderGrid>(0x004BC720)};
        installed = true;
        NativeObjectRendering::ApplyValidatedPlan(image, plan);
        FlushInstructionCache(GetCurrentProcess(), image + 0x1000, 0xDB000);
        DWORD ignored;
        VirtualProtect(image + 0x1000, 0xDB000, previous, &ignored);
        return Status::Applied;
    }
}
