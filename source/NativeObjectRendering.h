#pragma once

#include <windows.h>
#include <algorithm>
#include <array>
#include <initializer_list>
#include <new>
#include <vector>
#include "ObjectDrawDistance.h"

namespace NativeObjectRendering
{
    static_assert(sizeof(void*) == 4, "The native object-rendering patch requires x86.");
    constexpr uint32_t ImageBase = 0x00400000;
    constexpr uint32_t ImageSize = 0x00A7F000;
    constexpr uint32_t ImageTimestamp = 0x381979B4;

    // One process-lifetime arena. All allocations precede patching, and all native
    // producers plus reset counters must validate before ANY distance is increased.
    constexpr size_t SpritesOffset = 0;
    constexpr size_t TransformsOffset = SpritesOffset + ObjectDrawDistance::SpriteCapacity * 0x84;
    constexpr size_t EntriesOffset = TransformsOffset + ObjectDrawDistance::TransformCapacity * 0x1B8;
    constexpr size_t TrianglesOffset = EntriesOffset + ObjectDrawDistance::EntryCapacity * 0x18;
    constexpr size_t RotationsOffset = TrianglesOffset + ObjectDrawDistance::TriangleCapacity * 0x78;
    constexpr size_t LocalMatricesOffset = RotationsOffset + ObjectDrawDistance::RenderSlots * 32 * 12;
    constexpr size_t WorldMatricesOffset = LocalMatricesOffset + ObjectDrawDistance::RenderSlots * 32 * 64;
    constexpr size_t RenderListOffset = WorldMatricesOffset + ObjectDrawDistance::RenderSlots * 32 * 64;
    constexpr size_t ArenaBytes = RenderListOffset + ObjectDrawDistance::ListSlots * sizeof(void*);

    struct BytePatch
    {
        uint32_t address;
        std::vector<uint8_t> expected;
        std::vector<uint8_t> replacement;
    };

    inline BytePatch WordPatch(uint32_t address, std::initializer_list<uint8_t> expected,
        size_t operandOffset, uint32_t value)
    {
        BytePatch patch{ address, expected, expected };
        std::memcpy(patch.replacement.data() + operandOffset, &value, sizeof(value));
        return patch;
    }

    inline BytePatch CallPatch(uint32_t address, uint32_t original, uint32_t replacement)
    {
        auto patch = WordPatch(address, { 0xE8, 0, 0, 0, 0 }, 1, replacement - address - 5);
        const auto displacement = original - address - 5;
        std::memcpy(patch.expected.data() + 1, &displacement, sizeof(displacement));
        return patch;
    }

