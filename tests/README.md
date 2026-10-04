# Input, rendering and high-resolution regression tests

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

cl /nologo /std:c++17 /EHsc /W4 /WX /MT tests/object_draw_distance.cpp /Fobuild/tests/object_draw_distance.obj /Febuild/tests/object_draw_distance.exe
./build/tests/object_draw_distance.exe

cl /nologo /std:c++17 /EHsc /W4 /WX /MT tests/native_object_rendering.cpp /Fobuild/tests/native_object_rendering.obj /Febuild/tests/native_object_rendering.exe
./build/tests/native_object_rendering.exe
# Optional executable integration/replay check (use your local game path):
./build/tests/native_object_rendering.exe "C:/Games/Toy Story 2/toy2.exe"

cl /nologo /std:c++17 /EHsc /W4 /WX /MT tests/scene_render_distance.cpp /Fobuild/tests/scene_render_distance.obj /Febuild/tests/scene_render_distance.exe
./build/tests/scene_render_distance.exe
```

The tests cover camera-only orbit angles, keyboard camera priority, pitch collision corrections, native fire/visor hold and press-edge behavior, keyboard coexistence, focus loss and refocus, and register replay. The button hook test replaces focus/button APIs with deterministic samples; it does not click or move the desktop cursor.

In-game checks are still required for camera feel, left-click firing/charging, right-click visor entry/exit (including holding right click through transitions), Ctrl/Tab coexistence, and Alt-Tab while buttons are held.

The high-resolution hook test verifies viewport-value reads, projection-field/register isolation, read-only surface diagnostic queries (including error/null paths), graphics-wrapper passthrough and controlled initialization failure without displaying an error dialog or exiting the test runner.

The fullscreen window test creates hidden popup windows with the game's legacy 640x480 sizing limits. It verifies selected-resolution client sizes and maximum-size limits, forwarding of native messages, repeated attachment without recursion, cleanup on destruction, and rejection of non-popup windows. It does not show windows, change focus, or change the desktop resolution.

The native integration test requires Windows with the matching native `d3dim.dll` and a working hardware HAL. It uses hidden offscreen surfaces, checks the 2048/2049 boundary on each axis, then verifies actual rendered pixels at larger dimensions (including 2560x1440, 3840x2160, 5120x2880, 7680x4320 and portrait equivalents), patch idempotency, and the retained 8192/8193 ceiling on both axes. It never changes the desktop resolution or Windows DLL files. A replaced native renderer is reported as skipped; unsupported native versions or driver capabilities can cause this environment-dependent test to fail.

In-game resolution checks: 640x480, 1920x1080, 1920x1440, 2560x1440, a supported mode with height above 2048, 3840x2160, 5120x2880 and 7680x4320 where the display/DSR configuration supports them; include startup screens, menus, gameplay, visor aiming, FMVs, focus loss and shutdown.

For high-resolution/DSR tests, set the Windows desktop to the selected game resolution **before launching the executable**, then verify the full image is visible. Restore the ordinary desktop mode manually after the test if desired. Automatic correction of clipping from a smaller starting desktop is not implemented.

## Object distances and pool capacity

See [the recovered rendering paths and capacity notes](object_rendering_notes.md) for executable fingerprints, function/global addresses and the reasons for keeping gameplay activation and fade tables unchanged.

`object_draw_distance` tests finite-value sanitization, overflow-safe distance math, all 64 actor slots plus Buzz and the terminator, native list order, hide/live/model-ready checks, reentrancy rejection and exact list/visibility restoration. Unrelated gameplay flags remain untouched, including writes made during rendering.

`native_object_rendering` tests every patch/guard mismatch, all-or-nothing read-only validation, bounded expanded pools and the deliberately unmodified bucket-end alias. With a `toy2.exe` argument, it validates all native sites and audits **every PE-relocated pool pointer**, including references in functions not recovered by initial analysis. It then copies the executable into private memory and replays all six actual sprite allocators with mocked texture/queue/plane consumers: 16384 records, exhaustion rejection, reset counters, old-buffer preservation and boundary canaries. It does not launch the game, resolve imports, alter files, show windows or change desktop resolution.

`scene_render_distance` replays the recovered native distance setter for 512 calls, with a live caller x87 value, and verifies stack depth, exception flags, the overridden geometry threshold and the original second threshold. Build all native replay tests as **x86**.

In-game checks are still required:

1. Compare the same camera positions with `IncreaseObjectRenderDistance = false` and `true`, restarting between changes. Check bedroom coins/lamp/doorway objects, a large outdoor level, enemies and pickups. Confirm the log reports installation rather than a safe skip.
2. Sweep between rooms/portals and rotate the camera; no extra objects should draw through native hidden rooms. Verify Buzz, animation, death/respawn and level transitions.
3. Verify coin collection, pickup prompts, target lock, keyboard/controller movement, left-click fire, right-click visor, mouse orbit and timing are unchanged. Distant unloaded actors are intentionally not activated.
4. Include menus, loading, cutscenes/FMV, pause, Alt-Tab and shutdown. Keep the established desktop-before-launch setup for high-resolution modes; automatic fullscreen-clipping work remains paused.
