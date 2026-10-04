#define WIN32_LEAN_AND_MEAN
#include "../source/NativeObjectRendering.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>

namespace
{
    uint32_t ReadWord(const std::vector<uint8_t>& bytes, size_t offset)
    {
        uint32_t value;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    }

    void VerifyRelocatedPoolReferences(const std::vector<uint8_t>& image,
        const std::vector<NativeObjectRendering::BytePatch>& plan)
    {
        using namespace NativeObjectRendering;
        const struct { uint32_t first, end; size_t destination; } pools[] = {
            {0x009B2760, 0x009F2EA0, SpritesOffset},
            {0x0088C7D0, 0x008F7E90, TransformsOffset},
            {0x008F7E90, 0x0094FCD0, TrianglesOffset},
            {0x0095C860, 0x0096E1A0, EntriesOffset},
            {0x00529D48, 0x00529E48, RenderListOffset},
            {0x00B1C3C0, 0x00B223C0, RotationsOffset},
            {0x00B223E8, 0x00B423E8, LocalMatricesOffset},
            {0x00B423F0, 0x00B623F0, WorldMatricesOffset}
        };
        const auto pe = ReadWord(image, 0x3C);
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image.data() + pe);
        const auto reloc = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        size_t found = 0;
        for (size_t cursor = reloc.VirtualAddress; cursor < reloc.VirtualAddress + reloc.Size; )
        {
            const auto page = ReadWord(image, cursor);
            const auto block = ReadWord(image, cursor + 4);
            if (block == 0)
                break;
            assert(block >= 8 && cursor + block <= image.size());
            for (size_t index = 0; index < (block - 8) / 2; ++index)
            {
                uint16_t item;
                std::memcpy(&item, image.data() + cursor + 8 + index * 2, sizeof(item));
                if ((item >> 12) != IMAGE_REL_BASED_HIGHLOW)
                    continue;
                const auto rva = page + (item & 0xFFF);
                const auto value = ReadWord(image, rva);
                for (const auto& pool : pools)
                {
                    if (value < pool.first || value >= pool.end)
                        continue;
                    // The only intentional alias: the END of the adjacent sort
                    // buckets shares the entry pool's start. It must NOT move.
                    if (rva + ImageBase == 0x004B5CF8)
                    {
                        assert(value == 0x0095C860);
                        continue;
                    }
                    bool covered = false;
                    for (const auto& patch : plan)
                    {
                        const auto start = patch.address - ImageBase;
                        if (rva < start || rva + 4 > start + patch.expected.size())
                            continue;
                        const auto operand = rva - start;
                        assert(ReadWord(patch.expected, operand) == value);
                        assert(ReadWord(patch.replacement, operand) ==
                            0x10000000 + pool.destination + value - pool.first);
                        covered = true;
                        ++found;
                        break;
                    }
                    if (!covered)
                        std::printf("Missing pool reference: address=%08X value=%08X\n", rva + ImageBase, value);
                    assert(covered);
                }
            }
            cursor += block;
        }
        assert(found == 55);
        std::printf("Complete PE relocation audit: %u pool references covered; bucket-end alias preserved.\n",
            static_cast<unsigned>(found));
    }

    uint8_t* expectedSprite = nullptr;
    size_t submittedSprites = 0, exhaustedSprites = 0;

    void __cdecl ObserveSprite(uint8_t* record)
    {
        assert(record == expectedSprite);
        ++submittedSprites;
    }
    uint32_t __cdecl ReturnZero() { return 0; }
    void __cdecl ClearSpritePlanes(void* planes) { std::memset(planes, 0, 16); }
    void __cdecl ObserveExhaustion(const char*) { ++exhaustedSprites; }

    void ReplayNativeSpriteAllocators(const std::vector<uint8_t>& image)
    {
        using namespace NativeObjectRendering;
        // Clone the validated PE, without starting it or loading its imports.
        // Only the six sprite constructors and reset routine are entered; texture,
        // plane and queue consumers are replaced with deterministic test callbacks.
        auto clone = static_cast<uint8_t*>(VirtualAlloc(nullptr, ImageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        assert(clone != nullptr);
        std::memcpy(clone, image.data(), image.size());
        std::vector<uint8_t> arena(ArenaBytes + 16, 0xCC);
        const auto plan = BuildPlan(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arena.data())), 65536, 0);
        ApplyValidatedPlan(clone, plan);
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image.data() + ReadWord(image, 0x3C));
        const auto reloc = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        const auto delta = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(clone)) - ImageBase;
        for (size_t cursor = reloc.VirtualAddress; cursor < reloc.VirtualAddress + reloc.Size; )
        {
            const auto page = ReadWord(image, cursor), block = ReadWord(image, cursor + 4);
            if (block == 0)
                break;
            for (size_t index = 0; index < (block - 8) / 2; ++index)
            {
                uint16_t item;
                std::memcpy(&item, image.data() + cursor + 8 + index * 2, sizeof(item));
                if ((item >> 12) != IMAGE_REL_BASED_HIGHLOW)
                    continue;
                const auto rva = page + (item & 0xFFF);
                bool rewritten = false;
                for (const auto& patch : plan)
                    if (patch.expected != patch.replacement && rva >= patch.address - ImageBase &&
                        rva + 4 <= patch.address - ImageBase + patch.expected.size() &&
                        ReadWord(patch.expected, rva - (patch.address - ImageBase)) !=
                        ReadWord(patch.replacement, rva - (patch.address - ImageBase)))
                        rewritten = true;
                if (!rewritten)
                {
                    const auto value = ReadWord(image, rva) + delta;
                    std::memcpy(clone + rva, &value, sizeof(value));
                }
            }
            cursor += block;
        }
        const auto redirect = [&](uint32_t address, uintptr_t callback)
        {
            auto location = clone + address - ImageBase;
            location[0] = 0xE9;
            const auto displacement = static_cast<uint32_t>(callback - reinterpret_cast<uintptr_t>(location) - 5);
            std::memcpy(location + 1, &displacement, 4);
        };
        redirect(0x004B8B10, reinterpret_cast<uintptr_t>(&ObserveSprite));
        redirect(0x004B8BF0, reinterpret_cast<uintptr_t>(&ReturnZero));
        redirect(0x004B37B0, reinterpret_cast<uintptr_t>(&ReturnZero));
        redirect(0x004BAC40, reinterpret_cast<uintptr_t>(&ClearSpritePlanes));
        redirect(0x004A8870, reinterpret_cast<uintptr_t>(&ObserveExhaustion));
        DWORD previous;
        assert(VirtualProtect(clone + 0x1000, 0xDB000, PAGE_EXECUTE_READ, &previous));
        FlushInstructionCache(GetCurrentProcess(), clone + 0x1000, 0xDB000);
        std::memset(clone + 0x009B2760 - ImageBase, 0xDD, 2000 * 0x84);
        const auto reset = reinterpret_cast<void(__cdecl*)()>(clone + 0x004B62C0 - ImageBase);
        reset();
        auto& remaining = *reinterpret_cast<uint32_t*>(clone + 0x00508700 - ImageBase);
        assert(remaining == ObjectDrawDistance::SpriteCapacity);
        assert(*reinterpret_cast<uint32_t*>(clone + 0x009F2F24 - ImageBase) == ObjectDrawDistance::TransformCapacity);
        assert(*reinterpret_cast<uint32_t*>(clone + 0x0094FCD0 - ImageBase) == ObjectDrawDistance::EntryCapacity);
        assert(*reinterpret_cast<uint32_t*>(clone + 0x00508728 - ImageBase) == ObjectDrawDistance::TriangleCapacity);
        const auto invoke = [&](size_t variant)
        {
            uint32_t positions[12]{}, uv[6]{};
            if (variant < 3)
            {
                const uint32_t addresses[] = {0x004B8A30, 0x004B8E60, 0x004B8F40};
                const auto function = reinterpret_cast<void(__cdecl*)(uint32_t*,uint32_t,uint32_t,uint32_t,
                    uint32_t*,uint32_t*,uint32_t,uint32_t,uint32_t)>(clone + addresses[variant] - ImageBase);
                function(positions, 0, 0, 0, uv, uv, 0, 0, 0);
            }
            else if (variant == 3)
                reinterpret_cast<void(__cdecl*)(uint32_t*,uint32_t*,uint32_t,uint32_t,uint32_t)>(
                    clone + 0x004B9020 - ImageBase)(positions, uv, 0, 0, 0);
            else if (variant == 4)
                reinterpret_cast<void(__cdecl*)(uint32_t*,uint32_t*,uint32_t*,uint32_t,uint32_t,uint32_t)>(
                    clone + 0x004B9100 - ImageBase)(positions, uv, uv, 0, 0, 0);
            else
                reinterpret_cast<void(__cdecl*)(uint32_t*,uint32_t*,uint32_t)>(
                    clone + 0x004B9210 - ImageBase)(positions, positions, 0);
        };
        submittedSprites = exhaustedSprites = 0;
        std::printf("Replaying native sprite constructors at cloned image %p...\n", clone);
        for (size_t slot = ObjectDrawDistance::SpriteCapacity; slot > 0; --slot)
        {
            expectedSprite = arena.data() + (slot - 1) * 0x84;
            invoke(slot % 6);
            assert(remaining == slot - 1);
            assert(submittedSprites == ObjectDrawDistance::SpriteCapacity - slot + 1);
        }
        const auto fullArena = arena;
        for (size_t variant = 0; variant < 6; ++variant)
            invoke(variant);
        assert(remaining == 0 && submittedSprites == ObjectDrawDistance::SpriteCapacity && exhaustedSprites == 6);
        assert(arena == fullArena); // Exhaustion never underflows or writes past slot zero.
        for (size_t index = TransformsOffset; index < arena.size(); ++index)
            assert(arena[index] == 0xCC); // Every expanded sprite write stayed in its own pool.
        for (size_t index = 0; index < 2000 * 0x84; ++index)
            assert(clone[0x009B2760 - ImageBase + index] == 0xDD); // Old pool is untouched.
        reset();
        assert(remaining == ObjectDrawDistance::SpriteCapacity);
        VirtualFree(clone, 0, MEM_RELEASE);
        std::puts("Native replay: all six sprite allocators fill 16384 relocated slots, preserve canaries and reject overflow.");
    }
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* exception) -> LONG
    {
        std::fprintf(stderr, "Native replay exception %08lX at %p (access address %p).\n",
            exception->ExceptionRecord->ExceptionCode, exception->ExceptionRecord->ExceptionAddress,
            reinterpret_cast<void*>(exception->ExceptionRecord->ExceptionInformation[1]));
        return EXCEPTION_EXECUTE_HANDLER;
    });
    using namespace NativeObjectRendering;
    const auto plan = BuildPlan(0x10000000, 65536, 0x10100000);
    std::vector<uint8_t> image(ImageSize);
    std::set<uint32_t> addresses;
    for (const auto& patch : plan)
    {
        assert(addresses.insert(patch.address).second);
        std::memcpy(image.data() + patch.address - ImageBase, patch.expected.data(), patch.expected.size());
    }
    assert(ValidatePlan(image.data(), image.size(), plan));
    assert(!ValidatePlan(image.data(), 0, plan));
    assert(!ValidatePlan(image.data(), 32, plan));
    const auto pristine = image;
    // Any single missing/mismatched producer/reset/callee prevents the entire plan.
    for (const auto& patch : plan)
    {
        const auto offset = patch.address - ImageBase;
        image[offset] ^= 1;
        const auto mismatched = image;
        assert(!ValidatePlan(image.data(), image.size(), plan));
        assert(image == mismatched); // Validation is read-only, including on failure.
        image = pristine;
    }
    ApplyValidatedPlan(image.data(), plan);
    for (const auto& patch : plan)
        assert(std::memcmp(image.data() + patch.address - ImageBase, patch.replacement.data(), patch.replacement.size()) == 0);
    assert(!ValidatePlan(image.data(), image.size(), plan)); // No accidental double installation.

    // The alias at 0x4b5cf7 is a bucket-table end sentinel, NOT the relocated entry pool.
    assert(addresses.count(0x004B5CF7) == 0);
    assert(ReadWord(image, 0x004412EB - ImageBase) == 65536 / 4);
    assert(ReadWord(image, 0x0044163D - ImageBase) == 65536 / 8);
    assert(ReadWord(image, 0x004B62FA - ImageBase) == ObjectDrawDistance::SpriteCapacity);
    assert(ReadWord(image, 0x004B62E6 - ImageBase) == ObjectDrawDistance::TransformCapacity);
    assert(ReadWord(image, 0x004B62DC - ImageBase) == ObjectDrawDistance::EntryCapacity);
    assert(ReadWord(image, 0x004B5E0B - ImageBase) == ObjectDrawDistance::TriangleCapacity);
    assert(ReadWord(image, 0x004CD122 - ImageBase) == ObjectDrawDistance::RenderSlots * 32 * 3);

    const struct { size_t offset, count, stride, next; } pools[] = {
        {SpritesOffset, ObjectDrawDistance::SpriteCapacity, 0x84, TransformsOffset},
        {TransformsOffset, ObjectDrawDistance::TransformCapacity, 0x1B8, EntriesOffset},
        {EntriesOffset, ObjectDrawDistance::EntryCapacity, 0x18, TrianglesOffset},
        {TrianglesOffset, ObjectDrawDistance::TriangleCapacity, 0x78, RotationsOffset},
        {RotationsOffset, ObjectDrawDistance::RenderSlots, 32 * 12, LocalMatricesOffset},
        {LocalMatricesOffset, ObjectDrawDistance::RenderSlots, 32 * 64, WorldMatricesOffset},
        {WorldMatricesOffset, ObjectDrawDistance::RenderSlots, 32 * 64, RenderListOffset}
    };
    std::vector<uint8_t> arena(ArenaBytes + 16, 0xCC);
    for (const auto& pool : pools)
    {
        assert(pool.offset % 16 == 0 && pool.offset + pool.count * pool.stride == pool.next);
        // Exercise every expanded slot, including the last, without touching the next pool.
        for (size_t remaining = pool.count; remaining > 0; --remaining)
        {
            const size_t offset = pool.offset + (remaining - 1) * pool.stride;
            std::memset(arena.data() + offset, 0x42, pool.stride);
        }
        assert(arena[pool.next] == 0xCC);
    }
    for (size_t i = ArenaBytes; i < arena.size(); ++i)
        assert(arena[i] == 0xCC);

    if (argc > 1)
    {
        // Optional static integration check against toy2.exe; never execute or edit it.
        std::ifstream file(argv[1], std::ios::binary);
        assert(file);
        std::vector<uint8_t> disk{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        const auto peOffset = ReadWord(disk, 0x3C);
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(disk.data() + peOffset);
        assert(nt->OptionalHeader.ImageBase == ImageBase && nt->OptionalHeader.SizeOfImage == ImageSize);
        assert(nt->FileHeader.TimeDateStamp == ImageTimestamp);
        image.assign(ImageSize, 0);
        std::memcpy(image.data(), disk.data(), nt->OptionalHeader.SizeOfHeaders);
        const auto sections = IMAGE_FIRST_SECTION(nt);
        for (size_t i = 0; i < nt->FileHeader.NumberOfSections; ++i)
            std::memcpy(image.data() + sections[i].VirtualAddress, disk.data() + sections[i].PointerToRawData, sections[i].SizeOfRawData);
        assert(ValidatePlan(image.data(), image.size(), plan));
        VerifyRelocatedPoolReferences(image, plan);
        ReplayNativeSpriteAllocators(image);
        std::printf("All %u native patch/guard sites match the supported executable.\n", static_cast<unsigned>(plan.size()));
    }
    std::puts("Atomic signature validation, finite pickup thresholds and expanded renderer-pool boundary tests passed.");
}
