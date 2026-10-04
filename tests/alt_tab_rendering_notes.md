# Native Alt-Tab rendering recovery

Analyzed `toy2.exe`: image base `00400000`, image size `00A7F000`, PE timestamp `381979B4`, SHA-256 `023eb6a9459443b34d24cf685591bfeb3b95e1acf579405f6d8fa4407ccbdaf0`. Names and explanatory comments were applied in Ghidra. Hook installation uses unique executable signatures and cross-checks their shared globals/call targets before changing any of the three call sites.

## Original recovery gap

The reported failure is a persistent black image after returning from Alt-Tab while sound, gameplay and pause-menu input continue.

`BeginNativeRenderFrame` calls `BeginNativeDirect3DScene`, then returns false immediately for a nonzero HRESULT. Presentation, including the old lost-surface fallback, can therefore be skipped just when it is needed. That fallback also ignores restore failures and does not recover texture pixels or device bindings.

| Address | Recovered function |
| --- | --- |
| `004ABA40` | `GetNativeDirectDrawInterface` |
| `004ABA80` | `GetNativeDirect3DDevice` |
| `004ABA90` | `BeginNativeDirect3DScene` (`BeginScene`, device vtable `+0x24`) |
| `004ABAD0` | `EndNativeDirect3DScene` (`EndScene`, device vtable `+0x28`) |
| `004ABAB0` | `PresentNativeDisplayWithSurfaceFallback` |
| `004ABD40` | `PresentNativeDisplay` |
| `004AF5F0` | `FlipOrBlitDisplaySurface` |
| `004ABD30` | `RestoreNativeDisplaySurfaces` |
| `004AF680` | `RestoreLostDisplaySurfacesUnchecked` |
| `004ABBF0` | `GetNativeDisplayBackBuffer` |
| `004B2D50` | `BeginNativeRenderFrame` (pre-frame hook call site) |
| `004B2DE0` | `EndNativeRenderFrame` |
| `004AEEE0` | `InitializeNativeDirectDraw` (settings capture at its `CALL` at `004AEE1F`) |

The second hook replaces only the first `CALL` at `004ABAB0`. It records the actual presentation HRESULT before the original `DDERR_SURFACELOST` comparison/tail jump to restoration can hide that failure. The native fallback remains intact.

`RestoreLostDisplaySurfacesUnchecked` queries/restores only context `+0x30`, `+0x34` and `+0x3C`, then returns zero regardless of the restoration results. It does not independently recover a distinct render target at `+0x38`.

## Display context

`g_pDisplayContext` is the pointer cell at `00884008`.

| Byte offset | Field |
| --- | --- |
| `+0x00` | Game HWND |
| `+0x04` | Exclusive/fullscreen flag |
| `+0x08`, `+0x0C` | Native display dimensions |
| `+0x30` | Primary `IDirectDrawSurface4` |
| `+0x34` | Back buffer, potentially an implicit attachment of the primary |
| `+0x38` | Render target, commonly aliased to primary/back buffer |
| `+0x3C` | Depth buffer |
| `+0x40` | `IDirect3DDevice3` |
| `+0x48` | `IDirectDraw4` |

The fix runs on the existing render thread before `BeginScene`, not inside the window procedure. It requires a foreground, non-minimized game window and checks `TestCooperativeLevel` before restoring anything. Restore order is primary, back, render target, depth; each is re-queried, and duplicate surface pointers are skipped. Restoring the primary normally restores its implicit back buffer; independently restoring a still-lost implicit child can return `DDERR_IMPLICITLYCREATED` and is not treated as success.

After display/texture recovery, `SetRenderTarget` refreshes the device binding even if the old native presentation fallback already restored an aliased surface. Failed operations retain pending work for a subsequent focused frame. There is at most one recovery pass and two `BeginScene` calls per frame, with no recovery while inactive.

The selected DirectDraw mode is captured during a healthy focused frame. A reported `DDERR_WRONGMODE`, or a different mode found after reacquiring exclusive ownership, permits reapplying it, and only for an exclusive/fullscreen context. An already-correct mode is not changed merely because ownership was reacquired. Windowed contexts never change the desktop mode. This does not attempt the paused high-resolution clipping workaround, select a new mode, or change the 8192-per-axis limit.

### Observed failure of the first test build

The user's first in-game retest remained black. Its runtime log at `23:18:21` recorded `0x887600E1` at `cooperative-level`, with zero restored surfaces/textures. This is **`DDERR_NOEXCLUSIVEMODE`**, not evidence of successful surface restoration or a device crash. The first build returned immediately on that error, so it could wait forever for automatic ownership recovery.

