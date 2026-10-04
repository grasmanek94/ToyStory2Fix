# Toy Story 2 Fix

A Windows patch that fixes and enhances Toy Story 2 for the PC. This fork includes:

* A fix for the "Unable to enumerate a suitable device" error and support for 32-bit colour resolutions.
* Framerate/timing fixes for modern PCs, plus fixes for the disk launcher and fast Zurg/flying enemies at 60 FPS.
* Widescreen rendering without 3D stretching and texture-mapping fixes.
* An experimental native Direct3D resolution-limit fix for dimensions above 2048, including 2560x1440 and 3840x2160.
* Configurable level render distance, finite coin/pickup/loaded-object distances and enlarged renderer queues.
* Configurable portable/no-CD support for a local game installation.
* Mouse-controlled camera orbit and visor aiming, with left-click fire and right-click visor controls.
* Immediate skipping of the copyright and ESRB screens with Space/Jump.

## Download and installation

See [this fork's releases](https://github.com/grasmanek94/ToyStory2Fix/releases) for packaged builds. Extract a release into the game folder, preserving its directory structure. Older upstream releases may not include the features added by this fork; build the current source if no packaged release includes them.

The patch loads as `scripts\ToyStory2Fix.asi` through an ASI loader. Keep the loader supplied with your distribution, and place `ToyStory2Fix.ini` alongside the `.asi`. Restart the game after replacing the patch or changing its settings.

## Configuration

All settings belong to the `[ToyStory2Fix]` section in `scripts\ToyStory2Fix.ini`. Boolean options accept `true` or `false`; the defaults below also apply when an option is missing.

| Option | Default | Purpose |
| --- | --- | --- |
| `FixFramerate` | `true` | Adjust game timing for modern systems. |
| `Allow32Bit` | `true` | Allow 32-bit colour resolutions regardless of the original registry setting. |
| `FixHighResolution` | `true` | Raise matching native Direct3D surface limits to 8192 per axis (8K) and handle graphics startup failures cleanly. |
| `IgnoreVRAM` | `true` | Ignore reported VRAM during graphics-device enumeration. |
| `PortableGame` | `false` | When enabled, bypass the original installation-registry and CD validation for a local game copy. |
| `SkipSplash` | `true` | Allow immediate copyright/ESRB screen skipping. |
| `MouseLook` | `true` | Enable third-person camera orbit and visor mouse aiming. |
| `MouseSensitivity` | `4.0` | Game-angle units per mouse pixel; range `0.1`–`32.0`. |
| `InvertMouseX` | `false` | Invert horizontal mouse movement. |
| `InvertMouseY` | `true` | Invert vertical mouse movement. |
| `MouseButtons` | `true` | Bind left click to fire and right click to visor view. |
| `IncreaseRenderDistance` | `true` | Increase the draw distance of level geometry. |
| `RenderDistanceValue` | `SQRT_FLT_MAX` | Set the level render-distance threshold; see below. |
| `IncreaseEnemyRenderDistance` | `true` | Extend enemy draw distance separately from level geometry. |
| `IncreaseObjectRenderDistance` | `true` | Extend coin/pickup and loaded actor rendering, with enlarged renderer-only pools. |
| `ObjectDrawDistance` | `65536` | Finite object-rendering radius in renderer world units; range `1024`–`65536`. |
| `Widescreen` | `true` | Correct the 3D aspect ratio for widescreen resolutions. |
| `TextureFix` | `true` | Fix texture-mapping bugs. |
| `DiskFix` | `true` | Fix the broken disk launcher at 60 FPS. |
| `ZurgFix` | `true` | Fix excessively fast Zurg and other flying enemies at 60 FPS. |

### High-resolution support (experimental)

Some native Windows Direct3D3 runtimes reject render targets wider or taller than 2048 pixels, even when the driver advertises larger texture limits. The game does not safely handle that device-creation failure and continues using an invalid display context with released graphics resources.

`FixHighResolution = true` raises the two recognized native `d3dim.dll` dimension checks to **8192 pixels per axis**, allowing 8K modes such as 7680x4320 and DSR modes such as 5120x2880 to be tested. It only patches larger resolutions selected with the hardware HAL device, and patches the loaded runtime in the game process; **no Windows DLL files are changed**. Original surface/driver validation remains in place. Unknown runtime signatures are left untouched, as are graphics wrappers that replace the native renderer. Any reported graphics-initialization failure produces a useful error message and a controlled exit rather than continuing into the crash path.

For those native high-resolution fullscreen modes, the patch also sizes the popup window before and after DirectDraw setup and overrides the game's legacy 640x480 maximum-size limits with the selected resolution. Other window messages still go through the original handler. Windowed modes, lower resolutions and replacement graphics wrappers are not resized.

**For high-resolution/DSR modes, set the Windows desktop resolution to the intended game resolution before launching `toy2.exe`.** For example, set the desktop to **5120x2880 first**, then launch the game and select **5120x2880**. Changing only the in-game resolution can leave logos, movies, menus and gameplay cropped to the top-left, even when all render buffers have the correct dimensions. The manual desktop-before-launch workaround was confirmed at 5120x2880; automatic correction is not provided. After exiting, restore your normal Windows desktop resolution if desired. Investigation of an automatic clipping fix is currently paused.

This is not unlimited-resolution support: larger dimensions, software renderers and different Windows runtime builds are not guaranteed. Native offscreen tests cover device creation, render-target selection and actual pixels drawn past the old boundary at 2560x1440, 3840x2160, 5120x2880 and 7680x4320, including portrait equivalents. Full in-game testing is still required. Set `FixHighResolution = false` to disable the startup hook and native limit patch.

The widescreen hook also now reads actual viewport dimensions, writes the projection field at byte offset `0x44`, and replaces the complete seven-byte instruction. This avoids an out-of-bounds projection write and a leftover instruction byte that could alter the camera pointer.

### Portable / no-CD support

`PortableGame = true` skips the original install-path/CD-path registry lookup and the CD validation-file check. Keep all required game data available locally: this option does not supply files that would otherwise be read from the disc. Set it to `false` to restore the original validation.

### Mouse look and buttons

**Mouse support was developed with AI assistance**, including the camera-only orbit correction and the fire/visor button bindings.

In normal gameplay, the mouse orbits the camera without rotating Buzz, in both active and passive camera modes. Keyboard camera controls take priority, and the camera-recenter action releases the mouse-selected angles. Visor mouse aiming changes Buzz's aim direction so shots remain aligned with the camera.

* **Left click/hold:** fire, equivalent to the action bound to Left Ctrl by default, including the game's normal hold/charge behavior.
* **Right click:** toggle visor view, equivalent to the action bound to Tab by default. Holding the button does not repeatedly toggle the visor.
* Original keyboard/controller bindings, including Ctrl, Tab and target-lock actions, remain available.
* Input is sampled only while the game is focused. After focus returns, release any held mouse button before pressing it again; mouse-look sampling also resets across visor transitions.
* `MouseLook` and `MouseButtons` can be disabled independently. `InvertMouseY` is enabled by default; set it to `false` for the opposite vertical direction.

Mouse sensitivity is clamped to `0.1`–`32.0`; invalid or non-positive values use `4.0`. The mouse hooks use executable-pattern detection and disable the affected mouse feature if its required signatures do not match uniquely, rather than applying an unverified patch. Mouse behavior on other regional executables still needs testing.

### Level and enemy render distance

`IncreaseRenderDistance` controls level geometry. When enabled, `RenderDistanceValue` accepts:

* A plain or scientific-notation number, optionally ending in `f`/`F`, such as `1.45e8`, `1.45e8f` or `100.0f`.
* `SQRT_FLT_MAX` (default): the recommended maximum threshold, approximately `1.84467e19`.
* `INFINITY`: disable this distance-culling threshold entirely. This can cause NPC rendering problems or FPS drops.

Keywords are case-insensitive. `1.45e8f` is the closest match to the original game threshold. Invalid values, NaN and non-keyword infinity fall back to `SQRT_FLT_MAX`; zero or negative finite values use `1.45e8f`. Other finite values above `1e15` are clamped to `1e15`; the explicit `SQRT_FLT_MAX` and `INFINITY` keywords are exempt.

`IncreaseEnemyRenderDistance` is a separate fix for enemies disappearing at a shorter distance. It can be toggled independently and is not controlled by `RenderDistanceValue`.

### Extended object distance (experimental)

`IncreaseObjectRenderDistance = true` uses a **large finite** `ObjectDrawDistance` for coins, pickups and already-loaded actor models. The default is **65536 renderer world units**; finite values are clamped to `1024`–`65536`, and invalid values (including infinity/NaN) use the default. This is a separate radius, not the squared geometry threshold in `RenderDistanceValue`.

To avoid trading disappearing objects for buffer overruns, this option allocates larger renderer-only queues before increasing visibility:

| Renderer capacity | Original | Extended |
| --- | ---: | ---: |
| World sprites | 2000 | 16384 |
| Model transforms | 1000 | 8192 |
| Render entries | 3000 | 32768 |
| Sorted triangles | 3000 | 32768 |
| Actor bone-buffer slots | 32 | 65 (64 loaded actors plus Buzz) |

The extra arena uses approximately **10.3 MiB**. All recovered pool references, reset counters and hook sites are validated together; unknown executables, modified signatures or allocation failure leave this feature's distances and pools unchanged. Compatibility currently covers the analyzed `toy2.exe`, not every regional release.

This is **not unlimited actor activation**: the native 64-slot gameplay actor pool, AI, collision, respawn and close-range pickup/target-lock checks are not extended. Additional actor visibility is applied only during rendering and restored immediately afterward. Native room/portal and frustum hiding remain in place, and an unloaded actor cannot be drawn. Some distant entities can therefore still disappear; expanding gameplay capacity safely requires a separate investigation. More visible geometry can also lower FPS.

Set `IncreaseObjectRenderDistance = false` to compare with the original object distances and renderer capacities on the next launch. The geometry-distance patch also now preserves the native x87 stack pop instead of leaking one floating-point value per distance-setter call.

### Log file

`ToyStory2Fix.log` is written alongside the `.asi`/`.ini`. It records display dimensions and initialization results, native resolution-patch status, fullscreen window/client and monitor dimensions, current OS/DirectDraw modes, DPI awareness, native surface dimensions/lost status, mouse-feature activation or signature failures, mouse-look sensitivity/inversion, parsed render-distance values, object-pool installation status and increases in the number of additional rendered actors. Check it to confirm that the intended options are being applied.

## Building and testing

Use Visual Studio with the C++ desktop workload and a Windows SDK. From a Visual Studio Developer PowerShell, initialize dependencies and generate the solution:

```powershell
git submodule update --init --recursive
./premake5.exe vs2026
msbuild build/ToyStory2Fix.sln /p:Configuration=Release /p:Platform=Win32 /p:PostBuildEventUseInBuild=false
```

The built patch is `data/scripts/ToyStory2Fix.asi`. Copy it, together with the INI, into the game's `scripts` directory. The game and patch are 32-bit, so use `Win32`, not `x64`. For another supported Visual Studio version, use the corresponding Premake generator.

See [the regression-test instructions](tests/README.md) for input, high-resolution, finite-distance, renderer-pool and native x87 replay tests. In-game testing remains necessary for camera feel, object visibility, collisions and executable compatibility.

## Credits

* [RibShark](https://github.com/RibShark/ToyStory2Fix) — original project and developer.
* [AndetSTK](https://github.com/AndetSTK/ToyStory2Fix) — upstream repository.
* [Juan-Antonio-Doe](https://github.com/Juan-Antonio-Doe/ToyStory2Fix) — upstream fork and render-distance enhancements.
* [grasmanek94](https://github.com/grasmanek94/ToyStory2Fix) — this fork, portable support and mouse features developed with AI assistance.
* DavidJ75 — enemy render-distance fix.
* WinterSnowfall (d7vk) — recommended maximum render-distance threshold.
* hdc0 — 32-bit colour and graphics-device enumeration fixes.
