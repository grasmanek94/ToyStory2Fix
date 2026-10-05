#include "../source/NativeMoleHoleSmoke.h"
#include "../source/NativeSceneryRendering.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace
{
    template <typename T> T Read(const uint8_t* bytes, size_t offset)
    {
        T value;
        std::memcpy(&value, bytes + offset, sizeof(value));
        return value;
    }
    template <typename T> void Write(uint8_t* bytes, size_t offset, T value)
    {
        std::memcpy(bytes + offset, &value, sizeof(value));
    }
    size_t Count(const MoleHoleSmoke::Pool& pool)
    {
        return static_cast<size_t>(std::count_if(pool.particles.begin(), pool.particles.end(),
            [](const auto& particle) { return particle.life > 0; }));
    }
    MoleHoleSmoke::HoleList MakeHoles()
    {
        return {7, 0, {{{-7500,0,-11500}, {-13500,INT32_MIN,-11500}, {-9980,INT32_MIN,-8969},
            {-9500,INT32_MIN,-4860}, {-12500,0,-5500}, {-13500,0,-2500}, {-8500,0,-1500}}}};
    }
    void TestPool()
    {
        using namespace MoleHoleSmoke;
        assert(DistanceLimit(INFINITY, 48000) == 48000);
        assert(DistanceLimit(2000, 48000) == NativeDistance);
        assert(DistanceLimit(20000, 16000) == 16000);
        assert(DistanceLimit(65536, NAN) == NativeDistance);
        assert(CullSquare(65536) == 36000000);
        assert(CullSquare(1024) == NativeDistanceSquared);
        assert(FloorShift8(-1) == -1 && FloorShift8(-256) == -1 && FloorShift8(-257) == -2);
        int32_t camera[]{-399582,-25332,-227000}; // User's paused nearby snapshot.
        auto holes = MakeHoles();
        Pool pool;
        assert(pool.Sync(2, &holes, &holes));
        assert(EmissionSquare(-13500*32,-11500*32,camera) >= NativeDistanceSquared);
        assert(EmissionSquare(-9980*32,-8969*32,camera) < NativeDistanceSquared);
        assert(pool.Emit(-13500*32,Height,-11500*32,camera,48000));
        assert(!pool.Emit(-9980*32,Height,-8969*32,camera,48000)); // Native owns near smoke.
        assert(!pool.Emit(-7500*32,Height,-11500*32,camera,48000)); // Uncompleted hole.
        assert(!pool.Emit(-13500*32,Height+1,-11500*32,camera,48000));
        assert(!pool.Emit(0,Height,0,camera,48000)); // Unknown source.
        assert(!pool.Emit(-13500*32,Height,-11500*32,camera,4096));
        const auto random = pool.random;
        const auto particle = pool.particles[0];
        pool.Tick(0);
        pool.Tick(-1);
        assert(pool.particles[0].life == Lifetime && pool.random == random);
        pool.Tick(17);
        assert(pool.particles[0].life == 31 && pool.particles[0].size == 144);
        assert(pool.particles[0].y == Height - 256 * 17);
        assert(pool.particles[0].x == particle.x + particle.vx * 17);
        assert(pool.particles[0].Color() == 0xFF5D4D3E);
        pool.Tick(INT32_MAX);
        assert(Count(pool) == 0);
        for (size_t i = 0; i < Capacity; ++i)
            assert(pool.Emit(-13500*32,Height,-11500*32,camera,48000));
        const auto fullRandom = pool.random;
        assert(!pool.Emit(-13500*32,Height,-11500*32,camera,48000));
        assert(pool.random == fullRandom && Count(pool) == Capacity);
        holes.holes[1].y = 0;
        assert(pool.Sync(2,&holes,&holes) && Count(pool) == 0);
        holes.holes[1].y = INT32_MIN;
        assert(pool.Emit(-13500*32,Height,-11500*32,camera,48000));
        assert(pool.Sync(2,&holes,&pool) && Count(pool) == 0); // New world.
        holes.count = 8;
        assert(!pool.Sync(2,&holes,&holes));
        holes.count = 7;
        assert(!pool.Sync(1,&holes,&holes));
        assert(!pool.Sync(2,nullptr,&holes));
        assert(!pool.Sync(2,&holes,nullptr));
        int32_t extreme[]{INT32_MIN,INT32_MAX,INT32_MIN};
        assert(EmissionSquare(INT32_MAX,INT32_MAX,extreme) > NativeDistanceSquared);
        assert(!WithinRange(INT32_MAX,INT32_MAX,INT32_MAX,extreme,48000));
        assert(!WithinRange(0,0,0,nullptr,48000));
        assert(!WithinRange(0,0,0,camera,INFINITY));
    }

    int level = 2, ticks = 1, nearCalls = 0, updateCalls = 0, renderCalls = 0, queued = 0;
    int32_t camera[3]{};
    float projectionFar = 48000;
    auto holes = MakeHoles();
    const MoleHoleSmoke::HoleList* holePointer = &holes;
    const void* world = &holes;
    uint8_t descriptor[]{31,0,31,31,0,0,0,0,192,96};
    const uint8_t* sprite = descriptor;
    bool missingTexture = false, invalidSize = false;
    void* __cdecl NativeSpawn(int32_t, int32_t, int32_t, int type, int motion)
    {
        assert(type == 0x3A && motion == 3);
        ++nearCalls;
        return &nearCalls;
    }
    void __cdecl NativeUpdate() { ++updateCalls; }
    void __cdecl NativeRender() { ++renderCalls; }
    int __cdecl ResolveTexture(int id) { assert(id == 31); return missingTexture ? 0 : 42; }
    void __cdecl TextureDimensions(int id, int* width, int* height, void* a, void* b, void* c)
    {
        assert(id == 42 && a == nullptr && b == nullptr && c == nullptr);
        *width = invalidSize ? 0 : 256;
        *height = 256;
    }
    void __cdecl Queue(const float* position, int angle, float width, float height,
        const float* uv, const float* end, int texture, uint32_t color, uint32_t flags)
    {
        assert(std::isfinite(position[0]) && std::isfinite(position[1]) && std::isfinite(position[2]));
        assert(angle == 0 && width >= 110 && width == height && width <= 204);
        assert(uv[0] == 192.0f/256 && uv[1] == 96.0f/256);
        assert(end[0] == 223.0f/256 && end[1] == 127.0f/256);
        assert(texture == 42 && (color >> 24) == 255 && flags == 0x4840);
        ++queued;
    }
    void Bind()
    {
        using namespace NativeMoleHoleSmoke;
        bindings = {&level,&ticks,camera,&holePointer,&world,&projectionFar,&sprite,
            NativeSpawn,NativeUpdate,NativeRender,ResolveTexture,TextureDimensions,Queue};
        installed = true;
        rendering = false;
        pool.Clear();
        NativeObjectRendering::drawDistance = 65536;
        level = 2;
        holes = MakeHoles();
        camera[0] = -399582; camera[1] = -25332; camera[2] = -227000;
        nearCalls = updateCalls = renderCalls = queued = 0;
    }
    void TestHooks()
    {
        using namespace NativeMoleHoleSmoke;
        Bind();
        assert(SpawnExtendedSmoke(-9980*32,MoleHoleSmoke::Height,-8969*32,0x3A,3) == &nearCalls);
        assert(nearCalls == 1 && Count(pool) == 0);
        assert(SpawnExtendedSmoke(-13500*32,MoleHoleSmoke::Height,-11500*32,0x3A,3) == nullptr);
        assert(nearCalls == 1 && Count(pool) == 1);
        const auto random = pool.random;
        UpdateWithExtendedSmoke();
        assert(updateCalls == 1 && pool.particles[0].life == 47 && pool.random == random);
        RenderWithExtendedSmoke();
        assert(renderCalls == 1 && queued == 1 && pool.particles[0].life == 47);
        missingTexture = true;
        RenderWithExtendedSmoke();
        missingTexture = false;
        invalidSize = true;
        RenderWithExtendedSmoke();
        invalidSize = false;
        sprite = nullptr;
        RenderWithExtendedSmoke();
        sprite = descriptor;
        assert(queued == 1 && !rendering);
        projectionFar = 2000;
        RenderWithExtendedSmoke();
        projectionFar = 48000;
        assert(queued == 1);
        level = 1;
        holePointer = reinterpret_cast<const MoleHoleSmoke::HoleList*>(1); // Stale level-specific pointer is never read.
        UpdateWithExtendedSmoke();
        RenderWithExtendedSmoke();
        assert(Count(pool) == 0 && nearCalls == 1);
        holePointer = &holes;
        installed = false;
        assert(Install(nullptr) == Status::PoolsUnavailable);
    }

    void TestManifest()
    {
        using namespace NativeObjectRendering;
        const auto plan = NativeMoleHoleSmoke::BuildPlan(65536,0x10000000,0x10001000,0x10002000);
        std::vector<uint8_t> image(ImageSize);
        Write(image.data(),0,static_cast<uint16_t>(IMAGE_DOS_SIGNATURE));
        Write(image.data(),0x3C,uint32_t{0x100});
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(image.data()+0x100);
        nt->Signature = IMAGE_NT_SIGNATURE;
        nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
        nt->FileHeader.TimeDateStamp = ImageTimestamp;
        nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
        nt->OptionalHeader.ImageBase = ImageBase;
        nt->OptionalHeader.SizeOfImage = ImageSize;
        for (const auto& patch : plan)
            std::memcpy(image.data()+patch.address-ImageBase,patch.expected.data(),patch.expected.size());
        assert(ValidatePlan(image.data(),image.size(),plan));
        for (const auto& patch : plan)
        {
            image[patch.address-ImageBase] ^= 1;
            const auto before = image;
            assert(!ValidatePlan(image.data(),image.size(),plan) && image == before);
            image[patch.address-ImageBase] ^= 1;
        }
        const auto before = image;
        ApplyValidatedPlan(image.data(),plan);
        size_t changed = 0;
        for (size_t i = 0; i < image.size(); ++i)
            changed += image[i] != before[i];
        assert(changed <= 19); // One operand and three relative calls only.
    }

    uint8_t* replayImage = nullptr;
    uintptr_t replayEmitter = 0;
    uintptr_t replayHolePointer = 0;
    __declspec(naked) void __cdecl ReplayEmitter()
    {
        __asm {
            push ebp
            push ebx
            push esi
            push edi
            sub esp, 24
            mov ecx, replayHolePointer
            mov ebx, 80000000h
            call replayEmitter
            add esp, 24
            pop edi
            pop esi
            pop ebx
            pop ebp
            ret
        }
    }
    void Replay(const std::vector<uint8_t>& image)
    {
        using namespace NativeObjectRendering;
        const auto effects = NativeMoleHoleSmoke::BuildPlan(65536,0x10000000,0x10001000,0x10002000);
        assert(ValidatePlan(image.data(),image.size(),effects));
        auto combined = image;
        ApplyValidatedPlan(combined.data(),BuildPlan(0x10000000,65536,0x10001000));
        const auto scenery = NativeSceneryRendering::BuildPlan(0x10002000,0x10003000);
        assert(ValidatePlan(combined.data(),combined.size(),scenery));
        ApplyValidatedPlan(combined.data(),scenery);
        assert(ValidatePlan(combined.data(),combined.size(),effects));

        replayImage = static_cast<uint8_t*>(VirtualAlloc(nullptr,ImageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        assert(replayImage != nullptr);
        std::memcpy(replayImage,image.data(),image.size());
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image.data()+Read<uint32_t>(image.data(),0x3C));
        const auto reloc = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        const auto delta = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(replayImage))-ImageBase;
        for (size_t cursor=reloc.VirtualAddress; cursor<reloc.VirtualAddress+reloc.Size; )
        {
            const auto page=Read<uint32_t>(image.data(),cursor), block=Read<uint32_t>(image.data(),cursor+4);
            assert(block >= 8);
            for (size_t i=0; i<(block-8)/2; ++i)
            {
                const auto item=Read<uint16_t>(image.data(),cursor+8+i*2);
                if ((item>>12)==IMAGE_REL_BASED_HIGHLOW)
                {
                    const auto rva=page+(item&0xFFF);
                    Write(replayImage,rva,Read<uint32_t>(image.data(),rva)+delta);
                }
            }
            cursor+=block;
        }
        Bind();
        replayHolePointer = reinterpret_cast<uintptr_t>(&holes);
        replayEmitter = reinterpret_cast<uintptr_t>(replayImage+0x19696);
        Write(replayImage,0x00559C78-ImageBase,&holes);
        Write(replayImage,0x0052F2D4-ImageBase,7);
        std::memcpy(replayImage+0x0052ADC0-ImageBase,camera,sizeof(camera));
        // Stop at the exact native emitter-loop boundary, before unrelated level AI.
        replayImage[0x19782] = 0xC3;
        auto redirect = [&](uint32_t address, uintptr_t callback) {
            auto code=replayImage+address-ImageBase;
            code[0]=0xE9;
            Write(code,1,static_cast<uint32_t>(callback-reinterpret_cast<uintptr_t>(code)-5));
        };
        redirect(0x0040FDF0,reinterpret_cast<uintptr_t>(&NativeSpawn));
        DWORD previous;
        assert(VirtualProtect(replayImage+0x1000,0xDB000,PAGE_EXECUTE_READWRITE,&previous));
        FlushInstructionCache(GetCurrentProcess(),replayImage+0x1000,0xDB000);
        ReplayEmitter();
        assert(nearCalls == 2 && Count(NativeMoleHoleSmoke::pool) == 0);
        const auto plan=NativeMoleHoleSmoke::BuildPlan(65536,
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&NativeMoleHoleSmoke::SpawnExtendedSmoke))-delta,
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&NativeMoleHoleSmoke::UpdateWithExtendedSmoke))-delta,
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&NativeMoleHoleSmoke::RenderWithExtendedSmoke))-delta);
        ApplyValidatedPlan(replayImage,plan);
        FlushInstructionCache(GetCurrentProcess(),replayImage+0x1000,0xDB000);
        nearCalls=0;
        ReplayEmitter();
        assert(nearCalls == 2 && Count(NativeMoleHoleSmoke::pool) == 1);
        NativeMoleHoleSmoke::pool.Clear();
        camera[0]=-450604; camera[1]=-29336; camera[2]=-98906; // User's far snapshot.
        std::memcpy(replayImage+0x0052ADC0-ImageBase,camera,sizeof(camera));
        nearCalls=0;
        ReplayEmitter();
        assert(nearCalls == 0 && Count(NativeMoleHoleSmoke::pool) == 3);
        assert(Read<int>(replayImage,0x0052F6D4-ImageBase)==0); // Same seven-entry cadence.
        const auto nativePoolBefore=std::vector<uint8_t>(replayImage+0x129E58,replayImage+0x12AD58);
        for (int frame=0; frame<512; ++frame)
        {
            NativeMoleHoleSmoke::pool.Tick(7);
            const double sentinel=1.23456789012345;
            double result=0;
            __asm fld sentinel
            ReplayEmitter();
            __asm fstp result
            assert(result==sentinel);
            assert(Count(NativeMoleHoleSmoke::pool)<=MoleHoleSmoke::Capacity);
        }
        assert(std::memcmp(replayImage+0x129E58,nativePoolBefore.data(),nativePoolBefore.size())==0);
        assert(VirtualFree(replayImage,0,MEM_RELEASE));
        replayImage=nullptr;
        std::puts("Native mole emitter replay: 2 near + 1 extra; 3 extra at far position; native cadence/pool retained; 512 calls preserve live x87 value.");
    }
}

