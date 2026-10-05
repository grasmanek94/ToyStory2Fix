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

cl /nologo /std:c++17 /EHsc /W4 /WX /MT tests/native_scenery_rendering.cpp /Fobuild/tests/native_scenery_rendering.obj /Febuild/tests/native_scenery_rendering.exe
./build/tests/native_scenery_rendering.exe
# Optional native world/grid replay in a private executable copy:
./build/tests/native_scenery_rendering.exe "C:/Games/Toy Story 2/toy2.exe"

cl /nologo /std:c++17 /EHsc /W4 /WX /MT tests/native_mole_hole_smoke.cpp /Fobuild/tests/native_mole_hole_smoke.obj /Febuild/tests/native_mole_hole_smoke.exe
./build/tests/native_mole_hole_smoke.exe
# Optional native emitter replay in a private executable copy:
./build/tests/native_mole_hole_smoke.exe "C:/Games/Toy Story 2/toy2.exe"
# Repeat with Release optimization and assertions retained:
cl /nologo /std:c++17 /O2 /EHsc /W4 /WX /MT tests/native_mole_hole_smoke.cpp /Fobuild/tests/native_mole_hole_smoke_optimized.obj /Febuild/tests/native_mole_hole_smoke_optimized.exe
./build/tests/native_mole_hole_smoke_optimized.exe "C:/Games/Toy Story 2/toy2.exe"

cl /nologo /std:c++17 /EHsc /W4 /WX /MT tests/scene_render_distance.cpp /Fobuild/tests/scene_render_distance.obj /Febuild/tests/scene_render_distance.exe
./build/tests/scene_render_distance.exe

cl /nologo /std:c++17 /EHsc /W4 /WX /MT tests/alt_tab_recovery.cpp /Fobuild/tests/alt_tab_recovery.obj /Febuild/tests/alt_tab_recovery.exe
./build/tests/alt_tab_recovery.exe

