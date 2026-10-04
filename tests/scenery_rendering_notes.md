# Detailed scenery distance audit

## Supported executable and scope

The same supported `toy2.exe` as the object-renderer patch: image base `00400000`, image size `00A7F000`, timestamp `381979B4`, SHA-256 `023eb6a9459443b34d24cf685591bfeb3b95e1acf579405f6d8fa4407ccbdaf0`. Installation requires the successful extended-object renderer allocation/manifest first. The scenery manifest validates all 12 hook/guard sites before changing any of its three calls.

The reported location is **Andy's neighbourhood** (`level02`). Its NGN contains two static-instance groups: 374 detailed instances and 214 distant instances. Grass/alpha scenery must be kept in the detailed pass; increasing the shared geometry-radius global alone cannot bypass its independent depth boundary. The user confirmed grass is now visible. The remaining fog effect appears after hitting the soldier with a slam in the mole holes and still disappears; its native rendering/activation path is not yet identified. Further fog investigation is paused until the user's command.

## Recovered native paths

| Address | Evidence / role |
| --- | --- |
| `0044127B` | `RenderGameplayScene` calls the world-scenery passes at `004CDDD0`, before actor models and particles. Only this world call is wrapped. |
| `004CDD10` | Selects one of three six-float detail presets and calls the squared-distance setter. |
| `00508D14` / `00508D18` | Distant-pass near cull / detailed-pass far cull. Highest-detail defaults are 10000 / 12000; lower presets use 3000 / 3500 and 5000 / 6500. |
| `004CDE39` / `004CDF29` | Load those independent boundaries into camera `+0x50` / `+0x54` before rebuilding projection/frustum state. |
| `004BA420` | Builds additional near/far culling planes from camera `+0x50` / `+0x54`; they are distinct from hardware projection near/far `+0x48` / `+0x4C`. |
| `00508D0C` | Detailed-pass hardware far clip, normally 48000. Left unchanged; the extension cannot exceed it. |
| `004BC720` | Visits camera-centred grid cells, follows linked `0x94`-byte instances, checks pass `+0x90`, hidden flag `+0x8C`, distance and native sphere/frustum outcodes, then queues static models. |
| `004CDEDA` / `004CDFDB` | Grid renderer calls for the distant/detailed passes. Native radii are 20/30 and 15/30 depending on special flags. |
| `004C33F0` / `004C36A0` | Loader creates a separately allocated 20x20 cell table for each static-instance pass. The cell width/depth reflect scene bounds; cells are not fixed-size terrain units. |
| `004C30D0` / `004C3130` | Reject out-of-bounds camera/cell coordinates before indexing the allocated cell table. |
| `004BC460` | Separate recursive room/portal renderer. Its traversal and hidden-room policy are not replaced by a grid scan. |
| `004B8490` / `004B84E0` / `004B85E0` | Static-model queue path uses the already-relocated transform and render-entry pools, including native exhaustion checks. |

## Override and safety

1. Save distant-near, detailed-far and primary squared-radius globals immediately around the native world-scenery call. Reject invalid layout/settings; reject reentrant extension. Restore the saved values even on a C++ exception.
2. Extend detailed-far toward the sanitized finite object distance, without shrinking native ranges or exceeding the existing hardware far clip. Move distant-near by the same delta, preserving the native overlap. With the normal defaults and `ObjectDrawDistance = 65536`, the boundaries become **46000 / 48000** during the world call only.
3. Raise the primary squared-radius check only if smaller than the requested finite radius squared. Preserve an existing larger/infinite `IncreaseRenderDistance` value and do not touch the secondary squared-radius minimum.
4. During the scoped grid path, cover the native grid using its dimensions, capped to audited small layouts (each axis at most 64). The recovered 20x20 layout requires radius 20; the native cell lookup still bounds every access. Unknown dimensions retain the native radius. No static-instance table, grid, gameplay pool or spawn limit is enlarged.
5. The native passes rebuild/reset their frustum state as before. Hardware near/far projection, depth precision, atmospheric fog settings, material alpha, special rendering flags, room/portal traversal and hidden instances remain native.

The existing 8192-transform, 32768-entry/triangle and 16384-sprite capacities are a prerequisite, not optional compensation after visibility changes. Exhaustion still drops rendering through native checks instead of overflowing. The original 64-slot gameplay actor pool remains unchanged.

## Effects and fade-table limits

`BuildDistanceFadeLookup` (`0043E6E0`) uses `0054BEF0` as a range while filling the fixed short table at `00557C20..00559C1E`. Neither the range nor the table is changed. References at `00449990`, `0044AA98`, `0044BD04` and `0044D3C2` are in unrecovered legacy code, not evidence for safely raising the shared value.

The live PC sprite producers called from `RenderGameplayScene` consume existing particle/effect records (`00445980`, `0044F010`, `0044EB90`) rather than that fade table. Their simulation/activation paths are separate: the general particle renderer iterates 64 `0x3C`-byte records, the rising-effect renderer consumes 64 `0x10`-byte records, and the neighbourhood chain renderer consumes eight independently allocated chains. None of these lifetimes, activation flags, collision updates or fixed simulation capacities are changed. Extending world scenery does not synthesize absent particles. Do not rename these paths as "fog" without identifying the visible effect.

## Validation and remaining checks

`native_scenery_rendering` covers finite/clamped/no-shrink split math, LOD overlap preservation, invalid settings, bounded grids, expanded-pool prerequisites, reentrancy, exception restoration, every manifest mismatch and patch isolation. With an executable argument it replays the actual native world/grid routines in a relocated private image using synthetic loaded instances and mock projection/frustum/queue consumers. It verifies a corner-cell detailed instance at 30000 becomes visible without its old distant counterpart, hidden/off-frustum/out-of-range records stay rejected, portal rendering stays portal-based, native special flags survive, and 512 calls retain a live x87 value/stack depth. It does not launch the game or prove pixel-level grass/fog correctness.

On 2026-10-05, all **12 regression suites** and the Win32 Release build passed. The scenery executable replay also passed with `/O2`, including combined object/scenery manifest compatibility. Input, user-confirmed Alt-Tab behavior and the retained native 8192-per-axis limits are preserved by the existing regression coverage; full gameplay/driver coverage remains manual.

The user subsequently confirmed the grass fix works. Implementation commit `866ea05` is pushed to `feature/extended-object-draw-distance`. This confirmation does not establish that the unresolved mole-hole fog effect or every scenery path is fixed.

Pending manual checks:

- Broader scenery checks: restart with the scenery option on/off, keeping object/geometry options identical. Compare fence-side plants, transparency and other camera positions; the reported grass visibility fix is already user-confirmed.
- Check the former split region for duplicate scenery, depth fighting or abrupt changes; rotate the camera and cross room/portal boundaries.
- When the user resumes fog work, reproduce the disappearing effect after slamming the soldier in the mole holes and distinguish scene alpha meshes, atmospheric fog and spawned particles before editing its producer.
- Confirm pickup/target lock, movement/controller, firing/visor/mouse orbit, timing, pause, death/respawn, level transitions, FMVs and the working Alt-Tab fix remain unchanged.
- Retain the desktop-before-launch high-resolution setup and 8192-per-axis ceiling; automatic fullscreen clipping work remains paused.
