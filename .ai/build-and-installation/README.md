# Build, tests, installation and preserved legacy behavior

## Paths and build

- Repository `C:\Users\Rafal\Desktop\Toy Story 2\scripts\ToyStory2Fix`.
- Game `C:\Users\Rafal\Desktop\Toy Story 2`; installed ASI/INI/log in its **`scripts`** directory, not repo `data/scripts`.
- Windows, **x86/Win32**; Visual Studio 2026 Professional with C++ desktop workload/SDK; generated `build/ToyStory2Fix.sln` is ignored.
- Generate if needed: submodules, then `./premake5.exe vs2026`. Source/include patterns come from `premake5.lua`; headers are compiled via `source/dllmain.cpp`.

```powershell
Import-Module 'C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'C:\Program Files\Microsoft Visual Studio\18\Professional' -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64'
msbuild build/ToyStory2Fix.sln /p:Configuration=Release /p:Platform=Win32 /p:PostBuildEventUseInBuild=false
```

Output **`data/scripts/ToyStory2Fix.asi`** is ignored. Keep `PostBuildEventUseInBuild=false`: generated project has an external game-copy command; building must not silently replace an installation. Do not commit executable/DLL/ASI or Ghidra database files.

## Validation inventory

All **13** suites passed on 2026-10-05; see [`tests/README.md`](../../tests/README.md) for complete compiler/link commands:

| Test | Main coverage |
| --- | --- |
| `mouse_look_camera` | Camera-only orbit ownership/keyboard/collision logic. |
| `mouse_button_actions` | Hold/edge, keyboard coexistence and focus arming. |
| `mouse_button_hooks` | Raw snapshot/register replay, focus and independent toggles. |
| `high_resolution_hooks` | Dimensions, byte-offset projection isolation, safe startup failure/diagnostics. |
| `high_resolution_window` | Hidden fullscreen popup sizing, native forwarding/cleanup. |
| `native_d3d_resolution` | Real offscreen HAL pixels above 2048; 8192 accepted, 8193 rejected, COM ownership/rebinding. |
| `object_draw_distance` | Finite math, eligibility, all loaded slots, gameplay/list restoration. |
| `native_object_rendering` | 70 guards, 55 PE pool references, six native allocators/canaries/exhaustion. |
| `native_scenery_rendering` | Scoped LOD split/grid/portal/hide/frustum and x87 replay. |
| `native_mole_hole_smoke` | Dedicated capacity/lifetime/RNG/range, native emitter near/far and x87 replay. |
| `scene_render_distance` | Native setter pop/second-threshold preservation over 512 calls. |
| `alt_tab_recovery` | Ownership/FPU flags, COM loss/retry/focus/texture categories and list bounds. |
| `alt_tab_hooks` | Native signatures/calling convention, wrapper passthrough and HRESULTs. |

Optional executable replays use a **private relocated copy**, never patch the file/live game. Run new smoke replay with **`/O2` and assertions enabled** as well; passed. Do not define `NDEBUG` for assertion-based tests. Regression coverage does not prove all visual/driver/transition behavior; record user reports separately from independently measured installation/log/pixels.

## Installation safety

The user earlier chose commit/push only; the agent did not install the smoke build. The later "works" report authorized this master merge/push, not a new live-memory operation. See [`project-state.md`](../project-state.md) for last verified installed hash and exact evidence distinction.

If installation is later requested: exit game, back up installed ASI/INI, preserve **every option value** (especially `PortableGame = true`), copy validated ASI, append only missing options, verify hashes/values and inspect log after restart. Never replace installed INI wholesale with shipped defaults. Preserve its full portable-game comment and desktop-before-launch guidance. `ToyStory2Fix.log` is truncated at first write each session then appended, so back it up before restarting if needed for evidence.

## Existing options and legacy patches

- `PortableGame` is **false** in shipped defaults/fallback but **true** in the user's installation. It bypasses original install/CD registry and validation-file routine by a RET patch selected by `81 EC 10 04 00 00`. Game/CD data must exist locally; option does not provide content. Do not infer or document an unverified absolute address for this legacy pattern.
- `Allow32Bit` legacy branch at `004ACA44`, `IgnoreVRAM` branch at `004ACAC2`; preserved. Device enumeration fix is distinct from the >2048 runtime rejection.
- `FixFramerate`: QueryPerformanceCounter/period sampling, speed multiplier clamped 1..3, demos minimum factor 2; targets native timer path `00490860` and existing frame sites. New visual effects use **native game ticks**, never change this timing code or consume wall-clock animation while paused.
- `SkipSplash` hook at `00438586` enables immediate Space/Jump skip. Does not add a new input action or bypass menu/gameplay.
- `DiskFix` site `00411099`, `ZurgFix` sites `00407F8E/00407FB0`, texture patch/jump `004DBD3D/004B300E`, widescreen setup `004317EC`: existing source-comment/pattern locations, not a new comprehensive reverse-engineering audit. Preserve them; scope/safety guarantees of newer atomic manifests must not be assumed for older legacy patches.
- `IncreaseEnemyRenderDistance` is an independent original visibility tweak; not controlled by `RenderDistanceValue`, and not authorization to raise gameplay activation.
- `IncreaseRenderDistance` parses numbers/scientific notation/optional `f`, case-insensitive `SQRT_FLT_MAX` or `INFINITY`; finite cap `1e15`, invalid/nonpositive fallback recommended `sqrt(FLT_MAX)`, original-match value `1.45e8f`. Geometry threshold is **not** the finite renderer-unit `ObjectDrawDistance` radius. Retain scene setter's x87 pop and second threshold.

Keep mouse/controller/keyboard, fire/visor/target lock, portable behavior, timing, 8192 limit, confirmed grass and native Alt-Tab fixes unchanged when adding another feature.
