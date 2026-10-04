#pragma once

#include <windows.h>
#include <cstdint>
#include <cstring>
#include "Hooking.Patterns.h"
#include "HighResolutionLimits.h"

namespace NativeD3DResolution
{
    // Experimental ceiling: full 7680x4320 and portrait equivalents.
    // GPU/surface capabilities still apply; this is not an unlimited-resolution patch.
    constexpr uint32_t OriginalLimit = HighResolutionLimits::OriginalLimit;
    constexpr uint32_t RaisedLimit = HighResolutionLimits::RaisedLimit;

    // SDK IID_IDirect3DHALDevice; do not raise the software rasterizer's limits.
    constexpr GUID HalDeviceGuid = { 0x84E63DE0, 0x46AA, 0x11CF,
        { 0x81, 0x6F, 0x00, 0x00, 0xC0, 0x20, 0x15, 0x6E } };

    inline bool IsHalDevice(const GUID* deviceGuid)
    {
        return deviceGuid != nullptr && std::memcmp(deviceGuid, &HalDeviceGuid, sizeof(GUID)) == 0;
    }

    enum class Status
    {
        Applied,
        AlreadyApplied,
        ModuleMissing,
        SignatureMismatch,
        ProtectionFailed
    };

    struct Result
    {
        Status status;
        size_t createMatches = 0;
        size_t targetMatches = 0;
    };

    inline Result RaiseLimit(HMODULE module)
    {
        if (module == nullptr)
            return { Status::ModuleMissing };

        // Ghidra: native D3DIM CreateDevice and SetRenderTarget validate both surface
        // dimensions against a shared MOV EAX,2048. Do not patch flags or other limits.
        // Wildcard the immediate to recognize our already-installed patch safely.
        hook::module_pattern create(module,
            "B8 ? ? ? ? 39 45 ? 0F 87 ? ? ? ? 39 45 ? 0F 87 ? ? ? ? 8B 07 8D 8D ? ? ? ?");
        hook::module_pattern target(module,
            "B8 ? ? ? ? 39 85 ? ? ? ? 0F 87 ? ? ? ? 39 85 ? ? ? ? 0F 87 ? ? ? ? C7 45 ? 00 00 02 00");
        Result result{ Status::SignatureMismatch, create.size(), target.size() };
        if (result.createMatches != 1 || result.targetMatches != 1)
            return result;

        auto createImmediate = create.get_first<uint8_t>(1);
        auto targetImmediate = target.get_first<uint8_t>(1);
        uint32_t createLimit = 0, targetLimit = 0;
        std::memcpy(&createLimit, createImmediate, sizeof(createLimit));
        std::memcpy(&targetLimit, targetImmediate, sizeof(targetLimit));
        if (createLimit == RaisedLimit && targetLimit == RaisedLimit)
        {
            result.status = Status::AlreadyApplied;
            return result;
        }
        if (createLimit != OriginalLimit || targetLimit != OriginalLimit)
            return result; // Respect another patch, rather than overwriting it.

        // Make both sites writable before changing either, avoiding a partial patch.
        DWORD createProtection = 0, targetProtection = 0;
        if (!VirtualProtect(createImmediate, sizeof(uint32_t), PAGE_EXECUTE_READWRITE, &createProtection))
        {
            result.status = Status::ProtectionFailed;
            return result;
        }
        if (!VirtualProtect(targetImmediate, sizeof(uint32_t), PAGE_EXECUTE_READWRITE, &targetProtection))
        {
            DWORD ignored = 0;
            VirtualProtect(createImmediate, sizeof(uint32_t), createProtection, &ignored);
            result.status = Status::ProtectionFailed;
            return result;
        }

        std::memcpy(createImmediate, &RaisedLimit, sizeof(RaisedLimit));
        std::memcpy(targetImmediate, &RaisedLimit, sizeof(RaisedLimit));
        FlushInstructionCache(GetCurrentProcess(), createImmediate - 1, 5);
        FlushInstructionCache(GetCurrentProcess(), targetImmediate - 1, 5);
        DWORD ignored = 0;
        VirtualProtect(targetImmediate, sizeof(uint32_t), targetProtection, &ignored);
        VirtualProtect(createImmediate, sizeof(uint32_t), createProtection, &ignored);
        result.status = Status::Applied;
        return result;
    }
}
