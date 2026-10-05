#define WIN32_LEAN_AND_MEAN
#include "../source/NativeSceneryRendering.h"
#include <cassert>
#include <cfloat>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>

namespace
{
    template<class T> T Read(const uint8_t* memory, size_t offset)
    {
        T value;
        std::memcpy(&value, memory + offset, sizeof(value));
        return value;
    }
    template<class T> void Write(uint8_t* memory, size_t offset, T value)
    {
        std::memcpy(memory + offset, &value, sizeof(value));
    }

    float nearSplit = 10000, farSplit = 12000, square = 144000000, projectionFar = 48000;
    int worldCalls = 0, gridCalls = 0;
    bool recurse = false, throwDuringRender = false;

    void __cdecl ObserveGrid(int radius, int pass, const uint8_t* world)
    {
        assert(radius == (NativeSceneryRendering::active && world != nullptr && pass <= 1 ? 20 : 15));
        ++gridCalls;
    }
    void __cdecl ObserveWorld(int room, uint8_t flags)
    {
        assert(room == 7 && flags == 2);
        assert(nearSplit == 46000 && farSplit == 48000 && square == 65536.0f * 65536.0f);
        ++worldCalls;
        if (recurse)
        {
            recurse = false;
            NativeSceneryRendering::RenderExtendedScenery(room, flags);
            assert(NativeSceneryRendering::active);
        }
        uint8_t world[0x44]{};
        Write(world, 0x34, 20);
        Write(world, 0x38, 20);
        NativeSceneryRendering::RenderExtendedSceneryGrid(15, 0, world);
        if (throwDuringRender)
            throw std::runtime_error("simulated render failure");
    }
    void __cdecl ObservePassthrough(int room, uint8_t flags)
    {
        assert(room == 7 && flags == 2);
        assert(!NativeSceneryRendering::active && nearSplit == 10000 && farSplit == 12000 && square == 144000000);
        ++worldCalls;
    }

    void TestSettingsAndScope()
    {
        using namespace SceneryDrawDistance;
        const Settings original{10000, 12000, 144000000};
        Settings output;
        assert(Extend(original, 48000, 65536, output));
        assert(output.detailedFar == 48000 && output.distantNear == 46000);
        assert(output.detailedDistanceSquared == 65536.0f * 65536.0f);
        assert(Extend(original, 48000, 16000, output));
        assert(output.detailedFar == 16000 && output.distantNear == 14000);
        assert(Extend(original, 48000, 1024, output));
        assert(std::memcmp(&output, &original, sizeof(original)) == 0); // Never shrink native ranges.
        const Settings lowQuality{3000, 3500, 100000000};
        assert(Extend(lowQuality, 48000, 65536, output));
        assert(output.distantNear == 47500 && output.detailedFar == 48000);
        const Settings unlimited{10000,12000,std::numeric_limits<float>::infinity()};
        assert(Extend(unlimited, 48000, 65536, output));
        assert(std::isinf(output.detailedDistanceSquared)); // Preserve legacy geometry option.
        assert(Extend(original, 48000, std::numeric_limits<float>::quiet_NaN(), output));
        assert(output.detailedFar == 48000);
        assert(!Extend(original, 11000, 65536, output));
        assert(!Extend({12000,10000,1}, 48000, 65536, output));
        assert(!Extend({-1,12000,1}, 48000, 65536, output));
        assert(!Extend({10000,12000,-1}, 48000, 65536, output));
        assert(!Extend({10000,12000,std::numeric_limits<float>::quiet_NaN()}, 48000, 65536, output));
        assert(!Extend(original, std::numeric_limits<float>::infinity(), 65536, output));
        assert(GridRadius(15,20,20) == 20 && GridRadius(30,20,20) == 30);
        assert(GridRadius(15,4,32) == 32 && GridRadius(15,0,20) == 15);
        assert(GridRadius(15,65,20) == 15 && GridRadius(-1,20,20) == -1);

        using namespace NativeSceneryRendering;
        assert(Install(nullptr) == Status::PoolsUnavailable && !installed);
        bindings = {&nearSplit, &farSplit, &square, &projectionFar, ObserveWorld, ObserveGrid};
        installed = true;
        NativeObjectRendering::drawDistance = 65536;
        recurse = true;
        RenderExtendedScenery(7, 2);
        assert(worldCalls == 2 && gridCalls == 2 && !active);
        assert(nearSplit == 10000 && farSplit == 12000 && square == 144000000);
        throwDuringRender = true;
        try { RenderExtendedScenery(7, 2); assert(false); }
        catch (const std::runtime_error&) {}
        assert(!active && nearSplit == 10000 && farSplit == 12000 && square == 144000000);
        throwDuringRender = false;
        RenderExtendedSceneryGrid(15, 0, nullptr);
        uint8_t world[0x44]{};
        Write(world, 0x34, 20);
        Write(world, 0x38, 20);
        RenderExtendedSceneryGrid(15, 0, world); // Outside scoped rendering: unchanged.
        active = true;
        RenderExtendedSceneryGrid(15, 2, world); // Unknown pass: unchanged.
        active = false;
        unsigned short before, after;
        float liveValue;
        _asm fld1
        _asm fnstsw before
        for (int index = 0; index < 512; ++index)
            RenderExtendedScenery(7, 2);
        _asm fnstsw after
        _asm fstp liveValue
        assert((before & 0x3800) == (after & 0x3800) && (after & 0x41) == 0 && liveValue == 1);
        bindings.renderWorld = ObservePassthrough;
        projectionFar = 11000;
        RenderExtendedScenery(7, 2); // Invalid layout/settings: unchanged.
        projectionFar = 48000;
        bindings.projectionFar = nullptr;
        RenderExtendedScenery(7, 2); // Missing bindings: unchanged.
        installed = false;
        RenderExtendedScenery(7, 2); // Disabled feature: unchanged.
        bindings = {};
    }

