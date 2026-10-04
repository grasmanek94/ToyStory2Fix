# Recovered object-rendering paths

Analyzed executable: `toy2.exe`, image base `00400000`, image size `00A7F000`, PE timestamp `381979B4`, SHA-256 `023eb6a9459443b34d24cf685591bfeb3b95e1acf579405f6d8fa4407ccbdaf0`.

Names/comments were applied to the Ghidra program and saved. Addresses below refer to this executable only; the patch requires matching PE metadata and **all** guarded instructions. It does not assume that every regional executable shares this layout.

## Separate distance paths

| Address | Recovered name / evidence |
| --- | --- |
| `004BC410` | `SetSceneCullDistanceSquares`: squares two float arguments and stores at `005088B0` / `005088B4`. Each store pops x87; replacing the first with NOPs leaks a value. |
| `004086F0` | `UpdateNearbyActors`: iterates 64 `0x9C`-byte slots at `0052C840`, builds `0052F1D0`, runs actor updates and timers. This is gameplay activation, not just drawing. |
| `00447BD0` | `UpdateNearbyActorRenderVisibility`: per-actor range at `+0x42`, model metadata, room and sphere/frustum checks; sets render bit `0x1` / hidden-room bit `0x2000`. |
| `004A28B0` | `BuildActorRenderList`: copies the nearby gameplay list into `00529D48`, appends Buzz (`0052F300`) and a null terminator. |
| `00440F70` | `RenderGameplayScene`: world/actor rendering, separate coin/pickup checks and native queue flushes. |
| `004412EA` | Coin/sprite render radius: native integer camera-relative X/Z deltas are divided by 16 before squaring. Replacing the range read with `distance / 4`, then retaining the native divide by 4, yields the intended finite renderer-unit radius. |
| `0044163C` | Pickup render radius: native squared float distance is divided by 1024. Replacing the range read with `distance / 8`, then retaining native division by 4, yields the same finite radius. The independent close target-list test remains untouched. |
| `0043E6E0` | `BuildDistanceFadeLookup`: writes and uses `0054BEF0` to build fixed fade tables. Raising this shared global is unsafe; only the two render-time reads are replaced. |

The render-only overlay retains native model eligibility: `00547CD4` is a **128-pointer metadata table**, and the first short of each record must be nonzero (`00447C19` / `00447C26`). A loaded mesh by itself is insufficient. The world model table is at world `+0x274`, with its allocated length at `+0x278`. Extra actors require valid metadata, a loaded model with 1–32 bones, a live/model-ready slot, native room/frustum visibility and the finite radius. Native list order and gameplay flags are restored after rendering.

## Renderer capacities

| Original pool | Base / remaining counter | Record size | Original → extended |
| --- | --- | ---: | ---: |
| World sprites | `009B2760` / `00508700` | `0x84` | 2000 → 16384 |
| Model transforms | `0088C7D0` / `009F2F24` | `0x1B8` | 1000 → 8192 |
| Render entries | `0095C860` / `0094FCD0` | `0x18` | 3000 → 32768 |
| Sorted triangles | `008F7E90` / `00508728` | `0x78` | 3000 → 32768 |

`ResetWorldRenderQueues` (`004B62C0`) resets these counters. `FlushSortedTriangleQueue` (`004B5CF0`) also resets the triangle counter. All producers decrement the remaining count before indexed writes. Consumer chains receive record pointers, so producer bases/interior pointers and reset counts must move together. The arena is approximately 10.3 MiB and lives until process exit.

Sprite allocators: `004B8A30`, `004B8E60`, `004B8F40`, `004B9020`, `004B9100`, `004B9210`. The allocator at `004B9020` was initially outside a defined Ghidra function, demonstrating why xrefs alone were insufficient. The integration test audits PE relocations and covers **55** pool-pointer operands, excluding the intentional alias below.

**Do not relocate `004B5CF8`**, although its value is `0095C860`: here it is the end of the adjacent 1024-pointer triangle-bucket table beginning at `0095B860`, not a render-entry allocation. Moving it would make the native flush walk unrelated memory.

Actor bone storage originally accommodates only 32 actors × 32 bones: rotations at `00B1C3C0` (12 bytes/bone), local matrices at `00B223E8` and world matrices at `00B423F0` (64 bytes/bone). `BuildActorBoneMatrices` (`004CD880`), `RenderActorModelList` (`004CDC20`), bone helpers and the renderer list are relocated together for **65 actors plus a list terminator**. The nearby gameplay list and the 64-slot allocation/update pool are not expanded.

## Scope and verification

The default 65536-unit radius is finite and bounded so the retained native squared pickup thresholds fit in signed 32-bit arithmetic. It is not an instruction to activate every enemy, bypass portals, change target-lock range or spawn new entities. Unloaded actors, gameplay activation limits, native effects/spawn limits and performance may still cause distant content to be absent.

The native integration test validates every patch/guard, every relocated pool pointer and the preserved bucket alias, then replays all six actual sprite constructors with mocked consumers. It fills all 16384 slots, tests exhaustion/reset and verifies new-buffer canaries and old-buffer preservation. The x87 replay verifies 512 calls with a live caller value. Neither test launches the game or modifies the executable. Visual gameplay, animation, transitions and collision/target-lock behavior still require manual A/B verification.
