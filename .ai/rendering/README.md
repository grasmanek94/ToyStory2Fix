# Rendering distances, capacities and isolation

All game addresses use the [fingerprinted x86 executable](../ghidra/README.md). Source of truth for exact patch bytes is `source/NativeObjectRendering.h` / `source/NativeSceneryRendering.h`, not a guessed address map.

## Independent native paths

| Address | Function/global and role |
| --- | --- |
| `004BC410` | `SetSceneCullDistanceSquares`, two squared float thresholds at `005088B0/B4`; both stores pop x87. Geometry option replaces only the first threshold while retaining that pop. |
| `004086F0` | `UpdateNearbyActors`: gameplay activation/update of 64 `0x9C`-byte records at `0052C840`; builds nearby list `0052F1D0`. **Do not extend as a rendering shortcut.** |
| `00447BD0` | `UpdateNearbyActorRenderVisibility`: per-actor range `+0x42`, room/frustum/model checks; render bit `0x1`, hidden-room bit `0x2000`. |
| `004A28B0` | `BuildActorRenderList`: nearby-list copy at `00529D48`, then Buzz `0052F300` and null terminator. |
| `00440F70` | `RenderGameplayScene`: world, models, coins/pickups, effects and queue flushes. |
| `004412EA` | Coin/sprite finite radius override; preserve native divides/coordinate scale. |
| `0044163C` | Pickup render radius override; independent close interaction/target-list checks stay native. |
| `00547CD4` | 128-pointer model metadata table; first short of metadata record must be nonzero. A loaded mesh alone is insufficient. |
| `004BC160` / `004BA1F0` | Room visibility / sphere-frustum checks retained for additional loaded actors. |
| World `+0x274/+0x278` | Loaded model table / allocated count. Require metadata, loaded model and 1–32 bones. |

`ObjectDrawDistance` defaults to **65536 renderer units**, sanitized to 1024..65536; NaN/infinity/non-positive values use default. Render overlay appends only eligible loaded actors, retains native list order and restores rendering state afterward. Native AI/collision/respawn/collection/target-lock activation and the 64 gameplay slots are not expanded.

## Renderer arena

| Pool | Native base / remaining counter | Stride | Native -> extended |
| --- | --- | --- | --- |
| World sprites | `009B2760` / `00508700` | `0x84` | 2000 -> 16384 |
| Model transforms | `0088C7D0` / `009F2F24` | `0x1B8` | 1000 -> 8192 |
| Render entries | `0095C860` / `0094FCD0` | `0x18` | 3000 -> 32768 |
| Sorted triangles | `008F7E90` / `00508728` | `0x78` | 3000 -> 32768 |

One process-lifetime arena, approximately **10.3 MiB**. Validate all **70 object patch/guard sites**, allocate before extending distance, move every producer/interior pointer/reset together, and retain native exhaustion checks. PE relocation integration audit covers **55** pool operands.

- Queue reset `004B62C0` (`ResetWorldRenderQueues`); triangle flush/reset `004B5CF0` (`FlushSortedTriangleQueue`).
- Six actual sprite allocators: `004B8A30`, `004B8E60`, `004B8F40`, `004B9020`, `004B9100`, `004B9210`.
- **Leave `004B5CF8` untouched:** `0095C860` there is the end of the 1024-pointer bucket table at `0095B860`, not a pool reference.
- Bones: rotations `00B1C3C0` (12 bytes/bone), local matrices `00B223E8` and world matrices `00B423F0` (64 bytes/bone). Extend from 32 actor slots to **65** (64 loaded actors + Buzz), 32 bones each, plus renderer list terminator. `004CD880` builds matrices; `004CDC20` renders the model list.

## Scenery/grass

- `0044127B` calls world renderer `004CDDD0`; extension is scoped around only this call.
- `004CDD10` selects detail preset. Independent distant-near / detailed-far globals **`00508D14/00508D18`** default to **10000/12000** at highest detail (other presets 3000/3500 and 5000/6500).
- Consumers `004CDE39/004CDF29` load camera **`+0x50/+0x54`** for extra cull planes; `004BA420` builds projection/frustum state. Hardware near/far are separate **`+0x48/+0x4C`**.
- **`00508D0C`** is the unchanged detailed projection far clip, normally **48000**. Extend both scenery boundaries together to **46000/48000**, preserving original overlap; never extend past hardware far clip or shrink native range.
- `004BC720` scans native grid/linked `0x94` instances, pass `+0x90`, hidden `+0x8C`, radius and frustum checks. Calls at `004CDEDA/004CDFDB` are wrapped only while world extension is active.
- `004C33F0/004C36A0` allocate separate **20x20** grids. Cell size derives from scene bounds, not a fixed terrain size. Extend scan radius to cover audited grids, cap axes at 64; `004C30D0/004C3130` still bounds every cell access.
- `004BC460` is the separate recursive room/portal renderer, not replaced by a full-world grid scan.
- Neighbourhood NGN has **374 detailed** and **214 distant** instances. Grass needs detailed pass; increasing geometry alone does not bypass the split.
- Save/restore globals even on recursion/exceptions; retain existing larger primary radius, secondary radius, hidden rooms, material alpha and native special flags.

The scenery manifest has **12** hook/guard sites and requires expanded object pools. Grass is user-confirmed. Persistent smoke is separate: [its README](../mole-hole-smoke/README.md) documents a targeted emitter patch and dedicated 64-slot visual storage, not another general gameplay-pool expansion.

## Do not blindly change shared fade/effect limits

`0043E6E0` builds the fixed fade table at **`00557C20..00559C1E`** using range **`0054BEF0`**. Legacy references are not evidence that enlarging it is safe. Leave it untouched. General particles have 64 shared gameplay records, rising effects 64 separate records, chains eight separate allocations; renderer capacity is not simulation capacity.

Detailed legacy audits: [`tests/object_rendering_notes.md`](../../tests/object_rendering_notes.md), [`tests/scenery_rendering_notes.md`](../../tests/scenery_rendering_notes.md). Replay tests validate capacities, canaries, aliases, native traversal and x87; broad visibility/animation/transition checks remain manual.