    uint8_t* replayImage = nullptr;
    uint8_t replayCamera[0x64]{};
    float currentNear = 0, currentFar = FLT_MAX;
    int detailedModels = 0, distantModels = 0, roomVisits = 0;
    const uint8_t* detailedMesh = nullptr;
    const uint8_t* distantMesh = nullptr;
    void __cdecl IgnoreNativeCall() {}
    void __cdecl CaptureProjection(const void*)
    {
        assert(Read<float>(replayCamera, 0x4C) == 48000); // Hardware clip never changes.
        currentNear = Read<float>(replayCamera, 0x50);
        currentFar = Read<float>(replayCamera, 0x54);
    }
    void __cdecl ReturnCameraPosition(const void*, float* position)
    {
        position[0] = position[1] = position[2] = 0;
    }
    uint32_t __cdecl TestFrustum(const float* position, float)
    {
        if (position[0] == -999 || position[2] < currentNear || position[2] > currentFar)
            return 1;
        return 0;
    }
    void __cdecl CaptureModel(const uint8_t* model, const void*, uint32_t)
    {
        if (model == detailedMesh)
            ++detailedModels;
        else
        {
            assert(model == distantMesh);
            ++distantModels;
        }
    }
    void __cdecl CaptureRoom(const void*, int room, int pass, int portal)
    {
        assert(room == 0 || room == 7);
        assert(pass == 0 || pass == 1);
        assert(portal == 0);
        ++roomVisits;
    }