The revised build reclaims ownership only after focus returns, with `IDirectDraw4::SetCooperativeLevel` using the same HWND, DirectDraw instance and flags as successful initial setup. `InitializeNativeDirectDraw` builds `DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN | DDSCL_ALLOWREBOOT` (`0x13`) or `DDSCL_NORMAL` (`0x08`), and adds `DDSCL_FPUSETUP` (`0x800`) unless game initialization flag `0x10` is set. The complete flag-calculation/return bytes are validated before installing the settings-capture hook. The calling-convention bridge retains ECX `this` and both stack arguments; it forwards the original result and records settings only after success. No new fullscreen/FPU policy is invented.

Ghidra evidence in matching native `ddraw.dll` (image base `51000000`): `CheckDirectDrawCooperativeLevel` at `510352E0` is assigned to vtable `+0x68` and tests exclusive ownership, returning `0x887600E1` when an exclusive instance no longer owns it. `SetDirectDrawCooperativeLevel` at `51034BE0` is assigned to vtable `+0x50`; its exclusive branch explicitly reacquires ownership even for an already-configured window. These are public COM calls, not new patches to Windows DLL code. Recovery checks ownership again afterward; failed acquisition or a still-unowned instance stops that frame's recovery instead of touching lost buffers.

## Texture contents and bindings

Restoring a lost texture surface allocates its storage again; it does **not** restore its pixels. `RestoreAllSurfaces` alone is therefore insufficient.

| Address | Recovered function/global |
| --- | --- |
| `004AFC10` | `AllocateTextureRecord` |
| `004AFBA0` | `ReleaseTextureGraphicsObjects` |
| `004B0200` | `RecreateTextureGraphicsObjects` |
| `004AFF80` | `UploadRetainedTextureBitmap` |
| `004C2870` | `BindRendererTextureHandle` |
| `004AC1A0` | `BindNativeTextureRecord` |
| `00884444` | `g_pTextureRecordHead` |
| `0088400C` | `g_pBoundTextureRecord` |
| `00AAD778` | `g_nCachedTextureHandle` |

Texture records are `0x110` bytes:

| Byte offset | Field |
| --- | --- |
| `+0x00` | Owned texture `IDirectDrawSurface4` |
| `+0x04` | `IDirect3DTexture2` |
| `+0x08` | Surface description |
| `+0x84` | Retained CPU bitmap |
| `+0x88`, `+0x8C` | Bitmap width/height |
| `+0x100` | Flags; `0x40` marks a borrowed framebuffer surface |
| `+0x104` | Native reference count |
| `+0x108`, `+0x10C` | Next/previous list links |

`RecreateTextureGraphicsObjects` is `int __cdecl(record*)`, returning 1 for creation and 0 for failure/borrowed records. It releases the owned graphics objects, creates a supported native texture, obtains its texture interface, refreshes its descriptor, and calls the retained-bitmap upload. It does not allocate a new record, change its handles/list links/reference count, or free the CPU bitmap. The upload retains the game's original channel-mask conversion and vertical flip.

The fix walks the existing list, recreating only lost/missing owned surfaces with retained pixels. Borrowed framebuffer records are skipped; dynamic surfaces without CPU pixels are restored and left for their native producer to refill. A 4096-record traversal guard bounds corrupt/cyclic lists. After recreation, stage 0 is unbound and the cached renderer handle is set to `-1`, forcing the next native texture selection to bind the new COM interface.

Graphics wrappers are passed through: recovery requires device `BeginScene` code to belong to loaded `d3dim.dll` and DirectDraw `TestCooperativeLevel` code to belong to loaded `ddraw.dll`, not merely the presence of those module names.

## Validation and limits

Automated tests cover persistent exclusive-ownership loss, original FPU flags, mocked surface loss, failures/retries, texture categories, list bounds, cache invalidation, mode/focus safety and wrapper passthrough. Executable integration scans/patches a private image and checks all three hook sites and recovered references. Native offscreen integration confirms real COM method ownership, healthy-surface recovery/rebinding and rendered pixels through the retained 8K limits; it does not seize actual fullscreen ownership.

The user confirmed that the revised build resolves their Alt-Tab black screen. That confirmation and these tests do not establish recovery on every driver or from every FMV/level transition; broader in-game checks remain in the manual checklist. No gameplay, audio, input, timing, activation, draw-distance or renderer-pool behavior is changed by this feature.
