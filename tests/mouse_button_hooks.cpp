#include "stdafx.h"
#include <cassert>
#include <cstdio>

namespace
{
    bool focused = true;
    bool fireDown = false;
    bool visorDown = false;
    DWORD ticks = 1000;

    HWND TestGetForegroundWindow()
    {
        return focused ? reinterpret_cast<HWND>(1) : nullptr;
    }

    DWORD TestGetWindowThreadProcessId(HWND, DWORD* processId)
    {
        *processId = GetCurrentProcessId();
        return 1;
    }

    SHORT TestGetAsyncKeyState(int key)
    {
        const bool down = key == VK_LBUTTON ? fireDown : visorDown;
        return down ? static_cast<SHORT>(0x8000) : 0;
    }

    DWORD TestGetTickCount()
    {
        ticks += 16;
        return ticks;
    }
}

// Exercise the real register hook and polling code without moving/clicking the desktop.
#define GetForegroundWindow TestGetForegroundWindow
#define GetWindowThreadProcessId TestGetWindowThreadProcessId
#define GetAsyncKeyState TestGetAsyncKeyState
#define GetTickCount TestGetTickCount
#include "../source/dllmain.cpp"
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#undef GetAsyncKeyState
#undef GetTickCount

int main()
{
    uint16_t polledActions = 0x10;
    g_pPolledRawInputActions = &polledActions;
    g_mouseButtons.enabled = true;
    g_mouseLook.enabled = false; // Mouse buttons are independent of mouse look.
    injector::reg_pack regs{};
    regs.ecx = 0xAABBFFFF;
    regs.eax = 123;
    regs.edx = 456;
    regs.ef = 0x246;
    uint16_t current = 0;
    unsigned visorPresses = 0;
    const auto frame = [&]()
    {
        const uint16_t previous = current;
        MouseButtonsHook{}(regs);
        current = static_cast<uint16_t>(regs.ecx);
        if ((current & MouseButtonActions::Visor) != 0 &&
            (previous & MouseButtonActions::Visor) == 0)
        {
            ++visorPresses;
        }
        assert((regs.ecx & 0xFFFF0000u) == 0xAABB0000);
        assert(regs.eax == 123 && regs.edx == 456 && regs.ef == 0x246);
        assert((current & polledActions) == polledActions);
    };

    frame();
    assert(current == 0x10);
    fireDown = true;
    visorDown = true;
    frame();
    assert(current == 0x8410 && visorPresses == 1);
    frame();
    assert(current == 0x8410 && visorPresses == 1);
    fireDown = false;
    visorDown = false;
    polledActions = 0x8410; // Native Ctrl/Tab still held when mouse releases.
    frame();
    assert(current == 0x8410 && visorPresses == 1 && polledActions == 0x8410);

    polledActions = 0x10;
    focused = false;
    fireDown = true;
    visorDown = true;
    frame();
    assert(current == 0x10);
    focused = true;
    frame();
    assert(current == 0x10 && visorPresses == 1);
    fireDown = false;
    visorDown = false;
    frame();
    fireDown = true;
    visorDown = true;
    frame();
    assert(current == 0x8410 && visorPresses == 2);

    ticks += 300; // Input polling suspended: don't turn an old held click into a new press.
    frame();
    assert(current == 0x10 && visorPresses == 2);
    fireDown = false;
    visorDown = false;
    frame();
    fireDown = true;
    visorDown = true;
    frame();
    assert(current == 0x8410 && visorPresses == 3);

    g_mouseButtons.enabled = false;
    frame();
    assert(current == 0x10);
    g_mouseButtons.enabled = true;
    frame();
    assert(current == 0x10 && visorPresses == 3);

    std::puts("Mouse-button hook replay, native snapshots, focus and independent enable tests passed.");
}
