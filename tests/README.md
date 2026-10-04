# Input and high-resolution regression tests

From the repository root in an **x86 Visual Studio Developer PowerShell**:

```powershell
New-Item -ItemType Directory -Force build/tests | Out-Null

cl /nologo /std:c++17 /EHsc /W4 /WX /MT tests/mouse_look_camera.cpp /Fobuild/tests/mouse_look_camera.obj /Febuild/tests/mouse_look_camera.exe
./build/tests/mouse_look_camera.exe

cl /nologo /std:c++17 /EHsc /W4 /WX /MT tests/mouse_button_actions.cpp /Fobuild/tests/mouse_button_actions.obj /Febuild/tests/mouse_button_actions.exe
./build/tests/mouse_button_actions.exe

cl /nologo /std:c++latest /EHsc /W3 /WX /MT /Iincludes /Iexternal/hooking /Iexternal/injector/include /Iexternal/inireader tests/mouse_button_hooks.cpp includes/stdafx.cpp external/hooking/Hooking.Patterns.cpp /Fobuild/tests/ /Febuild/tests/mouse_button_hooks.exe /link winmm.lib user32.lib
./build/tests/mouse_button_hooks.exe

cl /nologo /std:c++latest /EHsc /W3 /WX /MT /Iincludes /Iexternal/hooking /Iexternal/injector/include /Iexternal/inireader tests/high_resolution_hooks.cpp includes/stdafx.cpp external/hooking/Hooking.Patterns.cpp /Fobuild/tests/ /Febuild/tests/high_resolution_hooks.exe /link winmm.lib user32.lib
./build/tests/high_resolution_hooks.exe

cl /nologo /std:c++17 /EHsc /W4 /WX /MT tests/high_resolution_window.cpp /Fobuild/tests/high_resolution_window.obj /Febuild/tests/high_resolution_window.exe /link user32.lib
./build/tests/high_resolution_window.exe

cl /nologo /std:c++17 /EHsc /W3 /WX /MT /Iincludes /Iexternal/hooking /Iexternal/injector/include /Iexternal/inireader tests/native_d3d_resolution.cpp includes/stdafx.cpp external/hooking/Hooking.Patterns.cpp /Fobuild/tests/ /Febuild/tests/native_d3d_resolution.exe /link ddraw.lib dxguid.lib user32.lib
./build/tests/native_d3d_resolution.exe
```

The tests cover camera-only orbit angles, keyboard camera priority, pitch collision corrections, native fire/visor hold and press-edge behavior, keyboard coexistence, focus loss and refocus, and register replay. The button hook test replaces focus/button APIs with deterministic samples; it does not click or move the desktop cursor.

In-game checks are still required for camera feel, left-click firing/charging, right-click visor entry/exit (including holding right click through transitions), Ctrl/Tab coexistence, and Alt-Tab while buttons are held.

The high-resolution hook test verifies viewport-value reads, projection-field/register isolation, read-only surface diagnostic queries (including error/null paths), graphics-wrapper passthrough and controlled initialization failure without displaying an error dialog or exiting the test runner.

The fullscreen window test creates hidden popup windows with the game's legacy 640x480 sizing limits. It verifies selected-resolution client sizes and maximum-size limits, forwarding of native messages, repeated attachment without recursion, cleanup on destruction, and rejection of non-popup windows. It does not show windows, change focus, or change the desktop resolution.

The native integration test requires Windows with the matching native `d3dim.dll` and a working hardware HAL. It uses hidden offscreen surfaces, checks the 2048/2049 boundary on each axis, then verifies actual rendered pixels at larger dimensions (including 2560x1440, 3840x2160, 5120x2880, 7680x4320 and portrait equivalents), patch idempotency, and the retained 8192/8193 ceiling on both axes. It never changes the desktop resolution or Windows DLL files. A replaced native renderer is reported as skipped; unsupported native versions or driver capabilities can cause this environment-dependent test to fail.

In-game resolution checks: 640x480, 1920x1080, 1920x1440, 2560x1440, a supported mode with height above 2048, 3840x2160, 5120x2880 and 7680x4320 where the display/DSR configuration supports them; include startup screens, menus, gameplay, visor aiming, FMVs, focus loss and shutdown.

For high-resolution/DSR tests, set the Windows desktop to the selected game resolution **before launching the executable**, then verify the full image is visible. Restore the ordinary desktop mode manually after the test if desired. Automatic correction of clipping from a smaller starting desktop is not implemented.
