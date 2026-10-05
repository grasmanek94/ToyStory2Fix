# Native display, high resolution, widescreen and Alt-Tab

Files: `source/NativeD3DResolution.h`, `source/HighResolutionLimits.h`, `source/HighResolutionWindow.h`, `source/AltTabRecovery.h`, display hooks in `source/dllmain.cpp`. See [Ghidra paths/fingerprints](../ghidra/README.md) before using addresses.

## Resolution limit and initialization safety

- Native `d3dim.dll` rejects surfaces above **2048** in both device creation and render-target validation. Patch both recognized `MOV EAX,2048` limits to **8192 per axis**, only in loaded process memory, only for the native hardware HAL. Do not patch Windows DLL files or software/replacement renderers.
- Ghidra `d3dim.dll` base `74000000`: `Direct3DCreateDevice` **`74004E20`**; `InitializeNativeD3DDeviceContext` **`74005060`**, limit at **`740052B4`**; `SetNativeD3DRenderTarget` **`7400F9C0`**, limit at **`7400FAB5`**. These DLL addresses depend on the analyzed build; runtime uses two unique signatures and checks original/already-patched immediates before writing either.
- Game `InitializeGameDisplay` **`00412B50`** calls `CreateDisplayContext` **`004ABEB0`** at **`00412CCC`**. Startup wrapper handles a failed native graphics result cleanly instead of letting renderer startup consume released context resources.
- Fullscreen popup sizing is corrected before/after DirectDraw setup, including legacy 640x480 tracking limits. Only supported native HAL fullscreen modes are resized; original window messages still forward. No automatic desktop correction is implemented.
- **Keep the desktop-before-launch rule:** set Windows desktop to intended high-resolution/DSR mode before launching. Otherwise only the top-left of logos/menus/movies/gameplay may appear despite full-sized render buffers. Automatic clipping investigation remains paused; do not remove guidance or change the 8192 ceiling.

## Widescreen correction

- `InitializeViewportDimensionCache` **`004B55D0`**, viewport operand use at **`004B5672`**: read **contents** of width/height globals, not their addresses.
- `UpdateRenderCameraProjection` **`004CE050`**, original instruction at **`004CE08F`**: `MOV [EAX+0x44],0.75`.
- Projection field **byte offset `+0x44`**, not float-element index 0x44. Replace complete **seven-byte** instruction. Leaving final `0x3F` executes `AAS` and corrupts the camera pointer.
- 3D scale uses inverse aspect, 2D scale `(4/3)/aspect`; invalid zero viewport dimensions safely use 4:3. Unrelated camera fields/registers stay native.

## Alt-Tab failure and fix

Original `BeginNativeRenderFrame` returns early if `BeginScene` fails, preventing presentation's old surface fallback. Restoring surfaces alone also does not recover lost texture pixels or exclusive ownership. The first test build stayed black because **`0x887600E1 = DDERR_NOEXCLUSIVEMODE`** was treated as something that would resolve automatically.

| Game address | Role |
| --- | --- |
| `004ABA40` / `004ABA80` | Native DirectDraw / Direct3D device getters. |
| `004ABA90` / `004ABAD0` | `BeginNativeDirect3DScene` / `EndNativeDirect3DScene`. |
| `004B2D50` / `004B2DE0` | Begin/end render frame. Begin call is recovery hook. |
| `004ABAB0` | Presentation fallback wrapper; hook first call to record actual HRESULT before native fallback hides it. |
| `004ABD40` / `004AF5F0` | Native present / flip-or-blit surface. |
| `004ABD30` / `004AF680` | Old surface restoration; ignored failures and omitted independent render target. |
| `004AEEE0` / `004AEE1F` | Native DirectDraw initialization / settings-capture call. Calling-convention bridge retains ECX `this` and both stack args. |
| `00884008` | Display-context pointer cell. |
| `004AFC10` / `004AFBA0` | Allocate texture record / release its graphics objects. |
| `004B0200` / `004AFF80` | Recreate texture graphics / upload retained CPU bitmap. |
| `004C2870` / `004AC1A0` | Renderer/native texture binding. |
| `00884444` / `0088400C` | Texture-list head / currently bound texture. |
| `00AAD778` | Cached renderer texture handle; invalidate to -1 after recovery. |

Display context: HWND **`+0`**, fullscreen flag **`+4`**, width/height **`+8/+0xC`**, primary/back/render/depth surfaces **`+0x30/+0x34/+0x38/+0x3C`**, device **`+0x40`**, DirectDraw **`+0x48`**.

Recovery runs on the render thread before BeginScene, only foreground/non-minimized. Reacquire exclusive ownership with original HWND/DirectDraw/cooperative flags **before** restoring surfaces/textures. Native flags exclusive/fullscreen/allow-reboot **`0x13`** or normal **`0x08`**, plus **`DDSCL_FPUSETUP 0x800`** unless original init flag `0x10` is set. Do not invent a different FPU/fullscreen policy.

Native `ddraw.dll`, analysis base `51000000`: cooperative check **`510352E0`** (COM vtable `+0x68`), set/reacquire **`51034BE0`** (vtable `+0x50`). Call public COM methods, do not patch DLL code. Recheck ownership; failures defer recovery. Restore primary, back, target, depth, deduplicating aliases and re-querying implicitly restored children. Refresh device target even if prior fallback restored aliased surfaces. One recovery pass / at most two BeginScene calls per focused frame; no unbounded loop.

Texture record **`0x110`** bytes: surface `+0`, texture interface `+4`, descriptor `+8`, retained CPU bitmap `+0x84`, dimensions `+0x88/+0x8C`, flags `+0x100` (borrowed framebuffer `0x40`), refs `+0x104`, next/prev `+0x108/+0x10C`. Recreate only lost owned records with retained pixels; preserve native conversion/vertical flip, refs/links and bitmap. Borrowed records skip; dynamic/no-bitmap surfaces await native refill. List walk capped at 4096 for corruption/cycles.

Healthy selected mode may be reapplied only after confirmed wrong/lost exclusive mode, not to change ordinary desktop settings. Windowed recovery does not change mode. Wrapper filtering verifies actual COM method ownership in `d3dim.dll`/`ddraw.dll`, not just module presence.

Alt-Tab is user-confirmed. Detailed audit and failure history: [`tests/alt_tab_rendering_notes.md`](../../tests/alt_tab_rendering_notes.md). Pixel/integration tests retain 8192/8193 boundaries; broader drivers/FMV/transitions remain manual.
