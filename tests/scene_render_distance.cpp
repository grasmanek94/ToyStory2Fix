#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../source/SceneRenderDistance.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cfloat>
#include <cmath>

int main()
{
    // Replay the recovered native x86 distance setter, not a C++ approximation.
    // Only the six-byte FSTP is replaced; the second threshold must still be stored.
    uint8_t code[] = {
        0xD9,0x44,0x24,0x04,0xD8,0x4C,0x24,0x04,0xD9,0x1D,0,0,0,0,
        0xD9,0x44,0x24,0x08,0xD8,0x4C,0x24,0x08,0xD9,0x1D,0,0,0,0,0xC3
    };
    float threshold = std::sqrt(FLT_MAX), secondThreshold = 0;
    const auto first = reinterpret_cast<uintptr_t>(&threshold);
    const auto second = reinterpret_cast<uintptr_t>(&secondThreshold);
    static_assert(sizeof(first) == 4, "Build this native replay test for x86.");
    std::memcpy(code + 10, &first, 4);
    std::memcpy(code + 24, &second, 4);
    std::memcpy(code + 8, SceneRenderDistance::PreserveThresholdStore.data(), 6);
    auto executable = VirtualAlloc(nullptr, sizeof(code), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    assert(executable != nullptr);
    std::memcpy(executable, code, sizeof(code));
    DWORD previous = 0;
    assert(VirtualProtect(executable, sizeof(code), PAGE_EXECUTE_READ, &previous));
    FlushInstructionCache(GetCurrentProcess(), executable, sizeof(code));
    const auto setter = reinterpret_cast<void(__cdecl*)(float,float)>(executable);
    unsigned short before = 0, after = 0;
    float callerValue = 0;
    _asm fld1 // A live caller x87 value must survive, not be reset with FINIT.
    _asm fnstsw before
    for (int i = 0; i < 512; ++i)
        setter(12000.0f, 250.0f);
    _asm fnstsw after
    _asm fstp callerValue
    assert((before & 0x3800) == (after & 0x3800)); // Identical x87 TOP.
    assert((after & 0x41) == 0); // No stack fault or invalid-operation exception.
    assert(callerValue == 1.0f);
    assert(threshold == std::sqrt(FLT_MAX) && secondThreshold == 62500.0f);
    VirtualFree(executable, 0, MEM_RELEASE);
    std::puts("Native distance-setter replay: 512 calls preserve x87 stack depth, live values and the second threshold.");
}