    inline std::vector<BytePatch> BuildPlan(uint32_t arena, float distance, uint32_t renderHook)
    {
        std::vector<BytePatch> patches;
        const auto word = [&](uint32_t address, std::initializer_list<uint8_t> expected,
            size_t offset, uint32_t value) { patches.push_back(WordPatch(address, expected, offset, value)); };
        const auto sprites = arena + static_cast<uint32_t>(SpritesOffset);
        const auto transforms = arena + static_cast<uint32_t>(TransformsOffset);
        const auto entries = arena + static_cast<uint32_t>(EntriesOffset);
        const auto triangles = arena + static_cast<uint32_t>(TrianglesOffset);
        const auto rotations = arena + static_cast<uint32_t>(RotationsOffset);
        const auto localMatrices = arena + static_cast<uint32_t>(LocalMatricesOffset);
        const auto worldMatrices = arena + static_cast<uint32_t>(WorldMatricesOffset);
        const auto renderList = arena + static_cast<uint32_t>(RenderListOffset);

        // Ghidra: every world-sprite producer uses an indexed 0x84-byte record.
        word(0x004B8A64, {0x8D,0x34,0x95,0x60,0x27,0x9B,0x00}, 3, sprites);
        word(0x004B8E94, {0x8D,0x34,0x95,0x60,0x27,0x9B,0x00}, 3, sprites);
        word(0x004B8F74, {0x8D,0x34,0x95,0x60,0x27,0x9B,0x00}, 3, sprites);
        word(0x004B9040, {0x8D,0x34,0x8D,0x60,0x27,0x9B,0x00}, 3, sprites);
        word(0x004B913B, {0x8D,0x34,0x95,0x60,0x27,0x9B,0x00}, 3, sprites);
        word(0x004B922B, {0x8D,0x34,0x8D,0x60,0x27,0x9B,0x00}, 3, sprites);
        word(0x004B62F4, {0xC7,0x05,0x00,0x87,0x50,0x00,0xD0,0x07,0x00,0x00}, 6, ObjectDrawDistance::SpriteCapacity);

        // Relocate BOTH allocators of 0x1b8-byte render transforms, including their
        // interior field pointers. Consumers receive record pointers, not indices.
        word(0x004B850E, {0x8D,0xB8,0xD0,0xC7,0x88,0x00}, 2, transforms);
        word(0x004B8522, {0x89,0x90,0xD0,0xC8,0x88,0x00}, 2, transforms + 0x100);
        word(0x004B852E, {0x89,0x88,0xD4,0xC8,0x88,0x00}, 2, transforms + 0x104);
        word(0x004B853A, {0x89,0x90,0xD8,0xC8,0x88,0x00}, 2, transforms + 0x108);
        word(0x004B8546, {0x89,0x88,0xDC,0xC8,0x88,0x00}, 2, transforms + 0x10C);
        word(0x004B8555, {0x89,0x88,0xE0,0xC8,0x88,0x00}, 2, transforms + 0x110);
        word(0x004B8566, {0x89,0x88,0xE0,0xC8,0x88,0x00}, 2, transforms + 0x110);
        word(0x004B8572, {0x89,0x90,0xE8,0xC8,0x88,0x00}, 2, transforms + 0x118);
        word(0x004B857E, {0x89,0x88,0xEC,0xC8,0x88,0x00}, 2, transforms + 0x11C);
        word(0x004B8584, {0x89,0x90,0xF0,0xC8,0x88,0x00}, 2, transforms + 0x120);
        word(0x004B858A, {0x8D,0x80,0xF4,0xC8,0x88,0x00}, 2, transforms + 0x124);
        word(0x004B85BA, {0x8D,0xB8,0x04,0xC9,0x88,0x00}, 2, transforms + 0x134);
        word(0x004B85C0, {0x89,0x90,0xE4,0xC8,0x88,0x00}, 2, transforms + 0x114);
        word(0x004B85C9, {0x8D,0x80,0xD0,0xC7,0x88,0x00}, 2, transforms);
        word(0x004B8866, {0x8D,0x1C,0xD5,0xD0,0xC7,0x88,0x00}, 3, transforms);
        word(0x004B88C5, {0x89,0x90,0xD0,0xC8,0x88,0x00}, 2, transforms + 0x100);
        word(0x004B88D1, {0x89,0x88,0xD4,0xC8,0x88,0x00}, 2, transforms + 0x104);
        word(0x004B88DD, {0x89,0x90,0xD8,0xC8,0x88,0x00}, 2, transforms + 0x108);
        word(0x004B88EA, {0x89,0x88,0xDC,0xC8,0x88,0x00}, 2, transforms + 0x10C);
        word(0x004B88F0, {0x89,0x90,0xE0,0xC8,0x88,0x00}, 2, transforms + 0x110);
        word(0x004B88F6, {0x8D,0x80,0xF4,0xC8,0x88,0x00}, 2, transforms + 0x124);
        word(0x004B8920, {0x89,0x90,0xE4,0xC8,0x88,0x00}, 2, transforms + 0x114);
        word(0x004B8926, {0x8D,0x80,0xD0,0xC7,0x88,0x00}, 2, transforms);
        word(0x004B62E0, {0xC7,0x05,0x24,0x2F,0x9F,0x00,0xE8,0x03,0x00,0x00}, 6, ObjectDrawDistance::TransformCapacity);

        word(0x004B8687, {0x8D,0x34,0xC5,0x60,0xC8,0x95,0x00}, 3, entries);
        word(0x004B89C7, {0x8D,0x34,0xC5,0x60,0xC8,0x95,0x00}, 3, entries);
        // Do NOT relocate 0x004b5cf7: its identical 0x95c860 operand is the END
        // of the adjacent 1024-bucket triangle table, not a render-entry pointer.
        word(0x004B5E75, {0x8D,0x1C,0xC5,0x90,0x7E,0x8F,0x00}, 3, triangles);
        static_assert(ObjectDrawDistance::EntryCapacity == ObjectDrawDistance::TriangleCapacity);
        word(0x004B62DB, {0xB8,0xB8,0x0B,0x00,0x00}, 1, ObjectDrawDistance::EntryCapacity);
        word(0x004B5E05, {0xC7,0x05,0x28,0x87,0x50,0x00,0xB8,0x0B,0x00,0x00}, 6, ObjectDrawDistance::TriangleCapacity);

        // Keep all actor-render lists/bone buffers large enough for 64 actors,
        // Buzz and the terminator. Original gameplay lists/pool sizes stay intact.
        word(0x0041496A, {0x89,0x1D,0x48,0x9D,0x52,0x00}, 2, renderList);
        // Retained native actor-list sorter (no live callers recovered in this
        // executable): keep its aliases consistent even if reached indirectly.
        word(0x0043DAC6, {0x8B,0x35,0x48,0x9D,0x52,0x00}, 2, renderList);
        word(0x0043DAD7, {0xB8,0x48,0x9D,0x52,0x00}, 1, renderList);
        word(0x0043DAF8, {0xC7,0x44,0x24,0x10,0x48,0x9D,0x52,0x00}, 4, renderList);
        word(0x0043DB96, {0x8B,0x0C,0x85,0x48,0x9D,0x52,0x00}, 3, renderList);
        word(0x0043DB9F, {0x89,0x3C,0x85,0x48,0x9D,0x52,0x00}, 3, renderList);
        word(0x0043DBBA, {0x8B,0x35,0x48,0x9D,0x52,0x00}, 2, renderList);
        word(0x0043DBCE, {0xC7,0x44,0x24,0x1C,0x48,0x9D,0x52,0x00}, 4, renderList);
        word(0x004411A9, {0x68,0x48,0x9D,0x52,0x00}, 1, renderList);
        word(0x00441280, {0x68,0x48,0x9D,0x52,0x00}, 1, renderList);
        word(0x004A28BF, {0x89,0x88,0x48,0x9D,0x52,0x00}, 2, renderList);
        word(0x004A28D6, {0xC7,0x04,0x95,0x48,0x9D,0x52,0x00,0x00,0xF3,0x52,0x00}, 3, renderList);
        word(0x004A28E1, {0xC7,0x04,0x95,0x4C,0x9D,0x52,0x00,0x00,0x00,0x00,0x00}, 3, renderList + 4);
        word(0x004CD121, {0xB9,0x00,0x18,0x00,0x00}, 1, ObjectDrawDistance::RenderSlots * 32 * 3);
        word(0x004CD128, {0xBF,0xC0,0xC3,0xB1,0x00}, 1, rotations);
        word(0x004CD8BF, {0xC7,0x44,0x24,0x20,0xC4,0xC3,0xB1,0x00}, 4, rotations + 4);
        word(0x004CDBD8, {0x89,0x88,0xC0,0xC3,0xB1,0x00}, 2, rotations);
        word(0x004CDBE2, {0x89,0x90,0xC4,0xC3,0xB1,0x00}, 2, rotations + 4);
        word(0x004CDBE8, {0x89,0x88,0xC8,0xC3,0xB1,0x00}, 2, rotations + 8);
        word(0x004CD15A, {0x05,0xE8,0x23,0xB2,0x00}, 1, localMatrices);
        word(0x004CD845, {0x8D,0xB8,0xE8,0x23,0xB2,0x00}, 2, localMatrices);
        word(0x004CD84B, {0x8D,0x90,0xF0,0x23,0xB4,0x00}, 2, worldMatrices);
        word(0x004CD8C7, {0xC7,0x44,0x24,0x24,0xF0,0x23,0xB4,0x00}, 4, worldMatrices);
        word(0x004CDC52, {0xBF,0xF0,0x23,0xB4,0x00}, 1, worldMatrices);

        // Override ONLY the two render-time reads. 0x54bef0 also sizes a fixed
        // fade lookup table during level loading; changing that global would corrupt memory.
        distance = ObjectDrawDistance::SanitizeDistance(distance);
        auto coins = WordPatch(0x004412EA, {0xA1,0xF0,0xBE,0x54,0x00}, 1, static_cast<uint32_t>(distance / 4));
        coins.replacement[0] = 0xB8; // MOV EAX,finite radius (the native code divides by 4 again).
        patches.push_back(coins);
        auto pickups = WordPatch(0x0044163C, {0xA1,0xF0,0xBE,0x54,0x00}, 1, static_cast<uint32_t>(distance / 8));
        pickups.replacement[0] = 0xB8;
        patches.push_back(pickups);

        patches.push_back(CallPatch(0x004394CC, 0x00440F70, renderHook));
        patches.push_back(CallPatch(0x0049E31D, 0x00440F70, renderHook));
        patches.push_back(CallPatch(0x0049E5F7, 0x00440F70, renderHook));
        // Read-only layout/callee guards. The pointer helpers are used by the overlay;
        // a matching pool operand alone is not enough to trust those functions.
        const auto guard = [&](uint32_t address, std::initializer_list<uint8_t> bytes)
            { patches.push_back({ address, bytes, bytes }); };
        guard(0x00407153, {0xB9,0xC0,0x09,0x00,0x00,0x33,0xC0,0xBF,0x40,0xC8,0x52,0x00,0xF3,0xAB});
        guard(0x004BA1F0, {0xA1,0x30,0x62,0x9F,0x00,0x53,0x55,0x56,0x57,0xBE,0x02,0x00,0x00,0x00});
        guard(0x004BC160, {0x8B,0x44,0x24,0x04,0x83,0xE0,0x3F,0x0F,0xBE,0x80,0x14,0xC4,0xA4,0x00,0xC3});
        guard(0x004C424F, {0x89,0x83,0x78,0x02,0x00,0x00});
        guard(0x00447C19, {0x8B,0x04,0x95,0xD4,0x7C,0x54,0x00});
        return patches;
    }

