#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cassert>
#include <cstdio>
#include "../source/HighResolutionWindow.h"

namespace
{
    unsigned sizingCalls = 0;
    unsigned otherCalls = 0;

    LRESULT CALLBACK LegacyWindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_GETMINMAXINFO)
        {
            ++sizingCalls;
            auto limits = reinterpret_cast<MINMAXINFO*>(lParam);
            limits->ptMaxSize = { 640, 480 };
            limits->ptMaxTrackSize = limits->ptMaxSize;
            limits->ptMinTrackSize = { 32, 24 };
            return 17;
        }
        if (message == WM_APP)
        {
            ++otherCalls;
            return static_cast<LRESULT>(wParam + lParam);
        }
        return DefWindowProcA(window, message, wParam, lParam);
    }

    void CheckClientSize(HWND window, LONG width, LONG height)
    {
        RECT client{};
        assert(GetClientRect(window, &client));
        assert(client.right - client.left == width && client.bottom - client.top == height);
    }

    void CheckSizingLimits(HWND window, LONG width, LONG height)
    {
        MINMAXINFO limits{};
        const auto before = sizingCalls;
        assert(SendMessageA(window, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits)) == 17);
        assert(sizingCalls == before + 1);
        assert(limits.ptMaxSize.x == width && limits.ptMaxSize.y == height);
        assert(limits.ptMaxTrackSize.x == width && limits.ptMaxTrackSize.y == height);
        assert(limits.ptMinTrackSize.x == 32 && limits.ptMinTrackSize.y == 24);
    }
}

int main()
{
    // Hidden real HWNDs: this never shows a window, changes focus or changes a display mode.
    WNDCLASSA klass{};
    klass.hInstance = GetModuleHandleA(nullptr);
    klass.lpfnWndProc = LegacyWindowProcedure;
    klass.lpszClassName = "Toy2HighResolutionWindowTest";
    assert(RegisterClassA(&klass));
    HWND window = CreateWindowExA(0, klass.lpszClassName, "", WS_POPUP,
        0, 0, 640, 480, nullptr, nullptr, klass.hInstance, nullptr);
    assert(window != nullptr && !IsWindowVisible(window));
    CheckClientSize(window, 640, 480);
    CheckSizingLimits(window, 640, 480);

    assert(!HighResolutionWindow::Attach(nullptr, 2560, 1440));
    assert(!HighResolutionWindow::Attach(window, 0, 1440));
    assert(!HighResolutionWindow::Attach(window, 4097, 1440));
    assert(!HighResolutionWindow::Resize());
    assert(HighResolutionWindow::Attach(window, 2560, 1440));
    const auto original = HighResolutionWindow::state.originalProcedure;
    CheckSizingLimits(window, 2560, 1440);
    assert(HighResolutionWindow::Resize());
    CheckClientSize(window, 2560, 1440);
    assert(!IsWindowVisible(window));
    assert(SendMessageA(window, WM_APP, 123, 456) == 579 && otherCalls == 1);

    // Reattaching must update dimensions without recursively subclassing the window.
    const uint32_t sizes[][2] = {{1920,2160}, {3840,2160}, {2160,3840}, {4096,2160}};
    for (const auto& size : sizes)
    {
        assert(HighResolutionWindow::Attach(window, size[0], size[1]));
        assert(HighResolutionWindow::state.originalProcedure == original);
        CheckSizingLimits(window, size[0], size[1]);
        assert(HighResolutionWindow::Resize());
        CheckClientSize(window, size[0], size[1]);
    }

    HWND other = CreateWindowExA(0, klass.lpszClassName, "", WS_POPUP,
        0, 0, 640, 480, nullptr, nullptr, klass.hInstance, nullptr);
    assert(other != nullptr && !HighResolutionWindow::Attach(other, 2560, 1440));
    CheckSizingLimits(other, 640, 480);
    assert(DestroyWindow(other));
    assert(HighResolutionWindow::state.window == window);
    assert(DestroyWindow(window));
    assert(HighResolutionWindow::state.window == nullptr);
    assert(HighResolutionWindow::state.originalProcedure == nullptr);
    assert(!HighResolutionWindow::Resize());

    HWND windowed = CreateWindowExA(0, klass.lpszClassName, "", WS_OVERLAPPEDWINDOW,
        0, 0, 640, 480, nullptr, nullptr, klass.hInstance, nullptr);
    assert(windowed != nullptr && !HighResolutionWindow::Attach(windowed, 2560, 1440));
    assert(DestroyWindow(windowed));
    assert(UnregisterClassA(klass.lpszClassName, klass.hInstance));
    std::puts("Fullscreen window sizing, native message forwarding, repeated attachment and destruction tests passed.");
}