    void ReplayNativeScenery(const std::vector<uint8_t>& image)
    {
        using namespace NativeObjectRendering;
        using namespace NativeSceneryRendering;
        replayImage = static_cast<uint8_t*>(VirtualAlloc(nullptr, ImageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        assert(replayImage != nullptr);
        std::memcpy(replayImage, image.data(), image.size());
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image.data() + Read<uint32_t>(image.data(), 0x3C));
        const auto reloc = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        const auto delta = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(replayImage)) - ImageBase;
        for (size_t cursor = reloc.VirtualAddress; cursor < reloc.VirtualAddress + reloc.Size; )
        {
            const auto page = Read<uint32_t>(image.data(), cursor), block = Read<uint32_t>(image.data(), cursor + 4);
            if (block == 0) break;
            for (size_t index = 0; index < (block - 8) / 2; ++index)
            {
                const auto item = Read<uint16_t>(image.data(), cursor + 8 + index * 2);
                if ((item >> 12) == IMAGE_REL_BASED_HIGHLOW)
                {
                    const auto rva = page + (item & 0xFFF);
                    Write(replayImage, rva, Read<uint32_t>(image.data(), rva) + delta);
                }
            }
            cursor += block;
        }
        // Relative calls in the private image must reach the actual test hook addresses.
        const auto plan = NativeSceneryRendering::BuildPlan(
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&RenderExtendedScenery)) - delta,
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&RenderExtendedSceneryGrid)) - delta);
        ApplyValidatedPlan(replayImage, plan);
        const auto redirect = [&](uint32_t address, uintptr_t callback)
        {
            auto code = replayImage + address - ImageBase;
            code[0] = 0xE9;
            Write(code, 1, static_cast<uint32_t>(callback - reinterpret_cast<uintptr_t>(code) - 5));
        };
        redirect(0x004B5630, reinterpret_cast<uintptr_t>(&IgnoreNativeCall));
        redirect(0x004BABE0, reinterpret_cast<uintptr_t>(&IgnoreNativeCall));
        redirect(0x004BAC70, reinterpret_cast<uintptr_t>(&IgnoreNativeCall));
        redirect(0x004CE050, reinterpret_cast<uintptr_t>(&CaptureProjection));
        redirect(0x004B6A50, reinterpret_cast<uintptr_t>(&IgnoreNativeCall));
        redirect(0x004BC120, reinterpret_cast<uintptr_t>(&IgnoreNativeCall));
        redirect(0x004BC140, reinterpret_cast<uintptr_t>(&IgnoreNativeCall));
        redirect(0x004BC170, reinterpret_cast<uintptr_t>(&IgnoreNativeCall));
        redirect(0x004BC460, reinterpret_cast<uintptr_t>(&CaptureRoom));
        redirect(0x004A97E0, reinterpret_cast<uintptr_t>(&ReturnCameraPosition));
        redirect(0x004BA270, reinterpret_cast<uintptr_t>(&TestFrustum));
        redirect(0x004B8490, reinterpret_cast<uintptr_t>(&CaptureModel));
        redirect(0x004B5CE0, reinterpret_cast<uintptr_t>(&IgnoreNativeCall));

        uint8_t world[0x67C]{}, meshA[0x48]{}, meshB[0x48]{};
        detailedMesh = meshA;
        distantMesh = meshB;
        Write(meshA, 0x44, 1.0f);
        Write(meshB, 0x44, 1.0f);
        const uint8_t* models[]{meshA,meshB};
        std::array<uint8_t*,400> detailedCells{}, distantCells{};
        uint8_t records[5][0x94]{};
        for (int index = 0; index < 5; ++index)
        {
            // Last corner cell is outside the original radius 15, but still allocated.
            Write(records[index], 0, index < 3 ? records[index + 1] : static_cast<uint8_t*>(nullptr));
            Write(records[index], 0x14, index == 3 ? 100000.0f : 30000.0f);
        }
        Write(records[1], 0x8C, 1); // Hidden record must remain hidden.
        Write(records[2], 0x0C, -999.0f); // Native frustum rejection must remain effective.
        Write(records[4], 0x70, 1);
        Write(records[4], 0x90, 1);
        detailedCells[399] = records[0];
        distantCells[399] = records[4];
        Write(world, 0, models);
        Write(world, 0x10, 4);
        Write(world, 0x14, 1);
        Write(world, 0x1C, detailedCells.data());
        Write(world, 0x20, distantCells.data());
        Write(world, 0x24, -1000.0f);
        Write(world, 0x2C, -1000.0f);
        Write(world, 0x34, 20);
        Write(world, 0x38, 20);
        Write(world, 0x3C, 2000.0f);
        Write(world, 0x40, 2000.0f);
        Write(replayImage, 0x00B62410 - ImageBase, world);
        Write(replayImage, 0x00B623FC - ImageBase, replayCamera);
        auto floatAt = [&](uint32_t address) { return reinterpret_cast<float*>(replayImage + address - ImageBase); };
        bindings = {floatAt(0x00508D14),floatAt(0x00508D18),floatAt(0x005088B0),floatAt(0x00508D0C),
            reinterpret_cast<RenderWorld>(replayImage + 0x004CDDD0 - ImageBase),
            reinterpret_cast<RenderGrid>(replayImage + 0x004BC720 - ImageBase)};
        *bindings.detailedDistanceSquared = 144000000;
        *floatAt(0x005088B4) = 1000000;
        DWORD previous;
        assert(VirtualProtect(replayImage + 0x1000, 0xDB000, PAGE_EXECUTE_READ, &previous));
        FlushInstructionCache(GetCurrentProcess(), replayImage + 0x1000, 0xDB000);
        installed = false;
        // Baseline native split renders the distant version, not detailed corner scenery.
        RenderExtendedScenery(-1, 0);
        assert(detailedModels == 0 && distantModels == 1);
        installed = true;
        detailedModels = distantModels = 0;
        RenderExtendedScenery(-1, 0);
        assert(detailedModels == 1 && distantModels == 0); // No duplicate old LOD.
        assert(*bindings.distantNear == 10000 && *bindings.detailedFar == 12000);
        assert(*bindings.detailedDistanceSquared == 144000000 && *floatAt(0x005088B4) == 1000000);
        assert(currentNear == 0 && currentFar == FLT_MAX && !active); // Native final projection reset.
        Write(world, 0x268, 1); // Portal path must remain portal-based, not replaced by grid traversal.
        roomVisits = detailedModels = distantModels = 0;
        RenderExtendedScenery(7, 0);
        assert(roomVisits == 4 && detailedModels == 0 && distantModels == 0);
        Write(world, 0x268, 0);
        unsigned short before, after;
        float callerValue;
        _asm fld1
        _asm fnstsw before
        for (int index = 0; index < 512; ++index)
            RenderExtendedScenery(-1, static_cast<uint8_t>(index % 3));
        _asm fnstsw after
        _asm fstp callerValue
        assert((before & 0x3800) == (after & 0x3800) && (after & 0x41) == 0 && callerValue == 1);
        assert(*bindings.distantNear == 10000 && *bindings.detailedFar == 12000 && !active);
        // Keep native special flags (both passes forced), including their second-threshold write.
        RenderExtendedScenery(-1, 3);
        assert(*floatAt(0x005088B4) == 0 && *bindings.detailedDistanceSquared == 144000000);
        installed = false;
        bindings = {};
        VirtualFree(replayImage, 0, MEM_RELEASE);
        replayImage = nullptr;
        std::puts("Native world/grid replay: corner scenery extended; hidden/frustum/range rejects and portal paths retained; LOD overlap prevented; 512 calls preserve x87.");
    }
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    TestSettingsAndScope();
    using namespace NativeObjectRendering;
    const auto plan = NativeSceneryRendering::BuildPlan(0x10000000, 0x10001000);
    std::vector<uint8_t> image(ImageSize);
    std::set<uint32_t> addresses;
    for (const auto& patch : plan)
    {
        assert(addresses.insert(patch.address).second);
        std::memcpy(image.data() + patch.address - ImageBase, patch.expected.data(), patch.expected.size());
    }
    const auto pristine = image;
    assert(ValidatePlan(image.data(), image.size(), plan));
    for (const auto& patch : plan)
    {
        image[patch.address - ImageBase] ^= 1;
        const auto damaged = image;
        assert(!ValidatePlan(image.data(), image.size(), plan) && image == damaged);
        image = pristine;
    }
    assert(!ValidatePlan(image.data(), 0, plan));
    ApplyValidatedPlan(image.data(), plan);
    assert(!ValidatePlan(image.data(), image.size(), plan));
    for (size_t index = 0; index < image.size(); ++index)
    {
        if (image[index] != pristine[index])
            assert((index >= 0x0044127B - ImageBase && index < 0x00441280 - ImageBase) ||
                (index >= 0x004CDEDA - ImageBase && index < 0x004CDEDF - ImageBase) ||
                (index >= 0x004CDFDB - ImageBase && index < 0x004CDFE0 - ImageBase));
    }
    if (argc > 1)
    {
        std::ifstream file(argv[1], std::ios::binary);
        assert(file);
        std::vector<uint8_t> disk{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(disk.data() + Read<uint32_t>(disk.data(), 0x3C));
        assert(nt->FileHeader.TimeDateStamp == ImageTimestamp && nt->OptionalHeader.ImageBase == ImageBase && nt->OptionalHeader.SizeOfImage == ImageSize);
        image.assign(ImageSize, 0);
        std::memcpy(image.data(), disk.data(), nt->OptionalHeader.SizeOfHeaders);
        const auto sections = IMAGE_FIRST_SECTION(nt);
        for (size_t index = 0; index < nt->FileHeader.NumberOfSections; ++index)
            std::memcpy(image.data() + sections[index].VirtualAddress, disk.data() + sections[index].PointerToRawData, sections[index].SizeOfRawData);
        assert(ValidatePlan(image.data(), image.size(), plan));
        const auto objectPlan = NativeObjectRendering::BuildPlan(0x10000000,65536,0x10001000);
        assert(ValidatePlan(image.data(), image.size(), objectPlan));
        const auto original = image;
        ApplyValidatedPlan(image.data(), objectPlan);
        assert(ValidatePlan(image.data(), image.size(), plan)); // Object hooks/pool aliases do not conflict.
        ApplyValidatedPlan(image.data(), plan);
        for (const auto& patch : objectPlan)
            assert(std::memcmp(image.data() + patch.address - ImageBase, patch.replacement.data(), patch.replacement.size()) == 0);
        image = original;
        ReplayNativeScenery(image);
        std::printf("All %u scenery patch/guard sites match the supported executable.\n", static_cast<unsigned>(plan.size()));
    }
    std::puts("Scenery split, bounded grid, scoped restoration, prerequisites and atomic signature tests passed.");
}