    inline bool ValidatePlan(const uint8_t* image, size_t bytes, const std::vector<BytePatch>& patches)
    {
        for (const auto& patch : patches)
        {
            if (patch.address < ImageBase || patch.expected.empty() || patch.expected.size() != patch.replacement.size())
                return false;
            const size_t rva = patch.address - ImageBase;
            if (rva >= bytes || patch.expected.size() > bytes - rva ||
                std::memcmp(image + rva, patch.expected.data(), patch.expected.size()) != 0)
                return false;
        }
        return true;
    }

    inline void ApplyValidatedPlan(uint8_t* image, const std::vector<BytePatch>& patches)
    {
        for (const auto& patch : patches)
            if (patch.expected != patch.replacement)
                std::memcpy(image + patch.address - ImageBase, patch.replacement.data(), patch.replacement.size());
    }

    enum class Status { Applied, UnsupportedExecutable, SignatureMismatch, AllocationFailed, ProtectionFailed };
    inline const char* StatusName(Status status)
    {
        switch (status)
        {
        case Status::Applied: return "applied";
        case Status::UnsupportedExecutable: return "unsupported executable or already installed";
        case Status::SignatureMismatch: return "native code signatures do not match";
        case Status::AllocationFailed: return "renderer-pool allocation failed";
        case Status::ProtectionFailed: return "code protection change failed";
        }
        return "unknown status";
    }
    inline void* arenaMemory = nullptr;
    inline float drawDistance = ObjectDrawDistance::DefaultDistance;