int main(int argc, char** argv)
{
    TestPool();
    TestHooks();
    TestManifest();
    if (argc > 1)
    {
        std::ifstream file(argv[1],std::ios::binary);
        assert(file);
        std::vector<uint8_t> disk{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
        assert(disk.size()>0x100 && Read<uint16_t>(disk.data(),0)==IMAGE_DOS_SIGNATURE);
        const auto ntOffset=Read<uint32_t>(disk.data(),0x3C);
        assert(ntOffset+sizeof(IMAGE_NT_HEADERS32)<disk.size());
        const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(disk.data()+ntOffset);
        assert(nt->OptionalHeader.SizeOfImage==NativeObjectRendering::ImageSize);
        std::vector<uint8_t> image(nt->OptionalHeader.SizeOfImage);
        std::memcpy(image.data(),disk.data(),nt->OptionalHeader.SizeOfHeaders);
        const auto sections=IMAGE_FIRST_SECTION(nt);
        for (size_t i=0; i<nt->FileHeader.NumberOfSections; ++i)
        {
            assert(static_cast<size_t>(sections[i].PointerToRawData)+sections[i].SizeOfRawData<=disk.size());
            assert(static_cast<size_t>(sections[i].VirtualAddress)+sections[i].SizeOfRawData<=image.size());
            std::memcpy(image.data()+sections[i].VirtualAddress,disk.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
        }
        Replay(image);
    }
    std::puts("Mole-hole smoke: finite range, isolated capacity, native near passthrough, lifetime/pause/transition safety, signatures and hook forwarding passed.");
    return 0;
}
