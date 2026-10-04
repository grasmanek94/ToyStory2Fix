#pragma once

#include <windows.h>
#include <cstdint>
#include "HighResolutionLimits.h"

namespace HighResolutionWindow
{
    struct State
    {
        HWND window = nullptr;
        WNDPROC originalProcedure = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    inline State state;

    inline LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        const auto original = state.originalProcedure;
        const auto result = original != nullptr
            ? CallWindowProcA(original, window, message, wParam, lParam)
            : DefWindowProcA(window, message, wParam, lParam);

        if (window == state.window)
        {
            if (message == WM_GETMINMAXINFO && lParam != 0)
            {
                // The game's old window handler still uses its legacy 640x480 mode,
                // not the mode selected by the Direct3D6 resolution dialog.
                auto limits = reinterpret_cast<MINMAXINFO*>(lParam);
                limits->ptMaxSize = { static_cast<LONG>(state.width), static_cast<LONG>(state.height) };
                limits->ptMaxTrackSize = limits->ptMaxSize;
            }
            else if (message == WM_NCDESTROY)
            {
                state = {};
            }
        }
        return result;
    }

    inline bool Attach(HWND window, uint32_t width, uint32_t height)
    {
        if (window == nullptr || width == 0 || height == 0 ||
            width > HighResolutionLimits::RaisedLimit || height > HighResolutionLimits::RaisedLimit)
            return false;

        DWORD processId = 0;
        const auto threadId = GetWindowThreadProcessId(window, &processId);
        if (processId != GetCurrentProcessId() || threadId != GetCurrentThreadId() ||
            (GetWindowLongPtrA(window, GWL_STYLE) & WS_POPUP) == 0)
            return false;

        if (state.window == window)
        {
            state.width = width;
            state.height = height;
            return true;
        }
        if (state.window != nullptr)
            return false;

        const auto original = reinterpret_cast<WNDPROC>(GetWindowLongPtrA(window, GWLP_WNDPROC));
        if (original == nullptr)
            return false;

        state = { window, original, width, height };
        if (SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowProcedure)) == 0)
        {
            state = {};
            return false;
        }
        return true;
    }

    inline bool Resize()
    {
        if (state.window == nullptr)
            return false;
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        if (!GetMonitorInfoA(MonitorFromWindow(state.window, MONITOR_DEFAULTTONEAREST), &monitor))
            return false;

        // Native fullscreen DirectDraw may leave the original 640x480 popup intact.
        // Size it before cooperative-level setup, and again after the display mode changes.
        // Preserve the game's existing z-order, focus, styles and native window procedure.
        return SetWindowPos(state.window, nullptr, monitor.rcMonitor.left, monitor.rcMonitor.top,
            static_cast<int>(state.width), static_cast<int>(state.height),
            SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED) != FALSE;
    }
}