    inline Status Install(HMODULE module, uint32_t renderHook, float distance)
    {
        if (reinterpret_cast<uintptr_t>(module) != ImageBase || arenaMemory != nullptr)
            return Status::UnsupportedExecutable;
        auto image = reinterpret_cast<uint8_t*>(module);
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x800)
            return Status::UnsupportedExecutable;
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt->FileHeader.TimeDateStamp != ImageTimestamp || nt->OptionalHeader.SizeOfImage != ImageSize ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            return Status::UnsupportedExecutable;
        // Validate first with dummy destinations: unsupported/modified code stays untouched.
        std::vector<BytePatch> plan;
        void* memory = nullptr;
        try
        {
            plan = BuildPlan(0, distance, renderHook);
            if (!ValidatePlan(image, ImageSize, plan))
                return Status::SignatureMismatch;
            memory = VirtualAlloc(nullptr, ArenaBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (memory == nullptr)
                return Status::AllocationFailed;
            plan = BuildPlan(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(memory)), distance, renderHook);
        }
        catch (const std::bad_alloc&)
        {
            if (memory != nullptr)
                VirtualFree(memory, 0, MEM_RELEASE);
            return Status::AllocationFailed;
        }
        // Cover only the supported executable's .text section, never its data/Windows DLLs.
        DWORD previousProtection = 0;
        if (!VirtualProtect(image + 0x1000, 0xDB000, PAGE_EXECUTE_READWRITE, &previousProtection))
        {
            VirtualFree(memory, 0, MEM_RELEASE);
            return Status::ProtectionFailed;
        }
        arenaMemory = memory;
        drawDistance = ObjectDrawDistance::SanitizeDistance(distance);
        ApplyValidatedPlan(image, plan);
        FlushInstructionCache(GetCurrentProcess(), image + 0x1000, 0xDB000);
        DWORD ignored = 0;
        VirtualProtect(image + 0x1000, 0xDB000, previousProtection, &ignored);
        return Status::Applied;
    }

    inline std::array<ObjectDrawDistance::Actor*, ObjectDrawDistance::ListSlots>& RenderList()
    {
        return *reinterpret_cast<std::array<ObjectDrawDistance::Actor*, ObjectDrawDistance::ListSlots>*>(
            static_cast<uint8_t*>(arenaMemory) + RenderListOffset);
    }
}
