#include "stdafx.h"
#include <ddraw.h>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace
{
    int backendResult = 0;
    unsigned backendCalls = 0;
    unsigned messages = 0;
    std::string lastMessage;
    HWND expectedWindow = reinterpret_cast<HWND>(1);
    void* expectedDrawInfo = reinterpret_cast<void*>(2);
    const GUID* expectedDevice = nullptr;
    const DDSURFACEDESC2* expectedMode = nullptr;
    uint32_t expectedFlags = 0;

    HMODULE TestGetModuleHandleA(const char* name)
    {
        if (std::strcmp(name, "d3dim.dll") == 0)
            return nullptr; // Simulate a graphics wrapper, without patching any DLL.
        return GetModuleHandleA(name);
    }

    int TestMessageBoxA(HWND window, const char* message, const char*, UINT)
    {
        assert(window == expectedWindow);
        ++messages;
        lastMessage = message;
        return IDOK;
    }

    [[noreturn]] void TestExitProcess(UINT code)
    {
        throw code; // Test controlled startup failure without terminating the test runner.
    }

    int __cdecl TestInitialize(HWND window, void* drawInfo, const GUID* device,
        const DDSURFACEDESC2* mode, uint32_t flags)
    {
        assert(window == expectedWindow && drawInfo == expectedDrawInfo);
        assert(device == expectedDevice && mode == expectedMode && flags == expectedFlags);
        ++backendCalls;
        return backendResult;
    }
}

#define GetModuleHandleA TestGetModuleHandleA
#define MessageBoxA TestMessageBoxA
#define ExitProcess TestExitProcess
#include "../source/dllmain.cpp"
#undef GetModuleHandleA
#undef MessageBoxA
#undef ExitProcess

int main()
{
    const uint32_t sizes[][2] = {{1920,1080}, {2560,1440}, {1920,2160}, {3840,2160}};
    for (const auto& size : sizes)
    {
        UpdateWidescreenDimensions(size);
        assert(Variables.nWidth == size[0] && Variables.nHeight == size[1]);
        assert(std::fabs(Variables.fAspectRatio - static_cast<float>(size[0]) / size[1]) < 0.00001f);
    }
    const uint32_t zeroHeight[2] = {2560, 0};
    UpdateWidescreenDimensions(zeroHeight);
    assert(std::isfinite(Variables.fScaleValue) && Variables.fScaleValue == 0.75f);

    const uint32_t normal[2] = {1920,1080};
    UpdateWidescreenDimensions(normal);
    alignas(4) uint8_t camera[0x180];
    std::memset(camera, 0xA5, sizeof(camera));
    injector::reg_pack regs{};
    regs.eax = reinterpret_cast<uint32_t>(camera);
    regs.ef = 0x246;
    WidescreenProjectionHook{}(regs);
    float aspect = 0;
    std::memcpy(&aspect, camera + 0x44, sizeof(aspect));
    assert(aspect == 0.5625f);
    for (size_t i = 0; i < sizeof(camera); ++i)
        assert((i >= 0x44 && i < 0x48) || camera[i] == 0xA5);
    assert(regs.eax == reinterpret_cast<uint32_t>(camera) && regs.ef == 0x246);

    const GUID hardware = NativeD3DResolution::HalDeviceGuid;
    const GUID software{};
    assert(NativeD3DResolution::IsHalDevice(&hardware));
    assert(!NativeD3DResolution::IsHalDevice(&software));
    assert(!NativeD3DResolution::IsHalDevice(nullptr));
    assert(NativeD3DResolution::RaiseLimit(nullptr).status == NativeD3DResolution::Status::ModuleMissing);

    sub_InitializeDisplay_addr = reinterpret_cast<uintptr_t>(&TestInitialize);
    DDSURFACEDESC2 mode{};
    mode.dwSize = sizeof(mode);
    mode.dwWidth = 2560;
    mode.dwHeight = 1440;
    expectedDevice = &hardware;
    expectedMode = &mode;
    expectedFlags = 7;
    assert(InitializeDisplayWithHighResolutionSupport(expectedWindow, expectedDrawInfo,
        expectedDevice, expectedMode, expectedFlags) == 0);
    assert(backendCalls == 1 && messages == 0);

    // Preserve successful nonzero results and the software-renderer path unchanged.
    expectedDevice = &software;
    backendResult = 1;
    assert(InitializeDisplayWithHighResolutionSupport(expectedWindow, expectedDrawInfo,
        expectedDevice, expectedMode, expectedFlags) == 1);
    assert(backendCalls == 2 && messages == 0);

    // An initialization error must not return to the original invalid-context path.
    backendResult = static_cast<int>(0x82000004u);
    bool exited = false;
    try
    {
        InitializeDisplayWithHighResolutionSupport(expectedWindow, expectedDrawInfo,
            expectedDevice, expectedMode, expectedFlags);
    }
    catch (UINT code)
    {
        exited = code == 1;
    }
    assert(exited && backendCalls == 3 && messages == 1);
    assert(lastMessage.find("2560x1440") != std::string::npos);
    assert(lastMessage.find("82000004") != std::string::npos);
    assert(lastMessage.find("4096") != std::string::npos);
    std::puts("Resolution dimensions, projection memory/register isolation and safe startup failure tests passed.");
}