cl /nologo /std:c++latest /EHsc /W3 /WX /MT /Iincludes /Iexternal/hooking /Iexternal/injector/include /Iexternal/inireader tests/alt_tab_hooks.cpp includes/stdafx.cpp external/hooking/Hooking.Patterns.cpp /Fobuild/tests/ /Febuild/tests/alt_tab_hooks.exe /link winmm.lib user32.lib
./build/tests/alt_tab_hooks.exe
# Optional read-only executable signature/patch check (use your local game path):
./build/tests/alt_tab_hooks.exe "C:/Games/Toy Story 2/toy2.exe"
```

The tests cover camera-only orbit angles, keyboard camera priority, pitch collision corrections, native fire/visor hold and press-edge behavior, keyboard coexistence, focus loss and refocus, and register replay. The button hook test replaces focus/button APIs with deterministic samples; it does not click or move the desktop cursor.

In-game checks are still required for camera feel, left-click firing/charging, right-click visor entry/exit (including holding right click through transitions), Ctrl/Tab coexistence, and Alt-Tab while buttons are held.

The high-resolution hook test verifies viewport-value reads, projection-field/register isolation, read-only surface diagnostic queries (including error/null paths), graphics-wrapper passthrough and controlled initialization failure without displaying an error dialog or exiting the test runner.

The fullscreen window test creates hidden popup windows with the game's legacy 640x480 sizing limits. It verifies selected-resolution client sizes and maximum-size limits, forwarding of native messages, repeated attachment without recursion, cleanup on destruction, and rejection of non-popup windows. It does not show windows, change focus, or change the desktop resolution.

The native integration test requires Windows with the matching native `d3dim.dll` and a working hardware HAL. It uses hidden offscreen surfaces, checks the 2048/2049 boundary on each axis, then verifies actual rendered pixels at larger dimensions (including 2560x1440, 3840x2160, 5120x2880, 7680x4320 and portrait equivalents), patch idempotency, and the retained 8192/8193 ceiling on both axes. It never changes the desktop resolution or Windows DLL files. A replaced native renderer is reported as skipped; unsupported native versions or driver capabilities can cause this environment-dependent test to fail.

That test also verifies native COM method ownership and focused recovery/rebinding of healthy aliased offscreen surfaces before checking rendered pixels. It does not induce actual OS surface loss or switch the desktop's foreground window.

In-game resolution checks: 640x480, 1920x1080, 1920x1440, 2560x1440, a supported mode with height above 2048, 3840x2160, 5120x2880 and 7680x4320 where the display/DSR configuration supports them; include startup screens, menus, gameplay, visor aiming, FMVs, focus loss and shutdown.

For high-resolution/DSR tests, set the Windows desktop to the selected game resolution **before launching the executable**, then verify the full image is visible. Restore the ordinary desktop mode manually after the test if desired. Automatic correction of clipping from a smaller starting desktop is not implemented.

## Object distances and pool capacity

See [the recovered rendering paths and capacity notes](object_rendering_notes.md) for executable fingerprints, function/global addresses and the reasons for keeping gameplay activation and fade tables unchanged.

`object_draw_distance` tests finite-value sanitization, overflow-safe distance math, all 64 actor slots plus Buzz and the terminator, native list order, hide/live/model-ready checks, reentrancy rejection and exact list/visibility restoration. Unrelated gameplay flags remain untouched, including writes made during rendering.

`native_object_rendering` tests every patch/guard mismatch, all-or-nothing read-only validation, bounded expanded pools and the deliberately unmodified bucket-end alias. With a `toy2.exe` argument, it validates all native sites and audits **every PE-relocated pool pointer**, including references in functions not recovered by initial analysis. It then copies the executable into private memory and replays all six actual sprite allocators with mocked texture/queue/plane consumers: 16384 records, exhaustion rejection, reset counters, old-buffer preservation and boundary canaries. It does not launch the game, resolve imports, alter files, show windows or change desktop resolution.

`scene_render_distance` replays the recovered native distance setter for 512 calls, with a live caller x87 value, and verifies stack depth, exception flags, the overridden geometry threshold and the original second threshold. Build all native replay tests as **x86**.

`native_scenery_rendering` tests the scoped detailed/distant split, native far-clip cap, overlap/no-shrink behavior, bounded grids, prerequisite rejection, atomic signatures and restoration after recursion/exceptions. Optional executable replay runs the actual native world/grid routines with synthetic instances and mocked rendering consumers: distant corner-cell visibility, hidden/frustum/range rejection, unchanged portal traversal and special flags, and x87 preservation across 512 calls. See [the scenery audit](scenery_rendering_notes.md); pixel-level grass/fog and emitter behavior still require gameplay checks.

`native_mole_hole_smoke` tests finite far-clip-capped ranges, exact native fixed-point cutoff/height, separate 64-slot capacity/exhaustion, native-near passthrough, private RNG, motion/size/color/lifetime, pause/tick bounds, cleared/reset/changed sources, stale level-specific pointer avoidance, texture/queue forwarding and every manifest mismatch. Optional executable replay executes the actual mole-hole emitter loop using the captured near/far positions in private relocated memory: two near native emissions plus one distant visual emission, then three distant visual emissions, unchanged seven-hole cadence and native particle bytes, and 512 calls with a live caller x87 value. Combined object/scenery/effect manifests are checked together. It does not modify the running game or installed files. `/O2` replay also passes. See [the address/evidence audit](../.ai/mole-hole-smoke/README.md); pixels, overlap at the 4096 boundary and transitions remain manual checks.

In-game checks are still required:

1. Compare the same camera positions with `IncreaseObjectRenderDistance = false` and `true`, restarting between changes. Check bedroom coins/lamp/doorway objects, a large outdoor level, enemies and pickups. Confirm the log reports installation rather than a safe skip.
2. Sweep between rooms/portals and rotate the camera; no extra objects should draw through native hidden rooms. Verify Buzz, animation, death/respawn and level transitions.
3. Verify coin collection, pickup prompts, target lock, keyboard/controller movement, left-click fire, right-click visor, mouse orbit and timing are unchanged. Distant unloaded actors are intentionally not activated.
4. Include menus, loading, cutscenes/FMV, pause, Alt-Tab and shutdown. Keep the established desktop-before-launch setup for high-resolution modes; automatic fullscreen-clipping work remains paused.
5. Compare `IncreaseSceneryRenderDistance = false` and `true` in Andy's neighbourhood while retaining the object option: grass/alpha meshes, the former detailed/distant split and room/portal hiding. Atmospheric fog and particle emitters remain untouched by this scenery option.
6. Compare `IncreaseEffectRenderDistance = false` and `true` after restarting, retaining the same object/scenery settings. Slam three holes and compare the recorded near/far positions in `.ai/mole-hole-smoke/README.md`; smoke should persist at the far position with the effect option enabled. Check crossing the old 4096 cutoff, uncompleted holes, texture transparency, camera rotation, visor, pause/focus loss, death/respawn, leaving/re-entering the level and the confirmed Alt-Tab fix. Other emitters and gameplay interactions must remain native.

## Alt-Tab recovery

See [the recovered display/texture paths](alt_tab_rendering_notes.md) for addresses, layouts and the original recovery gap.

`alt_tab_recovery` uses mock COM vtables to test loss reported by `BeginScene` itself, display restoration order and aliased back buffers, retained-bitmap recreation, borrowed/dynamic texture handling, cached texture invalidation, partial failures, changed devices, wrong-mode recovery, windowed-mode safety, query errors and cyclic-list bounds. It reproduces persistent `DDERR_NOEXCLUSIVEMODE`, verifies reacquisition precedes surface/texture recovery and retains the game's original FPU flags, and rejects missing/mismatched cooperative settings. It verifies no ownership changes, recovery, mode changes or native render-frame starts occur while unfocused, and failed recovery/scene starts do not trigger unbounded retries.

`alt_tab_hooks` tests HRESULT forwarding, null-context paths, wrapper passthrough even when `d3dim.dll` is loaded, and actual method-owner filtering. It also executes the native DirectDraw initialization calling-convention bridge for 512 calls with both FPU policy variants, checking argument forwarding, flags and failed-initialization cleanup. With an executable argument it scans and patches only a private copy of the image, checks relative call targets and all recovered globals, and verifies signature/reference mismatches leave all three hook sites unchanged. It does not launch the game or modify the executable file.

Manual in-game checks (not replaced by the automated tests):

1. With `FixAltTab = true`, switch to another application and back repeatedly from gameplay, the pause menu, visor view, main menus and an FMV. Include both quick switches and a longer period away.
2. Check the image returns completely: geometry, HUD/menu text, sprites, textures and depth ordering. Confirm sound, controls, pause-menu navigation, held mouse-button suppression and timing are unchanged.
3. Check ordinary resolution and a supported high-resolution mode, keeping the desktop-before-launch workaround. Check minimized/restored windows and switching back after a level transition.
4. Confirm the log reports successful recovery or a useful failure stage/error. Compare with `FixAltTab = false` after restarting, and verify replacement graphics wrappers remain unaffected.
